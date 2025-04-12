#include "engine.hh"
#include "nic.hh"
#include <iostream>

int main(int argc, char *argv[]) {
  if (argc < 2) {
    std::cerr << "Uso: send? 1 para sim, 0 para nao";
    return 1;
  }
  const int send = atoi(argv[1]);

  NIC<Engine> nic = NIC<Engine>("lo");

  if (send) {
    for (int i = 0; i < 10; i++) {
      Ethernet::Address dest = Ethernet::Address(Ethernet::BROADCAST_ADDRESS);
      unsigned char data[5] = { 0xAA, 0xBB, 0xCC, 0xDD, 0xFF };

      Buffer<Ethernet::Frame> *buf = nic.alloc(dest, htons(ETH_P_802_EX1), 64);
      memcpy(buf->data()->data, data, 5);
      buf->setSize(buf->size() + 5);

      nic.send(buf);
    }
  } else {
    while (1) {
      sleep(10);
    }
  }
}
