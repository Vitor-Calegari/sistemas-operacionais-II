#include "communicator.hh"
#include "engine.hh"
#include "message.hh"
#include "nic.hh"
#include "protocol.hh"
#include "shared_engine.hh"
#include "map.hh"
#include <cstdlib>
#include <iostream>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef INTERFACE_NAME
#define INTERFACE_NAME "lo"
#endif

// Constantes globais para o teste de carga
const int num_messages = 100;
const std::size_t MESSAGE_SIZE = 256;
[[maybe_unused]]
const int timeout_sec = 5;

struct msg_struct {
  int counter = 0;
};

int main() {
  std::cout << "Main proc is " << getpid() << std::endl;
  using SocketNIC = NIC<Engine<Ethernet>>;
  using SharedMemNIC = NIC<SharedEngine<Ethernet>>;
  using Protocol = Protocol<SocketNIC, SharedMemNIC, NavigatorDirected>;
  using Message = Message<Protocol::Address>;
  using Communicator = Communicator<Protocol, Message>;
  using Coordinate = NavigatorCommon::Coordinate;

  // Inicializa mutex da variavel de condição
  pthread_mutexattr_t mutex_cond_attr;
  pthread_mutexattr_init(&mutex_cond_attr);
  pthread_mutexattr_setpshared(&mutex_cond_attr, PTHREAD_PROCESS_SHARED);
  pthread_mutex_t mutex;
  pthread_mutex_init(&mutex, &mutex_cond_attr);
  // Inicializa variavel de condição
  pthread_condattr_t cond_attr;
  pthread_condattr_init(&cond_attr);
  pthread_condattr_setpshared(&cond_attr, PTHREAD_PROCESS_SHARED);
  pthread_cond_t cond;
  pthread_cond_init(&cond, &cond_attr);

  bool *map_ready = static_cast<bool*>(
    mmap(NULL, sizeof(bool),
         PROT_READ|PROT_WRITE,
          MAP_SHARED|MAP_ANONYMOUS, -1, 0));
  if (map_ready == MAP_FAILED) { perror("mmap"); exit(1); }
  *map_ready = false;

  Map *map = new Map(1, 1);
  pthread_mutex_lock(&mutex);
  *map_ready = true;
  pthread_cond_broadcast(&cond);
  pthread_mutex_unlock(&mutex);

  // Cria um semaphore compartilhado entre processos
  sem_t *semaphore =
      static_cast<sem_t *>(mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE,
                                MAP_SHARED | MAP_ANONYMOUS, -1, 0));
  if (semaphore == MAP_FAILED) {
    std::cerr << "Erro ao criar o semaphore." << std::endl;
    exit(1);
  }
  sem_init(semaphore, 1, 0);

  // Cria um semaphore compartilhado entre processos
  sem_t *semaphore_send_to_pid =
      static_cast<sem_t *>(mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE,
                                MAP_SHARED | MAP_ANONYMOUS, -1, 0));
  if (semaphore_send_to_pid == MAP_FAILED) {
    std::cerr << "Erro ao criar o semaphore." << std::endl;
    exit(1);
  }
  sem_init(semaphore_send_to_pid, 1, 0);

  pid_t *send_to_pid = new pid_t;
  pid_t pid = fork();
  if (pid < 0) {
    std::cerr << "Erro ao criar processo" << std::endl;
    exit(1);
  }

  if (pid == 0) {
    pthread_mutex_lock(&mutex);
    while (!*map_ready) {
        pthread_cond_wait(&cond, &mutex);
    }
    pthread_mutex_unlock(&mutex);
    sem_wait(semaphore_send_to_pid);
    // Código do processo-filho
    Topology topo = map->getTopology();
    Coordinate point(0, 0);
    Protocol &prot = Protocol::getInstance(INTERFACE_NAME, getpid(), {point}, topo, 10, 0);

    Communicator communicator(&prot, 10);

    // Aguarda até que o processo pai libere o semaphore
    sem_wait(semaphore);

    Message send_msg(communicator.addr(),
                     Protocol::Address(prot.getNICPAddr(), *send_to_pid, 11),
                     MESSAGE_SIZE);
    struct msg_struct ms;
    ms.counter = 0;
    std::memcpy(send_msg.data(), &ms, sizeof(ms));

    for (int i = 0; i < num_messages; i++) {

      bool sent = false;
      // Envia
      do {
        sent = communicator.send(&send_msg);
        if (!sent) {
          std::cout << "Inspect Proc(" << std::dec << getpid()
                    << "): Error sending msg " << i << std::endl;
        }
      } while (sent == false);

      // Recebe
      Message recv_msg(MESSAGE_SIZE);
      if (!communicator.receive(&recv_msg)) {
        std::cout << "Inspect Proc(" << std::dec << getpid()
                  << "): Error sending msg " << i << std::endl;
      }
      // Verifica se contou
      msg_struct *resp = reinterpret_cast<struct msg_struct *>(recv_msg.data());
      if (resp->counter ==
          reinterpret_cast<struct msg_struct *>(send_msg.data())->counter + 1) {
        std::cout << "Inspect Proc(" << std::dec << getpid() << "): Received "
                  << resp->counter << std::endl;
        reinterpret_cast<struct msg_struct *>(send_msg.data())->counter =
            resp->counter;
      } else {
        std::cout << "Count failed." << std::endl;
        exit(1);
      }
    }
    std::cout << "Inspect ended" << std::endl;
    exit(0); // Conclui com sucesso no processo-filho
  }

  pid = fork();
  if (pid < 0) {
    std::cerr << "Erro ao criar processo" << std::endl;
    exit(1);
  }

  if (pid == 0) {
    pthread_mutex_lock(&mutex);
    while (!*map_ready) {
        pthread_cond_wait(&cond, &mutex);
    }
    pthread_mutex_unlock(&mutex);
    *send_to_pid = getpid();
    sem_post(semaphore_send_to_pid);
    Topology topo = map->getTopology();
    Coordinate point(0, 0);
    Protocol &prot = Protocol::getInstance(INTERFACE_NAME, getpid(), {point}, topo, 10, 0);
    Communicator communicator(&prot, 11);

    Message send_msg(MESSAGE_SIZE);
    struct msg_struct ms;
    ms.counter = 0;
    std::memcpy(send_msg.data(), &ms, sizeof(ms));

    sem_post(semaphore);

    int recvd = 0;
    for (int i = 0; i < num_messages; i++) {
      // Recebe
      Message recv_msg(MESSAGE_SIZE);
      if (!communicator.receive(&recv_msg)) {
        std::cout << "Counter Proc(" << std::dec << getpid()
                  << "): Error sending msg " << i << std::endl;
      }

      recvd = reinterpret_cast<struct msg_struct *>(recv_msg.data())->counter;
      std::cout << "Counter Proc(" << std::dec << getpid() << "): Adding 1 to "
                << recvd << std::endl;
      reinterpret_cast<struct msg_struct *>(recv_msg.data())->counter++;

      Message send_msg(communicator.addr(), *recv_msg.sourceAddr(),
                        MESSAGE_SIZE);
      std::memcpy(send_msg.data(), recv_msg.data(), MESSAGE_SIZE);

      bool sent = false;
      // Envia
      do {
        sent = communicator.send(&send_msg);
        if (!sent) {
          std::cout << "Counter Proc(" << std::dec << getpid()
                    << "): Error sending msg " << i << std::endl;
        }
      } while (sent == false);
    }
    std::cout << "Counter ended" << std::endl;
    exit(0);
  }

  // Processo pai aguarda todos os filhos finalizarem
  map->finalizeRSU();
  int status;
  while (wait(&status) > 0)
    ;
  delete map;
  delete send_to_pid;
  std::cout << "Teste ping pong concluído" << std::endl;

  // Libera os recursos do semaphore
  sem_destroy(semaphore);
  munmap(semaphore, sizeof(sem_t));

  return 0;
}
