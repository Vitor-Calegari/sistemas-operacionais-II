#include "communicator.hh"
#include "engine.hh"
#include "nic.hh"
#include "protocol.hh"
#include "shared_engine.hh"
#include <array>
#include <cassert>
#include <csignal>
#include <cstddef>
#include <iostream>
#include <mutex>
#include <random>
#include <sys/wait.h>
#include <unistd.h>
#include <condition_variable>

constexpr int NUM_SEND_THREADS = 1;
constexpr int NUM_RECV_THREADS = 10;
constexpr int NUM_MESSAGES_PER_THREAD = 1;
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



  std::mutex mtx;
  std::condition_variable cv;

  int comm_waiting = 0;

  SocketNIC rsnic(INTERFACE_NAME);
  SharedMemNIC smnic(INTERFACE_NAME);
  Protocol &prot = Protocol::getInstance(&rsnic, &smnic, getpid());

  auto send_task = [&](const int thread_id) {
    Communicator communicator(&prot, thread_id);
    std::unique_lock<std::mutex> lock(mtx);
    cv.wait(lock, [&comm_waiting]() {
        return comm_waiting == NUM_RECV_THREADS;
    });
    for (int j = 0; j < NUM_MESSAGES_PER_THREAD;) {
      Message msg = Message(
          communicator.addr(),
          Protocol::Address(rsnic.address(), getpid(), Protocol::BROADCAST),
          MESSAGE_SIZE);
      memset(msg.data(), 0, MESSAGE_SIZE);

      for (size_t j = 0; j < msg.size(); j++) {
        msg.data()[j] = std::byte(randint(0, 255));
      }

      if (communicator.send(&msg)) {
        std::cout << "Thread (" << thread_id << "): Sending (" << std::dec << j
                  << "): ";
        std::cout.flush();
        for (size_t j = 0; j < msg.size(); ++j) {
          std::cout << std::hex << static_cast<int>(msg.data()[j]) << " ";
          std::cout.flush();
        }
        std::cout << std::endl << std::flush;
        sleep(0);
        j++;
      }
    }
  };

  auto receive_task = [&](const int thread_id) {
    Communicator communicator(&prot, thread_id);
    Message msg(MESSAGE_SIZE);

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
        std::cout << "Thread (" << thread_id << "): Received (" << std::dec << j
                  << "): ";
        std::cout.flush();

        for (size_t i = 0; i < msg.size(); i++) {
          std::cout << std::hex << static_cast<int>(msg.data()[i]) << " ";
        }
        std::cout << std::endl << std::flush;
        std::cout.flush();

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

  return 0;
}
