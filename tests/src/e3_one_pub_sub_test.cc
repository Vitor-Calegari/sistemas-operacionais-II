#include "communicator.hh"
#include "engine.hh"
#include "message.hh"
#include "nic.hh"
#include "protocol.hh"
#include "shared_engine.hh"
#include "smart_unit.hh"
#include "transducer.hh"
#include <csignal>
#include <cstddef>
#include <iostream>
#include <sys/mman.h>
#include <sys/wait.h>

constexpr int NUM_MESSAGES = 5;
constexpr int PERIOD_SIZE_BYTES = 4;

#ifndef INTERFACE_NAME
#define INTERFACE_NAME "lo"
#endif

int main(int argc, char *argv[]) {
  sem_t *semaphore =
      static_cast<sem_t *>(mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE,
                                MAP_SHARED | MAP_ANONYMOUS, -1, 0));
  sem_init(semaphore, 1, 0); // Inicialmente bloqueado

  bool publisher;
  if (argc < 2) {
    // Novo processo será o publisher.
    auto ret = fork();

    publisher = ret == 0;
  } else {
    publisher = atoi(argv[1]);
    sem_post(semaphore);
  }

  constexpr SmartUnit Meter(SmartUnit::SIUnit::M);

  using Buffer = Buffer<Ethernet::Frame>;
  using SocketNIC = NIC<Engine<Buffer>>;
  using SharedMemNIC = NIC<SharedEngine<Buffer>>;
  using Protocol = Protocol<SocketNIC, SharedMemNIC>;
  using Message = Message<Protocol::Address, SmartUnit>;
  using CommunicatorSub = Communicator<Protocol, Message>;
  using CommunicatorPub = Communicator<Protocol, Message, Transducer<Meter>>;

  SocketNIC rsnic = SocketNIC(INTERFACE_NAME);
  SharedMemNIC smnic = SharedMemNIC(INTERFACE_NAME);

  Protocol &prot = Protocol::getInstance(&rsnic, &smnic, getpid());

  if (publisher) {
    Transducer<Meter> transducer(0, 300000);

    CommunicatorPub comm(&prot, 10, &transducer);
    comm.initPeriocT();

    sem_post(semaphore);

    std::this_thread::sleep_for(std::chrono::seconds(1));
    // Espera o subscriber ler NUM_MESSAGES mensagens.
    sem_wait(semaphore);

    std::cout << "Terminou (publisher)" << std::endl;
  } else {
    sem_wait(semaphore);

    CommunicatorSub comm(&prot, 10);
    Message message(comm.addr(),
                    Protocol::Address(rsnic.address(), Protocol::BROADCAST_SID,
                                      Protocol::BROADCAST),
                    PERIOD_SIZE_BYTES, false, Meter);

    int period = 2e6;
    std::memcpy(message.data(), &period, sizeof(period));
    comm.send(&message);

    for (int i_m = 0; i_m < NUM_MESSAGES; ++i_m) {
      Message message(PERIOD_SIZE_BYTES, false,
                      SmartUnit(SmartUnit::SIUnit::M));

      comm.receive(&message);
      std::cout << "Received: ";
      for (size_t i = 0; i < message.size(); i++) {
        std::cout << std::dec << static_cast<int>(message.data()[i]) << " ";
      }
      std::cout << std::endl;
    }

    sem_post(semaphore);
    std::cout << "Terminou (subscriber)" << std::endl;
  }

  return 0;
}
