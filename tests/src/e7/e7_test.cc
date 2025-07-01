#ifndef SIMULATION_TIMESTAMP
#define SIMULATION_TIMESTAMP 1748768400000000;
#endif

#include "car.hh"
#include "map.hh"
#include "shared_mem.hh"
#include "utils.hh"

#include <array>
#include <cassert>
#include <csignal>
#include <future>
#include <iomanip>
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>

#ifndef INTERFACE_NAME
#define INTERFACE_NAME "lo"
#endif

constexpr int NUM_CARS = 15;
constexpr int NUM_SEND_MESSAGES_PER_THREAD = 50;
constexpr int MESSAGE_SIZE = 12;
constexpr int64_t SIM_END = 1748772002000000;

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

  Map *map = new Map(3, 3);

  // Inicializa barreira
  pthread_barrierattr_t barrier_attr;
  pthread_barrierattr_init(&barrier_attr);
  pthread_barrierattr_setpshared(&barrier_attr, PTHREAD_PROCESS_SHARED);
  pthread_barrier_t barrier;
  pthread_barrier_init(&barrier, &barrier_attr, NUM_CARS);

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
    std::mutex stdout_mtx;

    Car car;

    pthread_barrier_wait(&barrier);

    auto future = std::async(std::launch::async, [&]() {
      auto comp = car.create_component(1);
      Message message(MESSAGE_SIZE, Control(Control::Type::COMMON), &car.prot);
      std::unique_lock<std::mutex> stdout_lock(stdout_mtx);
      stdout_lock.unlock();
      int j = 0;
      while (true) {
        memset(message.data(), 0, MESSAGE_SIZE);
        if (!comp.receive(&message)) {
          std::cerr << "Erro ao receber mensagem na thread " << 1 << std::endl;
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
          std::cout << std::dec << " Proc (" << getpid() << ") Thread (" << 1
                    << "): Received (" << j << "): ";
          j++;
          for (size_t i = 0; i < message.size(); i++) {
            std::cout << std::hex << static_cast<int>(message.data()[i]) << " ";
          }
          std::cout << std::endl << std::endl;

          stdout_lock.unlock();
        }
      }
    });

    bool timeout = false;

    if (future.wait_for(std::chrono::seconds(SIM_END)) ==
        std::future_status::timeout) {
      std::cerr << "Carro(" << getpid() << ") Fim da simulação." << std::endl;
      timeout = true;
    }
  } else {
    for (int i = 0; i < NUM_CARS; ++i) {
      wait(nullptr);
    }
    delete map;
  }

  return 0;
}
