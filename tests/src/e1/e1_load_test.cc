#include "communicator.hh"
#include "engine.hh"
#include "map.hh"
#include "message.hh"
#include "navigator.hh"
#include "nic.hh"
#include "protocol.hh"
#include "shared_engine.hh"
#include "shared_mem.hh"
#include <cstdlib>
#include <future>
#include <iostream>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef INTERFACE_NAME
#define INTERFACE_NAME "lo"
#endif

// Constantes globais para o teste de carga
const int num_communicators = 3;
const int num_messages_per_comm = 40;
const std::size_t MESSAGE_SIZE = 256;
const int timeout_sec = 3;

int main() {
  using SocketNIC = NIC<Engine<Ethernet>>;
  using SharedMemNIC = NIC<SharedEngine<SharedMem>>;
  using Protocol = Protocol<SocketNIC, SharedMemNIC, NavigatorDirected>;
  using Message = Message<Protocol::Address, Protocol>;
  using Communicator = Communicator<Protocol, Message>;

  // Cria um semaphore compartilhado entre processos
  sem_t *semaphore =
      static_cast<sem_t *>(mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE,
                                MAP_SHARED | MAP_ANONYMOUS, -1, 0));
  if (semaphore == MAP_FAILED) {
    std::cerr << "Erro ao criar o semaphore." << std::endl;
    exit(1);
  }
  sem_init(semaphore, 1, 0);

  pid_t parentPID = getpid();

  Map *map = new Map(1, 1);
  for (int i = 0; i < num_communicators; i++) {
    pid_t pid = fork();
    if (pid < 0) {
      std::cerr << "Erro ao criar processo" << std::endl;
      exit(1);
    }
    if (pid == 0) {
      // Código do processo-filho
      Topology topo = map->getTopology();
      NavigatorCommon::Coordinate point(0, 0);
      Protocol &prot = Protocol::getInstance(INTERFACE_NAME, getpid(),
                                             { point }, topo, 10, 0);

      Communicator communicator(&prot, i);

      // Aguarda até que o processo pai libere o semaphore
      sem_wait(semaphore);

      int j = 0;
      while (j < num_messages_per_comm) {
        // Envia mensagens
        Message msg(communicator.addr(),
                    Protocol::Address(prot.getNICPAddr(), parentPID, 9999),
                    MESSAGE_SIZE, Control(Control::Type::COMMON), &prot);
        if (communicator.send(&msg)) {
          // std::cout << "Proc(" << std::dec << getpid() << "): Sent msg " << j
          //           << std::endl;
          j++;
        }
      }
      exit(0); // Conclui com sucesso no processo-filho
    }
  }

  // Processo pai - Cria seu próprio comunicador
  Topology topo = map->getTopology();
  NavigatorCommon::Coordinate point(0, 0);
  Protocol &prot =
      Protocol::getInstance(INTERFACE_NAME, getpid(), { point }, topo, 10, 0);
  Communicator communicator(&prot, 9999);
  Message msg(MESSAGE_SIZE, Control(Control::Type::COMMON), &prot);

  // Libera o semaphore para que todos os filhos possam prosseguir com os envios
  for (int i = 0; i < num_communicators; i++) {
    sem_post(semaphore);
  }

  int received_msg_count = 0;
  int total_msg = num_communicators * num_messages_per_comm;

  auto future = std::async(std::launch::async, [&]() {
    for (int j = 0; j < total_msg; j++) {
      if (!communicator.receive(&msg)) {
        std::cerr << "Erro no recebimento da mensagem no processo " << getpid()
                  << std::endl;
        exit(1);
      } else {
        received_msg_count++;
      }
    }
  });

  bool timeout = false;

  if (future.wait_for(std::chrono::seconds(timeout_sec)) ==
      std::future_status::timeout) {
    std::cerr << "Timeout na recepção de mensagens." << std::endl;
    timeout = true;
  }

  std::cout << "Mensagens enviadas: " << std::dec << total_msg << std::endl;
  std::cout << "Mensagens recebidas: " << std::dec << received_msg_count
            << std::endl;

  std::cout << "Teste de carga concluído" << std::endl;

  // Processo pai aguarda todos os filhos finalizarem
  int status;
  for (int i = 0; i < num_communicators; i++) {
    wait(&status);
  }
  delete map;

  // Libera os recursos do semaphore
  sem_destroy(semaphore);
  munmap(semaphore, sizeof(sem_t));

  if (timeout) {
    exit(0);
  }

  return 0;
}
