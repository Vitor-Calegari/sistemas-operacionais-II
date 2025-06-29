#include "communicator.hh"
#include "engine.hh"
#include "map.hh"
#include "message.hh"
#include "navigator.hh"
#include "nic.hh"
#include "protocol.hh"
#include "shared_engine.hh"
#include "shared_mem.hh"
#include "utils.hh"
#include <array>
#include <cassert>
#include <csignal>
#include <cstddef>
#include <iostream>
#include <semaphore>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

constexpr int NUM_SEND_THREADS = 5;
constexpr int NUM_RECV_THREADS = 5;
constexpr int NUM_MESSAGES_PER_THREAD = 5;
constexpr int MESSAGE_SIZE = 5;

#ifndef INTERFACE_NAME
#define INTERFACE_NAME "lo"
#endif

int main() {
  using SocketNIC = NIC<Engine<Ethernet>>;
  using SharedMemNIC = NIC<SharedEngine<SharedMem>>;
  using Protocol = Protocol<SocketNIC, SharedMemNIC, NavigatorDirected>;
  using Message = Message<Protocol::Address, Protocol>;
  using Communicator = Communicator<Protocol, Message>;

  Map *map = new Map(1, 1);

  std::counting_semaphore<NUM_RECV_THREADS> *sem_receivers =
      static_cast<std::counting_semaphore<NUM_RECV_THREADS> *>(
          mmap(NULL, sizeof(std::counting_semaphore<NUM_RECV_THREADS>),
               PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0));

  std::binary_semaphore *stdout_mtx = static_cast<std::binary_semaphore *>(
      mmap(NULL, sizeof(std::binary_semaphore), PROT_READ | PROT_WRITE,
           MAP_SHARED | MAP_ANONYMOUS, -1, 0));
  stdout_mtx->release();

  for (int i = 0; i < NUM_RECV_THREADS; ++i) {
    pid_t pid = fork();
    if (pid < 0) {
      std::cerr << "Erro ao criar processo" << std::endl;
      exit(1);
    }
    if (pid == 0) {
      // Código do processo-filho
      Topology topo = map->getTopology();
      NavigatorCommon::Coordinate point(0, 0);
      Protocol &prot2 = Protocol::getInstance(INTERFACE_NAME, getpid(),
                                              { point }, topo, 10, 0);

      Communicator communicator(&prot2, i);

      sem_receivers->release();

      for (int j = 0; j < NUM_SEND_THREADS * NUM_MESSAGES_PER_THREAD; j++) {
        Message msg(MESSAGE_SIZE, Control(Control::Type::COMMON), &prot2);
        memset(msg.data(), 0, MESSAGE_SIZE);
        if (!communicator.receive(&msg)) {
          std::cerr << "Erro ao receber mensagem no processo " << getpid()
                    << std::endl;
          exit(1);
        } else {
          stdout_mtx->acquire();
          std::cout << std::dec << "Processo (" << getpid() << "): Received ("
                    << j << "): ";
          for (size_t i = 0; i < msg.size(); i++) {
            std::cout << std::hex << static_cast<int>(msg.data()[i]) << " ";
          }
          std::cout << std::endl << std::flush;
          stdout_mtx->release();
        }
      }
      exit(0); // Conclui com sucesso no processo-filho
    }
  }

  Topology topo = map->getTopology();
  NavigatorCommon::Coordinate point(0, 0);
  Protocol &prot =
      Protocol::getInstance(INTERFACE_NAME, getpid(), { point }, topo, 10, 0);
  auto send_task = [&](const int thread_id) {
    Communicator communicator(&prot, thread_id);

    for (int j = 0; j < NUM_MESSAGES_PER_THREAD;) {
      Message msg =
          Message(communicator.addr(),
                  Protocol::Address(prot.getNICPAddr(), Protocol::BROADCAST_SID,
                                    Protocol::BROADCAST),
                  MESSAGE_SIZE, Control(Control::Type::COMMON), &prot);
      memset(msg.data(), 0, MESSAGE_SIZE);

      for (size_t j = 0; j < msg.size(); j++) {
        msg.data()[j] = std::byte(randint(0, 255));
      }

      if (communicator.send(&msg)) {
        stdout_mtx->acquire();
        std::cout << std::dec << "Thread (" << thread_id << "): Sending (" << j
                  << "): ";
        for (size_t j = 0; j < msg.size(); ++j) {
          std::cout << std::hex << static_cast<int>(msg.data()[j]) << " ";
        }
        std::cout << std::endl << std::flush;
        stdout_mtx->release();

        j++;
      }
    }
  };

  for (int i = 0; i < NUM_RECV_THREADS; ++i) {
    sem_receivers->acquire();
  }

  std::array<std::thread, NUM_SEND_THREADS> send_threads;
  for (int i = 0; i < NUM_SEND_THREADS; ++i) {
    send_threads[i] = std::thread(send_task, i);
  }

  int status;
  for (int i = 0; i < NUM_RECV_THREADS; ++i)
    wait(&status);

  for (int i = 0; i < NUM_SEND_THREADS; ++i) {
    send_threads[i].join();
  }

  munmap(sem_receivers, sizeof *sem_receivers);
  munmap(stdout_mtx, sizeof *stdout_mtx);

  std::cout << "Broadcast test finished!" << std::endl;

  delete map;
}
