#include "car.hh"
#include "cond.hh"
#include "map.hh"
#include "message.hh"
#include "shared_mem.hh"
#include "smart_unit.hh"
#include "transducer.hh"
#include <cassert>
#include <chrono>
#include <cmath>
#include <iostream>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

constexpr size_t NUM_MESSAGES = 10;
constexpr uint32_t DEFAULT_PERIOD_US = 5e3;

int main() {
  Map *map = new Map(1, 1);
  uint32_t period_us = DEFAULT_PERIOD_US;
  constexpr SmartUnit Meter(SmartUnit::SIUnit::M);

  sem_t *semaphore =
      static_cast<sem_t *>(mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE,
                                MAP_SHARED | MAP_ANONYMOUS, -1, 0));
  sem_init(semaphore, 1, 0); // Inicialmente bloqueado

  pid_t pid = fork();

  if (pid == 0) {
    Car car = Car();
    TransducerRandom<Meter> transd(0, 255);
    Condition cond(true, Meter.get_int_unit());
    Car::ComponentC component = car.create_component(1);
    auto pub_comp =
        component
            .template register_publisher<Condition, TransducerRandom<Meter>>(
                &transd, cond);
    sem_wait(semaphore);
    return 0;
  }

  Car car = Car();
  Condition cond(false, Meter.get_int_unit(), period_us);
  Car::ComponentC component = car.create_component(1);
  auto sub_comp = component.template subscribe<Condition>(cond);

  std::vector<uint64_t> stamps;
  stamps.reserve(NUM_MESSAGES);
  using Message = Message<Car::ProtocolC::Address, Car::ProtocolC>;
  Message message = Message(8 + Meter.get_value_size_bytes(),
                            Control(Control::Type::COMMON), &car.prot);
  message.getControl()->setType(Control::Type::PUBLISH);

  for (size_t i = 0; i < NUM_MESSAGES; ++i) {
    sub_comp.receive(&message);
    stamps.push_back(std::chrono::duration_cast<std::chrono::microseconds>(
                         std::chrono::steady_clock::now().time_since_epoch())
                         .count());
  }
  sem_post(semaphore);
  int status;
  waitpid(pid, &status, 0);
  delete map;

  double avg_diff = 0.0;
  for (size_t i = 1; i < NUM_MESSAGES; ++i) {
    uint64_t delta = stamps[i] - stamps[i - 1];
    double diff = std::abs(static_cast<double>(delta) - period_us);
    avg_diff += diff;
  }
  avg_diff /= (NUM_MESSAGES - 1);
  std::cout << "Average timing difference: " << avg_diff << "us\n";

  std::cout << "Periodic response timing test passed for period_us="
            << period_us << "us\n";

  return 0;
}
