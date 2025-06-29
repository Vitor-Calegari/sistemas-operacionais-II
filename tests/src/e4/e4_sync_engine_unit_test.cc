// #define DEBUG_SYNC
#include "engine.hh"
#include "ethernet.hh"
#include "nic.hh"
#include "protocol.hh"
#include "shared_engine.hh"
#include "sync_engine.hh"
#include "shared_mem.hh"
// #undef DEBUG_SYNC

#include <iostream>

#ifndef INTERFACE_NAME
#define INTERFACE_NAME "lo"
#endif

std::string bool_to_str(bool b) {
  if (b) {
    return "true";
  } else {
    return "false";
  }
}

template <typename SyncEngine>
std::string state_to_str(const SyncEngine &s) {
  auto state = s.getCurState();

  switch (state) {
  case SyncEngine::State::WAITING_DELAY:
    return "Waiting delay";
  case SyncEngine::State::WAITING_SYNC:
    return "Waiting sync";
  }

  return "";
}

template <typename SyncEngine>
void print_action(typename SyncEngine::Action action) {
  std::cout << "PTP Action: ";
  switch (action) {
  case SyncEngine::Action::DO_NOTHING:
    std::cout << "Do nothing";
    break;
  case SyncEngine::Action::SEND_DELAY_REQ:
    std::cout << "Send delay request";
    break;
  case SyncEngine::Action::SEND_DELAY_RESP:
    std::cout << "Send delay response";
  }
  std::cout << std::endl;
}

class ProtocolMock {
public:
  using BufferEthernet = Buffer<Ethernet>;
  using SocketNIC = NIC<Engine<BufferEthernet>>;
  using SharedMemNIC = NIC<SharedEngine<BufferEthernet>>;
  using ProtocolNIC = Protocol<SocketNIC, SharedMemNIC>;
  using Address = ProtocolNIC::Address;
  using SyncEngineP = SyncEngine<ProtocolMock>;

  ProtocolMock(ProtocolNIC::SysID sysID) : _sysID(sysID), _sync_engine(this){};

  Address getAddr() {
    return Address();
  }

  ProtocolNIC::SysID getSysID() {
    return _sysID;
  }

  Address getBroadcastAddr() {
    return Address();
  }

  int send([[maybe_unused]] Address &from, Address &to,
           [[maybe_unused]] Control &ctrl) {
    if (to.getSysID() == ProtocolNIC::UNIVERSAL_BROADCAST) {
      _sync_engine.setBroadcastAlreadySent(true);
    }
    return 0;
  }

  static ProtocolMock &getInstance([[maybe_unused]] const char *interface_name,
                                   ProtocolNIC::SysID sysID) {
    static ProtocolMock instance(sysID);

    return instance;
  }

  void update(uint64_t recv_timestamp, uint64_t msg_timestamp,
              Address origin_addr = ProtocolNIC::Address()) {
    auto action = _sync_engine.handlePTP(recv_timestamp, msg_timestamp,
                                         origin_addr, SyncEngineP::PTP);

    print_action<SyncEngineP>(action);
    std::cout << std::endl;
    switch (action) {
    case SyncEngineP::Action::DO_NOTHING:
      break;
    case SyncEngineP::Action::SEND_DELAY_REQ:
      break;
    case SyncEngineP::Action::SEND_DELAY_RESP:
      break;
    }
  }

  void set_delay(uint64_t time) {
    _sync_engine.setDelayReqSendT(time);
  }

  void print_sync_engine() {
    std::cout << "================ SyncEngine ===============" << std::endl;
    std::cout << "Is leader: " << bool_to_str(_sync_engine.amILeader())
              << std::endl;
    std::cout << "Current state: " << state_to_str(_sync_engine) << std::endl;
    std::cout << "Clock offset: " << _sync_engine.getClockOffset() << std::endl;
    std::cout << "Is synced: " << bool_to_str(_sync_engine.getSynced())
              << std::endl;
    std::cout << "===========================================" << std::endl
              << std::endl;
  }

private:
  ProtocolNIC::SysID _sysID;
  SyncEngineP _sync_engine;
};

int main() {
  using Protocol = ProtocolMock;

  Protocol::Address origin_addr({}, getpid() + 2, 12);

  Protocol &prot = Protocol::getInstance(INTERFACE_NAME, getpid());
  prot.print_sync_engine();

  int64_t T1, T2, T3, T4 = 0;

  std::cout << "\n\nTest with captured timestamps\n" << std::endl;

  T1 = 22045047862401;  // Time that Sync was sent by leader
  T2 = 22045048171391;  // Time that Sync was received by slave
  T3 = 22045048175594;  // Time that Delay Req was sent by slave
  T4 = 22045048151500;  // Time that Delay was sent by leader

  std::cout << "Received Sync" << std::endl;
  // Sync
  prot.update(T2, T1, origin_addr);

  prot.print_sync_engine();

  std::cout << "Received Delay" << std::endl;
  // Delay received
  prot.set_delay(T3);
  prot.update(T3, T4, origin_addr);

  prot.print_sync_engine();

  std::cout << "\n\nTest with simple timestamps\n" << std::endl;

  T1 = 10;  // Time that Sync was sent by leader
  T2 = 15;  // Time that Sync was received by slave
  T3 = 16;  // Time that Delay Req was sent by slave
  T4 = 17;  // Time that Delay was sent by leader

  std::cout << "Received Sync" << std::endl;
  // Sync
  prot.update(T2, T1, origin_addr);

  prot.print_sync_engine();

  std::cout << "Received Delay" << std::endl;
  // Delay received
  prot.set_delay(T3);
  prot.update(T3, T4, origin_addr);

  prot.print_sync_engine();

  return 0;
}
