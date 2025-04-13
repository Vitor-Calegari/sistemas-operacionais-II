#include "communicator.hh"
#include "engine.hh"
#include "nic.hh"
#include "protocol.hh"
#include <csignal>
#include <cstddef>
#include <iostream>
#include <random>
#include <sys/wait.h>

#define NUM_MSGS 10000
#define MSG_SIZE 5

#ifndef INTERFACE_NAME
#define INTERFACE_NAME "lo"
#endif

int randint(int p, int r) {
  std::random_device rd;
  std::mt19937 rng(rd());
  std::uniform_int_distribution<int> uni(p, r);

  return uni(rng);
}

int main(int argc, char *argv[]) {
  int send;
  if (argc < 2) {
    // Novo processo será o sender.
    auto ret = fork();

    send = ret == 0;

    if (ret == 0) {
      sleep(1);
    }
  } else {
    send = atoi(argv[1]);
  }

  NIC<Engine> nic = NIC<Engine>(INTERFACE_NAME);

  Protocol<NIC<Engine>> *prot = Protocol<NIC<Engine>>::getInstance(&nic);

  Protocol<NIC<Engine>>::Address addr =
      Protocol<NIC<Engine>>::Address(Ethernet::Address(), 0xEEEE);

  Communicator<Protocol<NIC<Engine>>> comm =
      Communicator<Protocol<NIC<Engine>>>(prot, addr);

  if (send) {
    int i = 0;
    while (i < NUM_MSGS) {
      Message message = Message(MSG_SIZE);
      std::cout << "Sending (" << std::dec << i << "): ";
      for (size_t i = 0; i < message.size(); i++) {
        message.data()[i] = std::byte(randint(0, 255));
        std::cout << std::hex << static_cast<int>(message.data()[i]) << " ";
      }
      std::cout << std::endl;
      if (comm.send(&message)) {
        i++;
      }
    }
  } else {
    for (int i_m = 0; i_m < NUM_MSGS; ++i_m) {
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
