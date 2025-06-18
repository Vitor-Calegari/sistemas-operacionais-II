#include "car.hh"
#include "communicator.hh"
#include "cond.hh"
#include "engine.hh"
#include "message.hh"
#include "nic.hh"
#include "protocol.hh"
#include "shared_engine.hh"
#include "smart_data.hh"
#include "smart_unit.hh"
#include "transducer.hh"
#include "map.hh"
#include <csignal>
#include <cstddef>
#include <iostream>
#include <sys/mman.h>
#include <sys/wait.h>

constexpr int NUM_MESSAGES = 10;
constexpr int PERIOD_SUBCRIBER = 5e3;

#ifndef INTERFACE_NAME
#define INTERFACE_NAME "lo"
#endif

int main(int argc, char *argv[]) {
  sem_t *semaphore =
      static_cast<sem_t *>(mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE,
                                MAP_SHARED | MAP_ANONYMOUS, -1, 0));
  sem_init(semaphore, 1, 0); // Inicialmente bloqueado

  Map *map = new Map(1, 1);

  bool publisher;
  if (argc < 2) {
    // Novo processo será o publisher.
    auto ret = fork();

    publisher = ret == 0;
  } else {
    publisher = atoi(argv[1]);
    sem_post(semaphore);
  }

  constexpr SmartUnit Farad(
      (SmartUnit::SIUnit::KG ^ -1) * (SmartUnit::SIUnit::M ^ -2) *
      (SmartUnit::SIUnit::S ^ 4) * (SmartUnit::SIUnit::A ^ 2));

  using SocketNIC = NIC<Engine<Ethernet>>;
  using SharedMemNIC = NIC<SharedEngine<Ethernet>>;
  using Protocol = Protocol<SocketNIC, SharedMemNIC, NavigatorDirected>;
  using Message = Message<Protocol::Address, Protocol>;
  using Communicator = Communicator<Protocol, Message>;

  Car car = Car();

  if (publisher) {
    Transducer<Farad> transducer(0, 255);

    auto comp = car.create_component(10);
    auto smart_data = comp.register_publisher(
        &transducer, Condition(true, Farad.get_int_unit()));

    sem_post(semaphore);

    std::this_thread::sleep_for(std::chrono::seconds(1));
    // Espera o subscriber ler NUM_MESSAGES mensagens.
    sem_wait(semaphore);

    std::cout << "Terminou (publisher)" << std::endl;
  } else {
    sem_wait(semaphore);

    auto comp = car.create_component(10);
    auto smart_data = comp.subscribe(
        Condition(false, Farad.get_int_unit(), PERIOD_SUBCRIBER));

    for (int i_m = 0; i_m < NUM_MESSAGES; ++i_m) {
      Message message =
          Message(sizeof(SmartData<Communicator, Condition>::Header) +
                      Farad.get_value_size_bytes(), Control(Control::Type::COMMON), &car.prot);
        message.getControl()->setType(Control::Type::PUBLISH);
      smart_data.receive(&message);
      std::cout << "Received (" << std::dec << i_m << "): ";
      for (size_t i = 0; i < message.size(); i++) {
        std::cout << std::hex << static_cast<int>(message.data()[i]) << " ";
      }
      std::cout << std::endl;
    }

    sem_post(semaphore);
    std::cout << "Terminou (subscriber)" << std::endl;
  }
  delete map;
  return 0;
}
