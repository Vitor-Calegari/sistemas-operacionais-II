#define DEBUG_SYNC
#include "car.hh"
#undef DEBUG_SYNC

#include <array>
#include <cassert>
#include <csignal>
#include <functional>
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>

#ifndef INTERFACE_NAME
#define INTERFACE_NAME "lo"
#endif

void car1() {
  Car car = Car();
  auto comp = car.create_component(10);
  sleep(2);
}

void car2() {
  Car car;
  auto comp = car.create_component(10);
  sleep(2);
}

void car3() {
  std::cout << "3" << std::endl;
}

void car4() {
  std::cout << "4" << std::endl;
}

int main() {
  // using Buffer = Buffer<Ethernet::Frame>;
  // using SocketNIC = NIC<Engine<Buffer>>;
  // using SharedMemNIC = NIC<SharedEngine<Buffer>>;
  // using Protocol = Protocol<SocketNIC, SharedMemNIC>;
  // using Message = Message<Protocol::Address>;
  // using Communicator = Communicator<Protocol, Message>;

  const std::array<std::function<void()>, 4> cars = { car1, car2, car3, car4 };

  [[maybe_unused]]
  int which_car = 0;
  // which_car += std::min(1, fork());
  // which_car += 2 * std::min(1, fork());

  Car car;
  car.create_component(12 + which_car);

  // cars[which_car]();

  std::this_thread::sleep_for(std::chrono::seconds(1));
  // Se for o líder termina a execução.
  if (car.prot.amILeader()) {
    std::cout << getpid() << " OUT" << std::endl;
    return 0;
  }
  std::this_thread::sleep_for(std::chrono::seconds(5));
  return 0;
}
