#ifndef SYNC_ENGINE_HH
#define SYNC_ENGINE_HH

#include "control.hh"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <set>
#include <sys/types.h>
#include <thread>

#ifdef DEBUG_SYNC
#include "utils.hh"
#endif

class SimulatedClock {
public:
  SimulatedClock(uint64_t offset_ns = 0) : offset(offset_ns) {
  }

  // Define novo offset
  void setOffset(uint64_t offset_ns) {
    offset = offset_ns;
  }

  // Retorna offset atual
  uint64_t getOffset() const {
    return offset;
  }

  // Retorna tempo atual com offset aplicado
  std::chrono::time_point<std::chrono::steady_clock> now() const {
    return std::chrono::steady_clock::now() - std::chrono::nanoseconds(offset);
  }

private:
  uint64_t offset{}; // em nanossegundos
};

template <typename Protocol>
class SyncEngine {
public:
  typedef pid_t SysID;
  typedef typename Protocol::Address Address;
  static constexpr auto ANNOUNCE = Control::Type::ANNOUNCE;
  static constexpr auto PTP = Control::Type::PTP;

  enum State { WAITING_DELAY = 0, WAITING_SYNC = 1 };

  enum Action { DO_NOTHING = 0, SEND_DELAY_REQ = 1, SEND_DELAY_RESP = 2 };

  static constexpr int HALF_LIFE = 0.45e6;

public:
  SyncEngine(Protocol *prot)
      : _protocol(prot), _iamleader(false), _leader(-1),
        _announce_period(HALF_LIFE), _announce_thread_running(false),
        _leader_period(HALF_LIFE), _leader_thread_running(false), _clock(0),
        _synced(false), _announce_iteration(0), _broadcast_already_sent(false) {
    startAnnounceThread();
    startLeaderThread();
  }

  ~SyncEngine() {
    stopAnnounceThread();
    stopLeaderThread();
  }

  // Função para lidar com a lógica do PTP. Cuida da logica do master,
  // quanto do slave.
  // return: int.
  // 0: announce ou delay, não fazer nada.
  // 1: sync ou delay_req, responder
  Action handlePTP(uint64_t recv_timestamp, uint64_t msg_timestamp,
                   Address origin_addr, Control::Type type) {
    Action ret = Action::DO_NOTHING;
    addSysID(origin_addr.getSysID());

    // Se for PTP e não sou lider
    if (type == PTP && !_iamleader) {    // Slave
      if (origin_addr != _master_addr) { // Sync de um lider diferente
        // Anota tempos do PTP
        _master_addr = origin_addr;
        _sync_t = msg_timestamp;
        _recvd_sync_t = recv_timestamp;
        // Reset State Machine
        _state = State::WAITING_DELAY;
        ret = Action::SEND_DELAY_REQ;
      } else {
        if (_state == State::WAITING_SYNC) { // Sync
          // Anota tempos do PTP
          _sync_t = msg_timestamp;
          _recvd_sync_t = recv_timestamp;
          _state = State::WAITING_DELAY;
          ret = Action::SEND_DELAY_REQ;
        } else if (_state == State::WAITING_DELAY) { // Delay
          // Calcula novo offset.
          _leader_recvd_delay_req_t = msg_timestamp;

          uint64_t delay = ((_leader_recvd_delay_req_t - _delay_req_t) +
                            (_recvd_sync_t - _sync_t)) /
                           2;

          uint64_t offset = (_recvd_sync_t - _sync_t) - delay;
          _clock.setOffset(offset);
          _state = State::WAITING_SYNC;
          _synced = true;
          _announce_iteration = 0;
        }
      }
      // Se for PTP e sou lider
    } else if (type == PTP && _iamleader) { // Master
      // Lider não tem maquina de estados
      ret = Action::SEND_DELAY_RESP;
    }

    return ret;
  }

  uint64_t getTimestamp() const {
    std::chrono::time_point<std::chrono::steady_clock> now = _clock.now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               now.time_since_epoch())
        .count();
  }

  void setBroadcastAlreadySent(bool already_sent) {
    _broadcast_already_sent = already_sent;
  }

  void setDelayReqSendT(uint64_t time) {
    _delay_req_t = time;
  }

  bool getSynced() {
    return _synced;
  }

  bool amILeader() const {
    return _iamleader;
  }

  State getCurState() const {
    return _state;
  }

  uint64_t getClockOffset() const {
    return _clock.getOffset();
  }

private:
  void startAnnounceThread() {
#ifdef DEBUG_SYNC
    std::cout << get_timestamp() << " Announce thread started " << getpid()
              << std::endl;
#endif
    _announce_thread_running = true;
    _announce_thread = std::thread([this]() {
      while (_announce_thread_running) {
        if (!_iamleader && _announce_iteration == 1) {
          _synced = false;
        }

        if (!_broadcast_already_sent) {
          // Anuncia que está na rede
          Address myaddr = _protocol->getAddr();
          Address broadcast = _protocol->getBroadcastAddr();
          Control ctrl(ANNOUNCE);
          _protocol->send(myaddr, broadcast, ctrl);
        } else {
          setBroadcastAlreadySent(false);
        }
        // Espera eventuais anuncios de outros veiculos
        std::chrono::_V2::steady_clock::time_point next_wakeup_t =
            std::chrono::steady_clock::now() +
            std::chrono::microseconds(_announce_period);
        std::this_thread::sleep_until(next_wakeup_t);
        if (!_announce_thread_running)
          break;

        // Só elege se ter alguém na rede
        if (_known_sysid.size() != 0) {
          SysID last_leader = _leader;
          _leader = elect();
          _iamleader = _leader == _protocol->getSysID();
          clearKnownSysID();
          if (_iamleader) { // Se eu for lider, me considero sincronizado
            _synced = true;
          } else if (last_leader !=
                     _leader) { // Se mudou o lider, me considero desincronizado
            _synced = false;
          }
#ifdef DEBUG_SYNC
          if (_iamleader) {
            std::cout << get_timestamp() << " I’m Leader " << getpid()
                      << std::endl;
          }
#endif
          // Se é lider, começa a mandar syncs periodicos
          // se não é, volta a esperar
          _leader_cv.notify_one();
        } else {
          _iamleader = false;
          _synced = false;
#ifdef DEBUG_SYNC
          std::cout << get_timestamp() << " No one around " << getpid()
                    << ": No election." << std::endl;
#endif
        }
        _announce_iteration = (_announce_iteration + 1) % 2;
      }
    });
  }

  void stopAnnounceThread() {
#ifdef DEBUG_SYNC
    std::cout << get_timestamp() << " Stopping Announce thread " << getpid()
              << std::endl;
#endif
    if (_announce_thread_running) {
      _announce_thread_running = false;
      if (_announce_thread.joinable()) {
        _announce_thread.join();
      }
    }
#ifdef DEBUG_SYNC
    std::cout << get_timestamp() << " Announce thread stopped " << getpid()
              << std::endl;
#endif
  }

  void startLeaderThread() {
#ifdef DEBUG_SYNC
    std::cout << get_timestamp() << " Leader thread started " << getpid()
              << std::endl;
#endif
    _leader_thread_running = true;
    _leader_thread = std::thread([this]() {
      while (_leader_thread_running) {
        // Primeira iteração sempre bloqueia. Enquanto eu for lider, não
        // bloqueia.
        std::unique_lock<std::mutex> lock(_leader_mutex);
        _leader_cv.wait(
            lock, [this]() { return _iamleader || !_leader_thread_running; });

        if (!_leader_thread_running)
          break;

        auto next_wakeup_t = std::chrono::steady_clock::now() +
                             std::chrono::microseconds(_leader_period);

        // Envia Sync Broadcast
        Address myaddr = _protocol->getAddr();
        Address broadcast = _protocol->getBroadcastAddr();
        Control ctrl(PTP);
        _protocol->send(myaddr, broadcast, ctrl);

        std::this_thread::sleep_until(next_wakeup_t);
      }
    });
  }

  void stopLeaderThread() {
#ifdef DEBUG_SYNC
    std::cout << get_timestamp() << " Stopping Leader thread " << getpid()
              << std::endl;
#endif
    if (_leader_thread_running) {
      _leader_thread_running = false;
      _leader_cv.notify_one();
      if (_leader_thread.joinable()) {
        _leader_thread.join();
      }
    }
#ifdef DEBUG_SYNC
    std::cout << get_timestamp() << " Leader thread stopped " << getpid()
              << std::endl;
#endif
  }

  SysID elect() {
    std::lock_guard<std::mutex> lock(_strata_mutex);

    auto mySysID = _protocol->getSysID();
    auto min_known_sysid =
        *std::min_element(_known_sysid.cbegin(), _known_sysid.cend());
    auto elected = std::min(mySysID, min_known_sysid);

#ifdef DEBUG_SYNC
    std::cout << get_timestamp() << ' ' << elected
              << " was elected as leader by " << getpid() << '.' << std::endl;
#endif

    return elected;
  }

  void addSysID(SysID SysID) {
    std::lock_guard<std::mutex> lock(_strata_mutex);
    _known_sysid.insert(SysID);
  }

  void clearKnownSysID() {
    std::lock_guard<std::mutex> lock(_strata_mutex);
    _known_sysid.clear();
  }

private:
  Protocol *_protocol = nullptr;

  // SysID ----------------------------------------
  std::atomic<bool> _iamleader = false;
  std::set<SysID> _known_sysid{};
  std::mutex _strata_mutex;
  SysID _leader = -1;

  // Need Sync Thread -----------------------------
  std::thread _announce_thread;
  uint64_t _announce_period = HALF_LIFE;
  std::atomic<bool> _announce_thread_running = false;

  // Leader Thread --------------------------------
  std::thread _leader_thread;
  uint64_t _leader_period = HALF_LIFE;
  std::atomic<bool> _leader_thread_running = false;
  std::condition_variable _leader_cv;
  std::mutex _leader_mutex;

  // PTP ------------------------------------------
  uint64_t _sync_t{};
  uint64_t _recvd_sync_t{};
  uint64_t _delay_req_t{};
  uint64_t _leader_recvd_delay_req_t{};

  Address _master_addr;
  State _state{};
  SimulatedClock _clock;
  std::atomic<bool> _synced = false;
  int _announce_iteration{};

  // Optimizations --------------------------------
  std::atomic<bool> _broadcast_already_sent = false;
};

#endif
