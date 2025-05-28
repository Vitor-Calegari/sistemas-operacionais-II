#define DEBUG_SYNC
#include "car.hh"
#undef DEBUG_SYNC

#include <cassert>
#include <csignal>
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>

#ifndef INTERFACE_NAME
#define INTERFACE_NAME "lo"
#endif

constexpr int NUM_CARS = 4;
constexpr int CYCLE_DURATION_SECONDS = 4;

// Nesse teste, a cada ciclo o líder terminará a sua execução até que sobre um
// carro (processo).
int main() {
  auto parent_pid = getpid();

  for (auto i = 0; i < NUM_CARS; ++i) {
    auto cur_pid = fork();
    if (cur_pid == 0) {
      break;
    }
  }

  if (getpid() != parent_pid) {
    Car car;

    // Período inicial.
    std::this_thread::sleep_for(std::chrono::seconds(CYCLE_DURATION_SECONDS));

    for (int i = 0; i < 3; ++i) {
      // Se for o líder termina a execução.
      if (car.prot.amILeader()) {
        std::cout << std::endl
                  << "[TEST] " << get_timestamp() << " Leader " << getpid()
                  << " will leave." << std::endl
                  << std::endl;
        return 0;
      }

      std::this_thread::sleep_for(std::chrono::seconds(CYCLE_DURATION_SECONDS));
    }
  } else {
    for (int i = 0; i < NUM_CARS; ++i) {
      wait(nullptr);
    }
  }

  return 0;
}
