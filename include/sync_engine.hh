#ifndef SYNC_ENGINE_HH
#define SYNC_ENGINE_HH

#include "control.hh"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <set>
#include <sys/types.h>
#include <thread>
#include <unordered_map>

#if defined(DEBUG_SYNC) || defined(DEBUG_TIMESTAMP)
#include "utils.hh"
#endif

class SimulatedClock {
public:
  SimulatedClock(int64_t offset_us = 0) : offset(offset_us) {
  }

  // Define novo offset
  void setOffset(int64_t offset_us) {
    offset = offset_us;
  }

  // Retorna offset atual
  int64_t getOffset() const {
    return offset;
  }

  // Retorna tempo atual com offset aplicado
  std::chrono::time_point<std::chrono::system_clock> now() const {
    return std::chrono::system_clock::now() - std::chrono::microseconds(offset);
  }

  int64_t getTimestamp() const {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               now().time_since_epoch())
        .count();
  }

private:
  int64_t offset{};
};

template <typename Protocol>
class SyncEngine {
public:
  typedef pid_t SysID;
  typedef typename Protocol::Address Address;
  static constexpr auto ANNOUNCE = Control::Type::ANNOUNCE;
  static constexpr auto DELAY_RESP = Control::Type::DELAY_RESP;
  static constexpr auto LATE_SYNC = Control::Type::LATE_SYNC;

  static constexpr int HALF_LIFE = 0.45e6;
  static constexpr int IT_TO_NEED_SYNC = 1;
  static constexpr int IT_TO_DESYNC = 3;

public:
  SyncEngine(Protocol *prot, bool isRSU)
      : _protocol(prot), _announce_period(HALF_LIFE),
        _announce_thread_running(false), _clock(0), _synced(false),
        _needSync(true), _announce_iteration(0), _broadcast_already_sent(false),
        _isRSU(isRSU) {
    if (!_isRSU) {
      startAnnounceThread();
    }
  }

  ~SyncEngine() {
    if (!_isRSU) {
      stopAnnounceThread();
    }
  }

  // Função para lidar com a lógica do PTP. Cuida da logica do master,
  // quanto do slave.
  // return: int.
  // 0: announce ou delay, não fazer nada.
  // 1: sync ou delay_req, responder
  void handlePTP(int64_t recv_timestamp, int64_t msg_timestamp,
                 Address origin_addr, Control::Type type,
                 int64_t timestamp_related_to) {
    if (type == DELAY_RESP) {            // delay_resp
      if (origin_addr != _master_addr) { // delay_resp de uma RSU diferente
        // Anota delay_req_t e delay_resp_t
        _map_delay_req_delay_resp_t[timestamp_related_to] = msg_timestamp;
        // Reset State Machine
        _master_addr = origin_addr;
      } else {
        // Anota delay_req_t e delay_resp_t
        _map_delay_req_delay_resp_t[timestamp_related_to] = msg_timestamp;
      }
    } else if (type == LATE_SYNC) { // Late Sync
      // Caso eu não tenha recebido Delay_Resp antes de ter recebido o late
      // sync, retorna
      if (_map_delay_req_delay_resp_t.find(timestamp_related_to) ==
          _map_delay_req_delay_resp_t.end()) {
        return;
      }

      // Tempo que lider enviou o Late Sync
      int64_t t1 = msg_timestamp;
      // Tempo que slave recebeu Late Sync
      int64_t t2 = recv_timestamp;
      // Tempo em que slave requisitou Delay
      int64_t t3 = timestamp_related_to;
      // Tempo em que Lider recebeu a requisiçao do Delay
      int64_t t4 = _map_delay_req_delay_resp_t[timestamp_related_to];

      // Calcula novo offset.
      int64_t delay = ((t4 - t3) + (t2 - t1)) / 2;
      int64_t offset = (t2 - t1) - delay;

      _clock.setOffset(offset);
      _synced = true;
      _needSync = false;
      _announc_it_mtx.lock();
      _announce_iteration = 0;
      _announc_it_mtx.unlock();
      _map_delay_req_delay_resp_t.clear();
#ifdef DEBUG_TIMESTAMP
      std::cout << get_timestamp() << " I’m Car " << getpid()
                << " My offset is " << getClockOffset() << std::endl;
#endif
    }
    return;
  }

  int64_t getTimestamp() const {
    return _clock.getTimestamp();
  }

  void setBroadcastAlreadySent(bool already_sent) {
    _broadcast_already_sent = already_sent;
  }

  bool getSynced() {
    return _synced || _isRSU;
  }

  bool getNeedSync() {
    return _needSync;
  }

  SimulatedClock *getClock() {
    return &_clock;
  }

  int64_t getClockOffset() const {
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
        _announc_it_mtx.lock();
        if (_announce_iteration == IT_TO_NEED_SYNC) {
          _needSync = true;
        } else if (_announce_iteration == IT_TO_DESYNC && _needSync) {
          _synced = false;
        }

#ifdef DEBUG_TIMESTAMP
        printSyncMsg(_needSync, _synced, _announce_iteration);
#endif
        _announce_iteration = (_announce_iteration + 1) % (IT_TO_DESYNC + 1);
        _announc_it_mtx.unlock();

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

private:
  Protocol *_protocol = nullptr;
  // Need Sync Thread -----------------------------
  std::thread _announce_thread;
  int64_t _announce_period = HALF_LIFE;
  std::atomic<bool> _announce_thread_running = false;

  // PTP ------------------------------------------
  // Map que relaciona tempos de delay_req(chave) e tempos de delay_resp(valor)
  std::unordered_map<int64_t, int64_t> _map_delay_req_delay_resp_t;

  Address _master_addr;
  SimulatedClock _clock;
  std::atomic<bool> _synced = false;
  std::atomic<bool> _needSync = true;
  std::mutex _announc_it_mtx;
  int _announce_iteration{};

  // Optimizations --------------------------------
  std::atomic<bool> _broadcast_already_sent = false;
  bool _isRSU;
};

#endif
