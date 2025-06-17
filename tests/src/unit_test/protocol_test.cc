#include "engine.hh"
#include "shared_engine.hh"
#include "nic.hh"
#include "protocol.hh"
#include <iostream>

int main(int argc, char *argv[]) {
  if (argc < 2) {
    std::cerr << "Uso: send? 1 para sim, 0 para nao";
    return 1;
  }
  const int send = atoi(argv[1]);

  using SocketNIC = NIC<Engine<Ethernet>>;
  using SharedMemNIC = NIC<SharedEngine<Ethernet>>;
  using Protocol = Protocol<SocketNIC, SharedMemNIC, NavigatorDirected>;

  SocketNIC rsnic = SocketNIC("lo");
  SharedMemNIC smnic = SharedMemNIC("lo");

  Protocol &prot = Protocol::getInstance(&rsnic, &smnic, getpid());

  if (send) {
    for (int i = 0; i < 10; i++) {
      Protocol::Address dest =
          Protocol::Address(Ethernet::BROADCAST_ADDRESS, getpid(), 10);
      unsigned char data[5] = { 0xAA, 0xBB, 0xCC, 0xDD, 0xFF };

      Protocol::Address from =
          Protocol::Address(Ethernet::Address(), getpid(), 10);
      prot.send(from, dest, data, 5);
    }
  } else {
    while (1) {
      sleep(10);
    }
  }
}
