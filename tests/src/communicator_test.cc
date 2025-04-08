#include "communicator.hh"
#include "engine.hh"
#include "nic.hh"
#include "protocol.hh"
#include <iostream>

int main(int argc, char *argv[]) {
  if (argc < 2) {
    std::cerr << "Uso: send? 1 para sim, 0 para nao";
    return 1;
  }

  const int send = atoi(argv[1]);

  NIC<Engine> nic = NIC<Engine>("lo");

  Protocol<NIC<Engine>> *prot = Protocol<NIC<Engine>>::getInstance(&nic);

  Protocol<NIC<Engine>>::Address addr =
      Protocol<NIC<Engine>>::Address(Ethernet::Address(), 0xEEEE);

  Communicator<Protocol<NIC<Engine>>> comm =
      Communicator<Protocol<NIC<Engine>>>(prot, addr);

  if (send) {
    for (int i = 0; i < 1; i++) {
      unsigned char data[5] = { 0xAA, 0xBB, 0xCC, 0xDD, 0xFF };
      Message message = Message(5);
      std::memcpy(message.data(), data, 5);
      comm.send(&message);
    }
  } else {
    for (int i_m = 0;; ++i_m) {
      Message message = Message(5);
      comm.receive(&message);
      std::cout << "Message " << std::dec << static_cast<int>(i_m) << ":"
                << std::endl;
      for (size_t i = 0; i < message.size(); i++) {
        std::cout << std::hex << static_cast<int>(message.data()[i]) << " ";
      }
      std::cout << std::endl;
    }
  }

  return 0;
}
