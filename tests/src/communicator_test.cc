#include "communicator.hh"
#include "engine.hh"
#include "message.hh"
#include "nic.hh"
#include "protocol.hh"
#include "shared_engine.hh"
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
  int send;
  int parentPID = 0;
  if (argc < 2) {
    // Novo processo será o sender.
    parentPID = getpid();
    auto ret = fork();

    send = ret == 0;

  } else {
    send = atoi(argv[1]);
    sem_post(semaphore);
  }

  using Buffer = Buffer<Ethernet::Frame>;
  using SocketNIC = NIC<Engine<Buffer>>;
  using SharedMemNIC = NIC<SharedEngine<Buffer>>;
  using Protocol = Protocol<SocketNIC, SharedMemNIC>;
  using Message = Message<Protocol::Address>;
  using Communicator = Communicator<Protocol, Message>;

  SocketNIC rsnic = SocketNIC(INTERFACE_NAME);
  SharedMemNIC smnic = SharedMemNIC(INTERFACE_NAME);

  Protocol &prot = Protocol::getInstance(&rsnic, &smnic, getpid());

  Communicator comm = Communicator(&prot, 10);

  if (send) {
    sem_wait(semaphore);
    int i = 0;
    while (i < NUM_MSGS) {
      Message message =
          Message(comm.addr(),
                  Protocol::Address(rsnic.address(), parentPID, 10), MSG_SIZE);
      std::cout << "Sending (" << std::dec << i << "): ";
      for (size_t i = 0; i < message.size(); i++) {
        message.data()[i] = std::byte(randint(0, 255));
        std::cout << std::hex << static_cast<int>(message.data()[i]) << " ";
      }
      std::cout << std::endl;
      if (comm.send(&message)) {
        i++;
      }
    }
  } else {
    sem_post(semaphore);
    for (int i_m = 0; i_m < NUM_MSGS; ++i_m) {
      Message message = Message(MSG_SIZE);
      comm.receive(&message);
      std::cout << "Received (" << std::dec << i_m << "): ";
      for (size_t i = 0; i < message.size(); i++) {
        std::cout << std::hex << static_cast<int>(message.data()[i]) << " ";
      }
      std::cout << std::endl;
    }
  }

  return 0;
}
