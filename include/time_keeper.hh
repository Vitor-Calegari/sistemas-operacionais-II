#include <atomic>
#include <chrono>
#include <mutex>
#include <semaphore>
#include <sys/types.h>
#include <thread>
#include <vector>
#include <condition_variable>


template <typename Protocol>
class SyncEngine {
public:
  typedef pid_t Stratum;
  typedef Protocol::Address Address;
  static const ANNOUNCE = Protocol::PTP::ANNOUNCE;
  static const PTP = Protocol::PTP::PTP;

public:
  SyncEngine(Protocol *prot)
      : _protocol(prot), _iamleader(0), _need_sync_period(1e6),
        _need_sync_thread_running(false), _leader_period(1e6),
        _leader_thread_running(false) {
    startNeedSyncThread();
    startLeaderThread();
  }

  ~SyncEngine() {
    stopNeedSyncThread();
    stopLeaderThread();
  }

  void addStratum(Stratum stratum) {
    std::lock_guard<std::mutex> lock(_strata_mutex);
    _knownStrata.push_back(stratum);
  }

  void clearStrata() {
    std::lock_guard<std::mutex> lock(_strata_mutex);
    _knownStrata.clear();
  }

  void handlePTP(uint32_t timestamp, Address origin_addr, uint8_t type) {
    if (type == ANNOUNCE) { // All
      addStratum(origin_addr.getSysID());
    } else if (type == PTP && !_iamleader) {  // Slave
      if (origin_addr != _master_addr) {
        // Reset State Machine
        _state = 0;
        _master_addr = origin_addr;
        // Anota tempos do PTP
        _sync_t = timestamp;
        _recvd_sync_t = 0; // TODO obter tempo de agora
        // Envia Delay Req
        Address myaddr = _protocol->getAddr();
        _protocol->send(myaddr, origin_addr, PTP, nullptr, 0);
      } else {
        if (_state == 0) { // Sync
          // Anota tempos do PTP
          _sync_t = timestamp;
          _recvd_sync_t = 0; // TODO obter tempo de agora
          // Envia Delay Req
          Address myaddr = _protocol->getAddr();
          _protocol->send(myaddr, origin_addr, PTP, nullptr, 0);
          _state++;
        } else if (_state == 1) { // Delay
          _leader_recvd_delay_req_t = timestamp;
          // TODO atualizar seu relógio
          _state = 0;
        }
      }
    } else if (type == PTP && _iamleader) {  // Master
        // Envia Delay
        Address myaddr = _protocol->getAddr();
        _protocol->send(myaddr, origin_addr, PTP, nullptr, 0);
    }
  }

private:
  void startNeedSyncThread() {
    _need_sync_thread_running = true;
    _need_sync_thread = std::thread([this]() {
      while (_need_sync_thread_running) {
        // Anuncia que está na rede
        Address myaddr = _protocol->getAddr();
        Address broadcast = _protocol->getBroadcastAddr();
        uint8_t type = Protocol::PTP::ANNOUNCE;
        _protocol->send(myaddr, broadcast, type, nullptr, 0);
        // Espera eventuais anuncios de outros veiculos
        auto next_wakeup_t = std::chrono::steady_clock::now() +
                             std::chrono::seconds(_need_sync_period);
        std::this_thread::sleep_until(next_wakeup_t);
        // Verifica se é lider
        bool _iamleader = amILeader();
        clearStrata();
        // Se é lider, começa a mandar syncs periodicos
        // se não é, volta a esperar
        _leader_cv.notify_one();
      }
    });
  }

  void stopNeedSyncThread() {
    if (_need_sync_thread_running) {
      _need_sync_thread_running = false;
      if (_need_sync_thread.joinable()) {
        _need_sync_thread.join();
      }
    }
  }

  void startLeaderThread() {
    _leader_thread_running = true;
    _leader_thread = std::thread([this]() {
      while (_leader_thread_running) {
        std::unique_lock<std::mutex> lock(_leader_mutex);
        // Primeira iteração sempre bloqueia. Enquanto eu for lider, não bloqueia.
        _leader_cv.wait(lock, [this]() { return _iamleader; });
        auto next_wakeup_t = std::chrono::steady_clock::now() +
                             std::chrono::seconds(_leader_period);

        // Envia Sync Broadcast
        Address myaddr = _protocol->getAddr();
        Address broadcast = _protocol->getBroadcastAddr();
        uint8_t type = Protocol::PTP::PTP;
        _protocol->send(myaddr, broadcast, type, nullptr, 0);

        std::this_thread::sleep_until(next_wakeup_t);
      }
    });
  }

  void stopLeaderThread() {
    if (_leader_thread_running) {
      _leader_thread_running = false;
      if (_leader_thread.joinable()) {
        _leader_thread.join();
      }
    }
  }

  bool amILeader() {
    Stratum myStratum = _protocol->getSysID();
    bool ret = true;
    _strata_mutex.lock();
    for (const auto &stratum : _knownStrata) {
      if (stratum < myStratum) {
        ret = false;
        break;
      }
    }
    _strata_mutex.unlock();
    return ret;
  }

private:
  Protocol *_protocol;
  bool _iamleader;
  // Stratum --------------------------------------
  std::mutex _strata_mutex;
  std::vector<Stratum> _knownStrata{};
  // Need Sync Thread -----------------------------
  uint32_t _need_sync_period;
  std::thread _need_sync_thread;
  std::atomic<bool> _need_sync_thread_running;
  // Leader Thread --------------------------------
  std::thread _leader_thread;
  uint32_t _leader_period;
  std::atomic<bool> _leader_thread_running;
  std::condition_variable _leader_cv;
  std::mutex _leader_mutex;
  // PTP ------------------------------------------
  uint32_t _sync_t;
  uint32_t _recvd_sync_t;
  uint32_t _delay_req_t;
  uint32_t _leader_recvd_delay_req_t;
  Address _master_addr;
  int _state;
  std::mutex _state_mutex;
};
