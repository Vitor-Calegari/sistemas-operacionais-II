#define DEBUG_TIMESTAMP
#include "car.hh"
#undef DEBUG_TIMESTAMP

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
#include "rsu_protocol.hh"
#include "mac_structs.hh"
#include <csignal>
#include <cstddef>
#include <iostream>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>

constexpr int NUM_MESSAGES = 2;
constexpr int PERIOD_SUBCRIBER = 5e6;

#ifndef INTERFACE_NAME
#define INTERFACE_NAME "lo"
#endif

const char* SHM_NAME = "/barrier_shm";
const int NUM_PROCESSES = 1;  // Apenas uma RSU nesse teste

int main() {
  using Buffer = Buffer<Ethernet::Frame>;
  using SocketNIC = NIC<Engine<Buffer>>;
  using SharedMemNIC = NIC<SharedEngine<Buffer>>;
  using Protocol = Protocol<SocketNIC, SharedMemNIC>;
  using RSU = RSUProtocol<SocketNIC, SharedMemNIC>;
  using Message = Message<Protocol::Address>;
  using Communicator = Communicator<Protocol, Message>;

  sem_t *pub_ready =
      static_cast<sem_t *>(mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE,
                                MAP_SHARED | MAP_ANONYMOUS, -1, 0));
  sem_init(pub_ready, 1, 0); // Inicialmente bloqueado

  sem_t *sub_ready =
      static_cast<sem_t *>(mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE,
                                MAP_SHARED | MAP_ANONYMOUS, -1, 0));
  sem_init(sub_ready, 1, 0); // Inicialmente bloqueado

  // Cria e configura a memória compartilhada
  int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
  ftruncate(shm_fd, sizeof(SharedData));
  SharedData* shared_data = (SharedData*) mmap(NULL, sizeof(SharedData), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);

  // Inicializa mutex 
  pthread_mutexattr_t mutex_attr;
  pthread_mutexattr_init(&mutex_attr);
  pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);
  pthread_mutex_init(&shared_data->mutex, &mutex_attr);

  // Inicializa barreira
  pthread_barrierattr_t barrier_attr;
  pthread_barrierattr_init(&barrier_attr);
  pthread_barrierattr_setpshared(&barrier_attr, PTHREAD_PROCESS_SHARED);
  pthread_barrier_init(&shared_data->barrier, &barrier_attr, NUM_PROCESSES);

  // Inicializa variaveis de controle para RSU
  shared_data->counter = 0;
  shared_data->entries_size_x = 1;
  shared_data->entries_size_y = 1;
  shared_data->choosen_rsu = getpid();

  RSU &rsu_p = RSU::getInstance(INTERFACE_NAME, getpid(), shared_data, std::make_pair(0,0), 0);

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
      if (subscriber) break;
    }
  }

  Car car = Car();

  if (publisher) {
    Transducer<Farad> transducer(0, 255);

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
                      Farad.get_value_size_bytes());
        message.getControl()->setType(Control::Type::PUBLISH);
      smart_data.receive(&message);
      std::cout << "Received (" << std::dec << i_m << "): ";
      for (size_t i = 0; i < message.size(); i++) {
        std::cout << std::hex << static_cast<int>(message.data()[i]) << std::dec << " ";
      }
      std::cout << std::endl;
    }

    sem_post(sub_ready);
    std::cout << "Terminou (subscriber)" << std::endl;
  }

  int status;
  for (int i = 0; i < 2; i++) {
    wait(&status);
  }
  
  pthread_mutex_destroy(&shared_data->mutex);
  pthread_barrier_destroy(&shared_data->barrier);
  munmap(shared_data, sizeof(SharedData));
  munmap(pub_ready, sizeof(sem_t));
  munmap(sub_ready, sizeof(sem_t));
  shm_unlink(SHM_NAME);
  sem_destroy(pub_ready);
  sem_destroy(sub_ready);

  return 0;
}
