#include "rsu_protocol.hh"
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
#include "mac_structs.hh"
#include "map.hh"
#include <csignal>
#include <cstddef>
#include <iostream>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>

constexpr int NUM_MESSAGES = 20;
constexpr int PERIOD_SUBCRIBER = 5e6;
constexpr int MESSAGE_SIZE = 30;
#ifndef INTERFACE_NAME
#define INTERFACE_NAME "lo"
#endif


int main() {
  using SocketNIC = NIC<Engine<Ethernet>>;
  using SharedMemNIC = NIC<SharedEngine<Ethernet>>;
  using Protocol = Protocol<SocketNIC, SharedMemNIC, NavigatorDirected>;
  using Message = Message<Protocol::Address>;
  using Communicator = Communicator<Protocol, Message>;

  sem_t *send_ready =
      static_cast<sem_t *>(mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE,
                                MAP_SHARED | MAP_ANONYMOUS, -1, 0));
  sem_init(send_ready, 1, 0); // Inicialmente bloqueado

  sem_t *recv_ready =
      static_cast<sem_t *>(mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE,
                                MAP_SHARED | MAP_ANONYMOUS, -1, 0));
  sem_init(recv_ready, 1, 0); // Inicialmente bloqueado
  
  pid_t *pid_recv = static_cast<pid_t *>(mmap(NULL, sizeof(pid_t), PROT_READ | PROT_WRITE, 
                                MAP_SHARED | MAP_ANONYMOUS, -1, 0));
  Map *map = new Map(2, 2);

  constexpr SmartUnit Farad(
    (SmartUnit::SIUnit::KG ^ -1) * (SmartUnit::SIUnit::M ^ -2) *
    (SmartUnit::SIUnit::S ^ 4) * (SmartUnit::SIUnit::A ^ 2));

  bool publisher;
  bool subscriber;

  for (int i=0; i < 2; i++) {
    if (i == 0) {
      auto ret = fork();
      publisher = ret == 0;
      if (publisher) break;
    } else if (i == 1) {
      auto ret = fork();
      subscriber = ret == 0;
      if (subscriber) {
        break;
      } else {
        *pid_recv = ret;
      }
    }
  }

  if (subscriber || publisher) {

      
    if (publisher) {
      Car car = Car("", {{-1, 10}} ,Topology({2, 2}, 10));
      std::this_thread::sleep_for(std::chrono::milliseconds(2000));
      auto comp = car.create_component(10);
      sem_wait(recv_ready);
      Message msg =
          Message(comp._comm.addr(),
                  Protocol::Address(car.prot.getNICPAddr(), *pid_recv,
                                    10),
                  MESSAGE_SIZE);
      memset(msg.data(), 0, MESSAGE_SIZE);

      for (size_t j = 0; j < MESSAGE_SIZE; j++) {
        msg.data()[j] = std::byte(randint(0, 255));
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(2000));
      comp.send(&msg);

      for (size_t j = 0; j < NUM_MESSAGES; j++) {
          std::this_thread::sleep_for(std::chrono::milliseconds(20));
        msg.data()[j] = std::byte(randint(0, 255));
        comp.send(&msg);
      }

      std::cout << "Terminou (sender)" << std::endl;
    } else if (subscriber) {
      Car car = Car("", {{1, 10}} ,Topology({2, 2}, 10));
      auto comp = car.create_component(10);
      std::this_thread::sleep_for(std::chrono::milliseconds(2000));
      sem_post(recv_ready);

      for (int i_m = 0; i_m < NUM_MESSAGES; ++i_m) {
        Message message =
            Message(sizeof(SmartData<Communicator, Condition>::Header) +
                        Farad.get_value_size_bytes());
        comp.receive(&message);
        std::cout << "Received (" << std::dec << i_m << "): ";
        for (size_t i = 0; i < message.size(); i++) {
          std::cout << std::hex << static_cast<int>(message.data()[i]) << std::dec << " ";
        }
        std::cout << std::endl;
      }

      std::cout << "Terminou (receiver)" << std::endl;
    }
    return 0;
  }
  int status;
  for (int i = 0; i < 2; i++) {
    wait(&status);
  }
  delete map;

  munmap(send_ready, sizeof(sem_t));
  munmap(recv_ready, sizeof(sem_t));
  return 0;
}
