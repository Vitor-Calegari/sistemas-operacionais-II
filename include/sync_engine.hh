#ifndef SYNC_ENGINE_HH
#define SYNC_ENGINE_HH

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <semaphore>
#include <sys/types.h>
#include <thread>
#include <vector>

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
  uint64_t offset; // em nanossegundos
};

template <typename Protocol>
class SyncEngine {
public:
  typedef pid_t Stratum;
  typedef typename Protocol::Address Address;
  static const uint8_t ANNOUNCE = Protocol::PTP::ANNOUNCE;
  static const uint8_t PTP = Protocol::PTP::PTP;

  enum STATE { WAITING_DELAY = 0, WAITING_SYNC = 1 };

  enum ACTION { DO_NOTHING = 0, SEND_DELAY_REQ = 1, SEND_DELAY = 2 };

public:
  SyncEngine(Protocol *prot)
      : _protocol(prot), _iamleader(false), _announce_period(1e6),
        _announce_thread_running(false), _leader_period(1e6),
        _leader_thread_running(false), _clock(0) {
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
  // 1: sync, enviar delay req.
  // 2: delay req, enviar delay.
  // -1: erro
  int handlePTP(uint64_t timestamp, Address origin_addr, uint8_t type) {
    int ret = -1;
    if (type == ANNOUNCE) { // All
      addStratum(origin_addr.getSysID());
      ret = ACTION::DO_NOTHING;
    } else if (type == PTP && !_iamleader) { // Slave
      if (origin_addr != _master_addr) {     // Sync de um lider diferente
        // Anota tempos do PTP
        _master_addr = origin_addr;
        _sync_t = timestamp;
        _recvd_sync_t = getTimestamp();
        // Reset State Machine
        _state = STATE::WAITING_DELAY;
        ret = ACTION::SEND_DELAY_REQ;
      } else {
        if (_state == STATE::WAITING_SYNC) { // Sync
          // Anota tempos do PTP
          _sync_t = timestamp;
          _recvd_sync_t = getTimestamp();
          _state = STATE::WAITING_DELAY;
          ret = ACTION::SEND_DELAY_REQ;
        } else if (_state == STATE::WAITING_DELAY) { // Delay
          // Calcula novo offset.
          _leader_recvd_delay_req_t = timestamp;
          // TODO AQUI O CALCULO CONSIDERA QUE T3 E T2 SÃO IGUAIS, POREM NA
          // REALIDADE OS TEMPOS SERIAM DIFERENTES, POIS, T2 SERIA OBTIDO PELA
          // PLACA DE REDE AO RECEBER SYNC E T3 SERIA INFORMADO PELA PLACA DE
          // REDE AO REALMENTE ENVIAR A MENSAGEM.
          uint64_t delay = ((_leader_recvd_delay_req_t - _recvd_sync_t) +
                            (_recvd_sync_t - _sync_t)) /
                           2;
          uint64_t offset = (_recvd_sync_t - _sync_t) - delay;
          _clock.setOffset(offset);
          _state = STATE::WAITING_SYNC;
          ret = ACTION::DO_NOTHING;
        }
      }
    } else if (type == PTP && _iamleader) { // Master
      // Lider não tem maquina de estados
      ret = ACTION::SEND_DELAY;
    }
    return ret;
  }

  uint64_t getTimestamp() const {
    std::chrono::time_point<std::chrono::steady_clock> now = _clock.now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               now.time_since_epoch())
        .count();
  }

private:
  void addStratum(Stratum stratum) {
    std::lock_guard<std::mutex> lock(_strata_mutex);
    _knownStrata.push_back(stratum);
  }

  void clearStrata() {
    std::lock_guard<std::mutex> lock(_strata_mutex);
    _knownStrata.clear();
  }

  void startAnnounceThread() {
    _announce_thread_running = true;
    _announce_thread = std::thread([this]() {
      while (_announce_thread_running) {
        // Anuncia que está na rede
        Address myaddr = _protocol->getAddr();
        Address broadcast = _protocol->getBroadcastAddr();
        uint8_t type = ANNOUNCE;
        _protocol->send(myaddr, broadcast, type);
        // Espera eventuais anuncios de outros veiculos
        std::chrono::_V2::steady_clock::time_point next_wakeup_t = std::chrono::steady_clock::now() +
                             std::chrono::microseconds(_announce_period);
        std::this_thread::sleep_until(next_wakeup_t);
        if (!_announce_thread_running) break;
        // Verifica se é lider
        _iamleader = amILeader();
        clearStrata();
        // Se é lider, começa a mandar syncs periodicos
        // se não é, volta a esperar
        _leader_cv.notify_one();
      }
    });
  }

  void stopAnnounceThread() {
    #ifdef DEBUG_SYNC
      std::cout << get_timestamp() << " Stopping Announce " << getpid() << std::endl;
    #endif
    if (_announce_thread_running) {
      _announce_thread_running = false;
      if (_announce_thread.joinable()) {
        _announce_thread.join();
      }
    }
    #ifdef DEBUG_SYNC
      std::cout << get_timestamp() << " Announce stopped" << getpid() << std::endl;
    #endif
  }

  void startLeaderThread() {
    _leader_thread_running = true;
    _leader_thread = std::thread([this]() {
      while (_leader_thread_running) {
        // Primeira iteração sempre bloqueia. Enquanto eu for lider, não
        // bloqueia.
        std::unique_lock<std::mutex> lock(_leader_mutex);
        _leader_cv.wait(lock, [this]() { return _iamleader || !_leader_thread_running; });
        #ifdef DEBUG_SYNC
          std::cout << get_timestamp() << " Leader running " << _leader_thread_running << std::endl;
        #endif
        if (!_leader_thread_running) break;

        auto next_wakeup_t = std::chrono::steady_clock::now() +
                             std::chrono::microseconds(_leader_period);

        // Envia Sync Broadcast
        Address myaddr = _protocol->getAddr();
        Address broadcast = _protocol->getBroadcastAddr();
        uint8_t type = PTP;
        _protocol->send(myaddr, broadcast, type);

        std::this_thread::sleep_until(next_wakeup_t);
       }
    });
  }

  void stopLeaderThread() {
    #ifdef DEBUG_SYNC
      std::cout << get_timestamp() << " Stopping Leader " << getpid() << std::endl;
    #endif
    if (_leader_thread_running) {
      _leader_thread_running = false;
      _leader_cv.notify_one();
      if (_leader_thread.joinable()) {
        _leader_thread.join();
      }
    }
    #ifdef DEBUG_SYNC
      std::cout << get_timestamp() << " Leader stopped" << getpid() << std::endl;
    #endif
  }

  bool amILeader() {
    std::lock_guard<std::mutex> lock(_strata_mutex);
    Stratum myStratum = _protocol->getSysID();
    bool ret = true;
    for (const auto &stratum : _knownStrata) {
      if (stratum < myStratum) {
        ret = false;
        break;
      }
    }
    return ret;
  }

private:
  Protocol *_protocol;
  std::atomic<bool> _iamleader;
  // Stratum --------------------------------------
  std::vector<Stratum> _knownStrata{};
  std::mutex _strata_mutex;
  // Need Sync Thread -----------------------------
  uint64_t _announce_period;
  std::thread _announce_thread;
  std::atomic<bool> _announce_thread_running;
  // Leader Thread --------------------------------
  std::thread _leader_thread;
  uint64_t _leader_period;
  std::atomic<bool> _leader_thread_running;
  std::condition_variable _leader_cv;
  std::mutex _leader_mutex;
  // PTP ------------------------------------------
  uint64_t _sync_t;
  uint64_t _recvd_sync_t;
  uint64_t _delay_req_t;
  uint64_t _leader_recvd_delay_req_t;
  Address _master_addr;
  int _state;
  SimulatedClock _clock;
};

#endif
