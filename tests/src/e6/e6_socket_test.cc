#include "car.hh"
#include "map.hh"
#include "shared_mem.hh"
#include "utils.hh"

#include <array>
#include <cassert>
#include <csignal>
#include <iomanip>
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>

#ifndef INTERFACE_NAME
#define INTERFACE_NAME "lo"
#endif

constexpr int NUM_CARS = 2;
constexpr int NUM_COMPONENTS = 1;
constexpr int NUM_MESSAGES_PER_THREAD = 5;
constexpr int NUM_SEND_MESSAGES_PER_THREAD = 50;
constexpr int MESSAGE_SIZE = 12;

std::string formatTimestamp(uint64_t timestamp_us) {
  auto time_point = std::chrono::system_clock::time_point(
      std::chrono::microseconds(timestamp_us));
  std::time_t time_t = std::chrono::system_clock::to_time_t(time_point);
  std::tm *tm = std::localtime(&time_t);

  std::ostringstream oss;
  oss << std::put_time(tm, "%H:%M:%S") << "." << std::setw(6)
      << std::setfill('0') << (timestamp_us % 1000000) << " us";
  return oss.str();
}

int main() {
  using SocketNIC = NIC<Engine<Ethernet>>;
  using SharedMemNIC = NIC<SharedEngine<SharedMem>>;
  using Protocol = Protocol<SocketNIC, SharedMemNIC, NavigatorDirected>;
  using Message = Message<Protocol::Address, Protocol>;

  constexpr auto print_addr = [](const Protocol::Address &addr) {
    for (auto k : addr.getPAddr().mac) {
      std::cout << int(k) << ' ';
    }
    std::cout << ": " << addr.getSysID() << " : " << addr.getPort();
  };

  Map *map = new Map(1, 1);

  auto parent_pid = getpid();
  bool sender = false;

  for (auto i = 0; i < NUM_CARS; ++i) {
    auto cur_pid = fork();
    if (cur_pid == 0) {
      if (i == 0) {
        sender = true;
      }
      break;
    }
  }

  if (getpid() != parent_pid) {
    std::this_thread::sleep_for(std::chrono::milliseconds(randint(350, 2000)));

    std::mutex stdout_mtx;

    std::mutex cv_mtx;
    std::condition_variable cv;

    Car car;

    auto send_task = [&](const int thread_id) {
      std::this_thread::sleep_for(std::chrono::milliseconds(2000));
      auto comp = car.create_component(thread_id);
      std::unique_lock<std::mutex> stdout_lock(stdout_mtx);
      stdout_lock.unlock();

      for (int j = 1; j <= NUM_SEND_MESSAGES_PER_THREAD;) {
        Message msg = Message(
            comp.addr(),
            Protocol::Address(car.prot.getNICPAddr(), Protocol::BROADCAST_SID,
                              Protocol::BROADCAST),
            MESSAGE_SIZE, Control(Control::Type::COMMON), &car.prot);
        for (size_t j = 0; j < msg.size(); j++) {
          msg.data()[j] = std::byte(randint(0, 255));
        }

        if (comp.send(&msg)) {
          stdout_lock.lock();
          std::cout << std::dec << " Proc (" << getpid() << ") Thread ("
                    << thread_id << "): Sending (" << j << "): ";
          std::cout.flush();
          for (size_t j = 0; j < msg.size(); ++j) {
            std::cout << std::hex << static_cast<int>(msg.data()[j]) << " ";
            std::cout.flush();
          }
          std::cout << std::endl << std::endl;
          stdout_lock.unlock();
          j++;
        }
      }
    };

    auto receive_task = [&](const int thread_id) {
      auto comp = car.create_component(thread_id);
      Message message(MESSAGE_SIZE, Control(Control::Type::COMMON), &car.prot);
      std::unique_lock<std::mutex> stdout_lock(stdout_mtx);
      stdout_lock.unlock();

      for (int j = 1; j <= NUM_MESSAGES_PER_THREAD; j++) {
        memset(message.data(), 0, MESSAGE_SIZE);
        if (!comp.receive(&message)) {
          std::cerr << "Erro ao receber mensagem na thread " << thread_id
                    << std::endl;
          exit(1);
        } else {
          stdout_lock.lock();

          std::cout << std::dec << "[Msg sent at "
                    << formatTimestamp(*message.timestamp()) << " from ("
                    << *message.getCoordX() << ", " << *message.getCoordY()
                    << ") - ";
          print_addr(*message.sourceAddr());
          std::cout << " to ";
          print_addr(*message.destAddr());
          std::cout << "]" << std::endl;
          std::cout << std::dec << " Proc (" << getpid() << ") Thread ("
                    << thread_id << "): Received (" << j << "): ";
          for (size_t i = 0; i < message.size(); i++) {
            std::cout << std::hex << static_cast<int>(message.data()[i]) << " ";
          }
          std::cout << std::endl << std::endl;

          stdout_lock.unlock();
        }
      }
    };

    std::array<std::thread, NUM_COMPONENTS> send_threads;
    std::array<std::thread, NUM_COMPONENTS> receive_threads;
    for (int i = 0; i < NUM_COMPONENTS; ++i) {
      if (sender) {
        send_threads[i] = std::thread(send_task, i);
      } else {
        receive_threads[i] = std::thread(receive_task, i + NUM_COMPONENTS);
      }
    }

    for (int i = 0; i < NUM_COMPONENTS; ++i) {
      if (sender) {
        send_threads[i].join();
      } else {
        receive_threads[i].join();
      }
    }
  } else {
    for (int i = 0; i < NUM_CARS; ++i) {
      wait(nullptr);
    }
    delete map;
  }

  return 0;
}
