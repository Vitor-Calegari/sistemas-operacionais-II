#define DEBUG_TIMESTAMP
#include "car.hh"
#include "rsu_protocol.hh"

#include "communicator.hh"
#include "cond.hh"
#include "engine.hh"
#include "mac_structs.hh"
#include "map.hh"
#include "message.hh"
#include "nic.hh"
#include "protocol.hh"
#include "shared_engine.hh"
#include "shared_mem.hh"
#include "smart_data.hh"
#include "smart_unit.hh"
#include "transducer.hh"
#include <csignal>
#include <cstddef>
#include <fcntl.h>
#include <iostream>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#undef DEBUG_TIMESTAMP

constexpr int NUM_MESSAGES = 2;
constexpr int PERIOD_SUBCRIBER = 5e6;

#ifndef INTERFACE_NAME
#define INTERFACE_NAME "lo"
#endif

int main() {
  using SocketNIC = NIC<Engine<Ethernet>>;
  using SharedMemNIC = NIC<SharedEngine<SharedMem>>;
  using Protocol = Protocol<SocketNIC, SharedMemNIC, NavigatorDirected>;
  using Message = Message<Protocol::Address, Protocol>;
  using Communicator = Communicator<Protocol, Message>;

  sem_t *pub_ready =
      static_cast<sem_t *>(mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE,
                                MAP_SHARED | MAP_ANONYMOUS, -1, 0));
  sem_init(pub_ready, 1, 0); // Inicialmente bloqueado

  sem_t *sub_ready =
      static_cast<sem_t *>(mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE,
                                MAP_SHARED | MAP_ANONYMOUS, -1, 0));
  sem_init(sub_ready, 1, 0); // Inicialmente bloqueado

  Map *map = new Map(1, 1);

  constexpr SmartUnit Farad(
      (SmartUnit::SIUnit::KG ^ -1) * (SmartUnit::SIUnit::M ^ -2) *
      (SmartUnit::SIUnit::S ^ 4) * (SmartUnit::SIUnit::A ^ 2));

  bool publisher;
  bool subscriber;

  for (int i = 0; i < 2; i++) {
    if (i == 0) {
      auto ret = fork();
      publisher = ret == 0;
      if (publisher)
        break;
    } else if (i == 1) {
      auto ret = fork();
      subscriber = ret == 0;
      if (subscriber)
        break;
    }
  }

  if (subscriber || publisher) {

    Car car = Car();

    if (publisher) {
      TransducerRandom<Farad> transducer(0, 255);

      auto comp = car.create_component(10);
      auto smart_data = comp.register_publisher(
          &transducer, Condition(true, Farad.get_int_unit()));

      sem_post(pub_ready);
      sem_wait(sub_ready);

      std::cout << "Terminou (publisher)" << std::endl;
    } else if (subscriber) {
      sem_wait(pub_ready);

      auto comp = car.create_component(10);
      auto smart_data = comp.subscribe(
          Condition(false, Farad.get_int_unit(), PERIOD_SUBCRIBER));

      for (int i_m = 0; i_m < NUM_MESSAGES; ++i_m) {
        Message message =
            Message(sizeof(SmartData<Communicator, Condition>::Header) +
                        Farad.get_value_size_bytes(),
                    Control(Control::Type::COMMON), &car.prot);
        message.getControl()->setType(Control::Type::PUBLISH);
        smart_data.receive(&message);
        std::cout << "Received (" << std::dec << i_m << "): ";
        for (size_t i = 0; i < message.size(); i++) {
          std::cout << std::hex << static_cast<int>(message.data()[i])
                    << std::dec << " ";
        }
        std::cout << std::endl;
      }

      sem_post(sub_ready);
      std::cout << "Terminou (subscriber)" << std::endl;
    }
    return 0;
  }

  int status;
  for (int i = 0; i < 2; i++) {
    wait(&status);
  }
  delete map;

  munmap(pub_ready, sizeof(sem_t));
  munmap(sub_ready, sizeof(sem_t));
  return 0;
}
