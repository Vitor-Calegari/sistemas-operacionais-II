#include "communicator.hh"
#include "engine.hh"
#include "message.hh"
#include "nic.hh"
#include "protocol.hh"
#include "shared_engine.hh"
#include "smart_unit.hh"
#include "transducer.hh"
#include "utils.hh"
#include <csignal>
#include <cstddef>
#include <iostream>
#include <sys/mman.h>
#include <sys/wait.h>

#define NUM_MSGS 1000
#define MSG_SIZE 5

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
    Transducer<Meter> transducer(0, 10);

    CommunicatorPub comm(&prot, 10, &transducer);
    comm.initPeriocT();

    std::this_thread::sleep_for(std::chrono::seconds(1));
    sem_post(semaphore);
    std::this_thread::sleep_for(std::chrono::seconds(16));
    std::cout << "Terminou\n";
  } else {
    CommunicatorSub comm(&prot, 10);
    sem_wait(semaphore);

    Message message(comm.addr(),
                    Protocol::Address(rsnic.address(), Protocol::BROADCAST_SID,
                                      Protocol::BROADCAST),
                    MSG_SIZE, false, Meter);

    int data = 2;
    std::memcpy(message.data(), &data, sizeof(data));
    comm.send(&message);

    auto start_time = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start_time <
           std::chrono::seconds(15)) {
      Message message(MSG_SIZE, false, SmartUnit(SmartUnit::SIUnit::M));

      comm.receive(&message);
      std::cout << "Received: ";
      for (size_t i = 0; i < message.size(); i++) {
        std::cout << std::hex << static_cast<int>(message.data()[i]) << " ";
      }
      std::cout << std::endl;
    }
  }

  return 0;
}
