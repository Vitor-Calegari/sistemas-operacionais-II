#include "car.hh"

#include <array>
#include <cassert>
#include <csignal>
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>

#ifndef INTERFACE_NAME
#define INTERFACE_NAME "lo"
#endif

constexpr int NUM_CARS = 4;
constexpr int NUM_COMPONENTS = 4;
constexpr int NUM_MESSAGES_PER_THREAD = 4;
constexpr int MESSAGE_SIZE = 25;

int main() {
  using Buffer = Buffer<Ethernet::Frame>;
  using SocketNIC = NIC<Engine<Buffer>>;
  using SharedMemNIC = NIC<SharedEngine<Buffer>>;
  using Protocol = Protocol<SocketNIC, SharedMemNIC>;
  using Message = Message<Protocol::Address>;

  auto parent_pid = getpid();

  for (auto i = 0; i < NUM_CARS; ++i) {
    auto cur_pid = fork();
    if (cur_pid == 0) {
      break;
    }
  }

  if (getpid() != parent_pid) {
    std::mutex stdout_mtx;

    std::mutex cv_mtx;
    std::condition_variable cv;

    int num_receivers_ready = 0;

    Car car;

    auto send_task = [&](const int thread_id) {
      auto comp = car.create_component(thread_id);
      std::unique_lock<std::mutex> stdout_lock(stdout_mtx);
      stdout_lock.unlock();

      std::unique_lock<std::mutex> cv_lock(cv_mtx);
      cv.wait(cv_lock, [&num_receivers_ready]() {
        return num_receivers_ready == NUM_COMPONENTS;
      });

      for (int j = 1; j <= NUM_MESSAGES_PER_THREAD;) {
        Message msg =
            Message(comp.addr(),
                    Protocol::Address(car.prot.getNICPAddr(), getpid(),
                                      thread_id + NUM_COMPONENTS),
                    MESSAGE_SIZE);
        for (size_t j = 0; j < msg.size(); j++) {
          msg.data()[j] = std::byte(randint(0, 255));
        }

        if (comp.send(&msg)) {
          stdout_lock.lock();
          std::cout << std::dec << '[' << msg.timestamp() << "] Proc ("
                    << getpid() << ") Thread (" << thread_id << "): Sending ("
                    << j << "): ";
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
      auto comp = car.create_component(thread_id);
      Message msg(MESSAGE_SIZE);
      std::unique_lock<std::mutex> stdout_lock(stdout_mtx);
      stdout_lock.unlock();

      ++num_receivers_ready;
      cv.notify_all();

      for (int j = 1; j <= NUM_MESSAGES_PER_THREAD; j++) {
        memset(msg.data(), 0, MESSAGE_SIZE);
        if (!comp.receive(&msg)) {
          std::cerr << "Erro ao receber mensagem na thread " << thread_id
                    << std::endl;
          exit(1);
        } else {
          stdout_lock.lock();
          std::cout << std::dec << '[' << msg.timestamp() << "] Proc ("
                    << getpid() << ") Thread (" << thread_id << "): Received ("
                    << j << "): ";
          for (size_t i = 0; i < msg.size(); i++) {
            std::cout << std::hex << static_cast<int>(msg.data()[i]) << " ";
          }
          std::cout << std::endl;
          stdout_lock.unlock();
        }
      }
    };

    std::array<std::thread, NUM_COMPONENTS> send_threads;
    std::array<std::thread, NUM_COMPONENTS> receive_threads;
    for (int i = 0; i < NUM_COMPONENTS; ++i) {
      send_threads[i] = std::thread(send_task, i);
      receive_threads[i] = std::thread(receive_task, i + NUM_COMPONENTS);
    }

    for (int i = 0; i < NUM_COMPONENTS; ++i) {
      send_threads[i].join();
      receive_threads[i].join();
    }
  } else {
    for (int i = 0; i < NUM_CARS; ++i) {
      wait(nullptr);
    }
  }

  return 0;
}
