#include "communicator.hh"
#include "engine.hh"
#include "nic.hh"
#include "protocol.hh"
#include <cstddef>
#include <iostream>
#include <random>

#define NUM_MSGS 1000
#define MSG_SIZE 5

int randint(int p, int r) {
  std::random_device rd;
  std::mt19937 rng(rd());
  std::uniform_int_distribution<int> uni(p, r);

  return uni(rng);
}

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
      Message message = Message(MSG_SIZE);
      std::cout << "Sending (" << std::dec << i << "): ";
      for (size_t i = 0; i < message.size(); i++) {
        message.data()[i] = std::byte(randint(0, 255));
        std::cout << std::hex << static_cast<int>(message.data()[i]) << " ";
      }
      std::cout << std::endl;
      comm.send(&message);
    }
  } else {
    for (int i_m = 0; argc >= 2 || i_m < 2 * NUM_MSGS; ++i_m) {
      Message message = Message(MSG_SIZE);
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
