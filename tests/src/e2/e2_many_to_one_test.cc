#include "communicator.hh"
#include "engine.hh"
#include "map.hh"
#include "message.hh"
#include "navigator.hh"
#include "nic.hh"
#include "protocol.hh"
#include "shared_engine.hh"
#include "utils.hh"
#include <array>
#include <cassert>
#include <csignal>
#include <cstddef>
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>

constexpr int NUM_THREADS = 7;
constexpr int NUM_MESSAGES_PER_THREAD = 35;
constexpr int MESSAGE_SIZE = 5;

#ifndef INTERFACE_NAME
#define INTERFACE_NAME "lo"
#endif

int main() {
  using SocketNIC = NIC<Engine<Ethernet>>;
  using SharedMemNIC = NIC<SharedEngine<Ethernet>>;
  using Protocol = Protocol<SocketNIC, SharedMemNIC, NavigatorDirected>;
  using Message = Message<Protocol::Address>;
  using Communicator = Communicator<Protocol, Message>;

  Map *map = new Map(1, 1);

  Topology topo = map->getTopology();
  NavigatorCommon::Coordinate point(0, 0);
  Protocol &prot =
      Protocol::getInstance(INTERFACE_NAME, getpid(), { point }, topo, 10, 0);

  std::mutex stdout_mtx;

  std::mutex cv_mtx;
  std::condition_variable cv;

  bool receiver_ready = false;

  auto send_task = [&](const int thread_id) {
    Communicator communicator(&prot, thread_id);
    std::unique_lock<std::mutex> stdout_lock(stdout_mtx);
    stdout_lock.unlock();

    std::unique_lock<std::mutex> cv_lock(cv_mtx);
    cv.wait(cv_lock, [&receiver_ready]() { return receiver_ready; });

    for (int j = 1; j <= NUM_MESSAGES_PER_THREAD;) {
      Message msg =
          Message(communicator.addr(),
                  Protocol::Address(prot.getNICPAddr(), getpid(), NUM_THREADS),
                  MESSAGE_SIZE);
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
        std::cout << std::endl;
        stdout_lock.unlock();
        j++;
      }
    }
  };

  auto receive_task = [&](const int thread_id) {
    Communicator communicator(&prot, thread_id);
    std::unique_lock<std::mutex> stdout_lock(stdout_mtx);
    stdout_lock.unlock();

    receiver_ready = true;
    cv.notify_all();

    Message msg(MESSAGE_SIZE);

    for (int j = 1; j <= NUM_THREADS * NUM_MESSAGES_PER_THREAD; j++) {
      memset(msg.data(), 0, MESSAGE_SIZE);
      if (!communicator.receive(&msg)) {
        std::cerr << "Erro ao receber mensagem na thread " << thread_id
                  << std::endl;
        exit(1);
      } else {
        stdout_lock.lock();
        std::cout << std::dec << "Thread (" << thread_id << "): Received (" << j
                  << "): ";
        for (size_t i = 0; i < msg.size(); i++) {
          std::cout << std::hex << static_cast<int>(msg.data()[i]) << " ";
        }
        std::cout << std::endl;
        stdout_lock.unlock();
      }
    }
  };

  std::array<std::thread, NUM_THREADS> send_threads;
  std::thread receive_thread(receive_task, NUM_THREADS);
  for (int i = 0; i < NUM_THREADS; ++i) {
    send_threads[i] = std::thread(send_task, i);
  }

  for (int i = 0; i < NUM_THREADS; ++i) {
    send_threads[i].join();
  }
  receive_thread.join();

  map->finalizeRSU();
  delete map;

  return 0;
}
