#include "communicator.hh"
#include "engine.hh"
#include "nic.hh"
#include "protocol.hh"
#include "shared_engine.hh"
#include <cassert>
#include <csignal>
#include <cstddef>
#include <iostream>
#include <mutex>
#include <random>
#include <sys/wait.h>
#include <unistd.h>

// constexpr int NUM_THREADS = 15;
constexpr int NUM_MESSAGES_PER_THREAD = 100;
constexpr int MESSAGE_SIZE = 5;

#ifndef INTERFACE_NAME
#define INTERFACE_NAME "lo"
#endif

int randint(int p, int r) {
  std::random_device rd;
  std::mt19937 rng(rd());
  std::uniform_int_distribution<int> uni(p, r);

  return uni(rng);
}

int main() {
  using Buffer = Buffer<Ethernet::Frame>;
  using SocketNIC = NIC<Engine<Buffer>>;
  using SharedMemNIC = NIC<SharedEngine<Buffer>>;
  using Protocol = Protocol<SocketNIC, SharedMemNIC>;
  using Message = Message<Protocol::Address>;
  using Communicator = Communicator<Protocol, Message>;

  pid_t parent = fork();
  if (!parent) {
    SocketNIC rsnic(INTERFACE_NAME);
    SharedMemNIC smnic(INTERFACE_NAME);
    Protocol &prot = Protocol::getInstance(&rsnic, &smnic, getpid());

    auto send_task = [&](const int thread_id) {
      Communicator communicator(&prot, 1);
      for (int j = 0; j < NUM_MESSAGES_PER_THREAD;) {
        Message msg = Message(communicator.addr(),
                              Protocol::Address(rsnic.address(), getpid(), 2),
                              MESSAGE_SIZE);
        memset(msg.data(), 0, MESSAGE_SIZE);

        if (communicator.send(&msg)) {
          sleep(0);
          std::cout << "Thread (" << thread_id << "): Sending (" << std::dec
                    << j << "): ";
          for (size_t j = 0; j < msg.size(); j++) {
            msg.data()[j] = std::byte(randint(0, 255));
            std::cout << std::hex << static_cast<int>(msg.data()[j]) << " ";
          }
          std::cout << std::endl;
          j++;
        }
      }
    };

    auto receive_task = [&](const int thread_id) {
      Communicator communicator(&prot, 2);
      Message msg(MESSAGE_SIZE);

      for (int j = 0; j < NUM_MESSAGES_PER_THREAD; j++) {
        memset(msg.data(), 0, MESSAGE_SIZE);
        if (!communicator.receive(&msg)) {
          std::cerr << "Erro ao receber mensagem no processo " << getpid()
                    << std::endl;
          exit(1);
        } else {
          std::cout << "Thread (" << thread_id << "): Received (" << std::dec
                    << j << "): ";
          for (size_t i = 0; i < msg.size(); i++) {
            std::cout << std::hex << static_cast<int>(msg.data()[i]) << " ";
          }
          std::cout << std::endl;
        }
      }
    };

    std::thread sender_thread(send_task, 0);
    std::thread receiver_thread(receive_task, 1);

    sender_thread.join();
    receiver_thread.join();
    exit(0);
  }

  int status;
  wait(&status);

  return 0;
}
