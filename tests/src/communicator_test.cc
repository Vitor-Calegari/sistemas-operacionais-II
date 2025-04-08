#include "communicator.hh"
#include "engine.hh"
#include "nic.hh"
#include "protocol.hh"
#include <iostream>

#define NUM_MSGS 10

int main(int argc, char *argv[]) {
  int send;
  if (argc < 2) {
    // Novo processo será o receiver.
    auto ret = fork();

    send = ret != 0;
  } else {
    send = atoi(argv[1]);
  }

  NIC<Engine> nic = NIC<Engine>("lo");

  Protocol<NIC<Engine>> *prot = Protocol<NIC<Engine>>::getInstance(&nic);

  Protocol<NIC<Engine>>::Address addr =
      Protocol<NIC<Engine>>::Address(Ethernet::Address(), 0xEEEE);

  Communicator<Protocol<NIC<Engine>>> comm =
      Communicator<Protocol<NIC<Engine>>>(prot, addr);

  if (send) {
    for (int i = 0; i < NUM_MSGS; i++) {
      unsigned char data[5] = { 0xAA, 0xBB, 0xCC, 0xDD, 0xFF };
      Message message = Message(5);
      std::memcpy(message.data(), data, 5);
      std::cout << "Sending (" << std::dec << i << "): ";
      for (size_t i = 0; i < message.size(); i++) {
        std::cout << std::hex << static_cast<int>(message.data()[i]) << " ";
      }
      std::cout << std::endl;
      comm.send(&message);
    }
  } else {
    for (int i_m = 0; argc >= 2 || i_m < NUM_MSGS; ++i_m) {
      Message message = Message(5);
      comm.receive(&message);
      std::cout << "Received (" << std::dec << i_m << "): ";
      for (size_t i = 0; i < message.size(); i++) {
        std::cout << std::hex << static_cast<int>(message.data()[i]) << " ";
      }
      std::cout << std::endl;
    }
  }

  return 0;
}
