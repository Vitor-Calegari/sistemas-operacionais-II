#include "communicator.hh"
#include "engine.hh"
#include "message.hh"
#include "nic.hh"
#include "protocol.hh"
#include "shared_engine.hh"
#include <csignal>
#include <cstddef>
#include <iostream>
#include <random>
#include <sys/mman.h>
#include <sys/wait.h>

#define NUM_MSGS 1000
#define MSG_SIZE 5

#ifndef INTERFACE_NAME
#define INTERFACE_NAME "lo"
#endif

int randint(int p, int r) {
  std::random_device rd;
  std::mt19937 rng(rd());
  std::uniform_int_distribution<int> uni(p, r);

  return uni(rng);
}

int main(int argc, char *argv[]) {
  using Buffer = Buffer<Ethernet::Frame>;
  using SocketNIC = NIC<Engine<Buffer>>;
  using SharedMemNIC = NIC<SharedEngine<Buffer>>;
  using Protocol = Protocol<SocketNIC, SharedMemNIC>;
  using Message = Message<Protocol::Address>;
  using Communicator = Communicator<Protocol, Message>;

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
  }

  SocketNIC rsnic = SocketNIC(INTERFACE_NAME);
  SharedMemNIC smnic = SharedMemNIC(INTERFACE_NAME);

  Protocol &prot = Protocol::getInstance(&rsnic, &smnic, getpid());

  if (send) {
    std::thread inter_process_sender_thread([&]() {
      Communicator comm = Communicator(&prot, 10);
      sem_wait(semaphore);
      int i = 0;
      while (i < NUM_MSGS) {
        Message message = Message(
            comm.addr(), Protocol::Address(rsnic.address(), parentPID, 10),
            MSG_SIZE);
        std::cout << "Inter proc: Sending (" << std::dec << i << "): ";
        for (size_t j = 0; j < message.size(); j++) {
          message.data()[j] = std::byte(randint(0, 255));
          std::cout << std::hex << static_cast<int>(message.data()[j]) << " ";
        }
        std::cout << std::endl;
        if (comm.send(&message)) {
          i++;
        }
      }
    });
    std::mutex cv_mtx;
    std::condition_variable cv;

    bool receiver_ready = false;
    std::thread inner_process_sender_thread([&]() {
      Communicator comm = Communicator(&prot, 1);
      std::unique_lock<std::mutex> cv_lock(cv_mtx);
      cv.wait(cv_lock, [&receiver_ready]() { return receiver_ready; });
      int i = 0;
      while (i < NUM_MSGS) {
        Message message =
            Message(comm.addr(),
                    Protocol::Address(rsnic.address(), getpid(), 2), MSG_SIZE);
        std::cout << "Inner proc: Sending (" << std::dec << i << "): ";
        for (size_t j = 0; j < message.size(); j++) {
          message.data()[j] = std::byte(randint(0, 255));
          std::cout << std::hex << static_cast<int>(message.data()[j]) << " ";
        }
        std::cout << std::endl;
        if (comm.send(&message)) {
          i++;
        }
      }
    });
    std::thread inner_process_receiver_thread([&]() {
      Communicator comm = Communicator(&prot, 2);
      receiver_ready = true;
      cv.notify_all();
      for (int i_m = 0; i_m < NUM_MSGS; ++i_m) {
        Message message = Message(MSG_SIZE);
        comm.receive(&message);
        std::cout << "Inner proc: Received (" << std::dec << i_m << "): ";
        for (size_t i = 0; i < message.size(); i++) {
          std::cout << std::hex << static_cast<int>(message.data()[i]) << " ";
        }
        std::cout << std::endl;
      }
    });
    inner_process_sender_thread.join();
    inner_process_receiver_thread.join();
    inter_process_sender_thread.join();
  } else {
    std::thread inter_receiver_thread([&]() {
      Communicator comm = Communicator(&prot, 10);
      sem_post(semaphore);
      for (int i_m = 0; i_m < NUM_MSGS; ++i_m) {
        Message message = Message(MSG_SIZE);
        comm.receive(&message);
        std::cout << "Inter proc: Received (" << std::dec << i_m << "): ";
        for (size_t i = 0; i < message.size(); i++) {
          std::cout << std::hex << static_cast<int>(message.data()[i]) << " ";
        }
        std::cout << std::endl;
      }
    });
    inter_receiver_thread.join();
  }

  if (argc < 2 && !send) {
    int status;
    wait(&status);
  }

  sem_destroy(semaphore);

  return 0;
}
