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
#include <condition_variable>
#include <csignal>
#include <cstddef>
#include <iostream>
#include <mutex>
#include <sys/wait.h>
#include <unistd.h>

constexpr int NUM_SEND_THREADS = 1;
constexpr int NUM_RECV_THREADS = 10;
constexpr int NUM_MESSAGES_PER_THREAD = 1;
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

  std::mutex mtx;
  std::condition_variable cv;

  int comm_waiting = 0;

  Map *map = new Map(1, 1);

  Topology topo = map->getTopology();
  NavigatorCommon::Coordinate point(0, 0);
  Protocol &prot =
      Protocol::getInstance(INTERFACE_NAME, getpid(), { point }, topo, 10, 0);

  std::mutex stdout_mtx;

  auto send_task = [&](const int thread_id) {
    Communicator communicator(&prot, thread_id);
    std::unique_lock<std::mutex> stdout_lock(stdout_mtx);
    stdout_lock.unlock();

    std::unique_lock<std::mutex> lock(mtx);
    cv.wait(lock,
            [&comm_waiting]() { return comm_waiting == NUM_RECV_THREADS; });
    for (int j = 0; j < NUM_MESSAGES_PER_THREAD;) {
      Message msg = Message(
          communicator.addr(),
          Protocol::Address(prot.getNICPAddr(), getpid(), Protocol::BROADCAST),
          MESSAGE_SIZE, Control(Control::Type::COMMON), &prot);
      memset(msg.data(), 0, MESSAGE_SIZE);

      for (size_t j = 0; j < msg.size(); j++) {
        msg.data()[j] = std::byte(randint(0, 255));
      }

      if (communicator.send(&msg)) {
        stdout_lock.lock();
        std::cout << std::dec << "Thread (" << thread_id << "): Sending (" << j
                  << "): ";
        std::cout.flush();
        for (size_t j = 0; j < msg.size(); ++j) {
          std::cout << std::hex << static_cast<int>(msg.data()[j]) << " ";
          std::cout.flush();
        }
        std::cout << std::endl << std::flush;
        stdout_lock.unlock();
        j++;
      }
    }
  };

  auto receive_task = [&](const int thread_id) {
    Communicator communicator(&prot, thread_id);
    std::unique_lock<std::mutex> stdout_lock(stdout_mtx);
    stdout_lock.unlock();

    Message msg(MESSAGE_SIZE, Control(Control::Type::COMMON), &prot);

    {
      std::lock_guard<std::mutex> lock(mtx);
      comm_waiting++;
    }
    cv.notify_all();
    for (int j = 0; j < NUM_MESSAGES_PER_THREAD; j++) {
      memset(msg.data(), 0, MESSAGE_SIZE);
      if (!communicator.receive(&msg)) {
        std::cerr << "Erro ao receber mensagem na thread " << thread_id
                  << std::endl;
        exit(1);
      } else {
        stdout_lock.lock();
        std::cout << std::dec << "Thread (" << thread_id << "): Received (" << j
                  << "): ";
        std::cout.flush();

        for (size_t i = 0; i < msg.size(); i++) {
          std::cout << std::hex << static_cast<int>(msg.data()[i]) << " ";
        }
        std::cout << std::endl << std::flush;
        std::cout.flush();
        stdout_lock.unlock();
      }
    }
  };

  std::array<std::thread, NUM_SEND_THREADS> send_threads;
  for (int i = 0; i < NUM_SEND_THREADS; ++i) {
    send_threads[i] = std::thread(send_task, i);
  }

  std::array<std::thread, NUM_RECV_THREADS> receive_threads;
  for (int i = 0; i < NUM_RECV_THREADS; ++i) {
    receive_threads[i] = std::thread(receive_task, i + NUM_RECV_THREADS);
  }

  for (int i = 0; i < NUM_SEND_THREADS; ++i) {
    send_threads[i].join();
  }
  for (int i = 0; i < NUM_RECV_THREADS; ++i) {
    receive_threads[i].join();
  }

  std::cout << "Broadcast test finished!" << std::endl;

  delete map;

  return 0;
}
