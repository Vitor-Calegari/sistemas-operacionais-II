#include "communicator.hh"
#include "engine.hh"
#include "message.hh"
#include "nic.hh"
#include "protocol.hh"
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <future>
#include <iostream>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace std;
using namespace std::chrono;

const int num_messages_per_comm = 1000;
const size_t MESSAGE_SIZE = 256;
const int timeout_sec = 10;

#ifndef INTERFACE_NAME
#define INTERFACE_NAME "wlp0s20f3"
#endif

int main() {
  std::cout << "\n\n\n\033[3A" << std::flush;
  // Criação do semaphore compartilhado
  sem_t *semaphore =
      static_cast<sem_t *>(mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE,
                                MAP_SHARED | MAP_ANONYMOUS, -1, 0));
  if (semaphore == MAP_FAILED) {
    cerr << "Erro ao criar o semaphore." << endl;
    exit(1);
  }
  sem_init(semaphore, 1, 0); // Inicialmente bloqueado

  pid_t pid = fork();
  if (pid < 0) {
    cerr << "Erro ao criar processo" << endl;
    exit(1);
  }

  using SocketNIC = NIC<Engine>;
  using Protocol = Protocol<SocketNIC>;
  using Message = Message<Protocol::Address>;
  using Communicator = Communicator<Protocol, Message>; 

  if (pid == 0) {
    // Processo-filho: envia mensagens
    SocketNIC nic(INTERFACE_NAME);
    auto &prot = Protocol::getInstance(&nic);
    Protocol::Address addr =
        Protocol::Address(nic.address(), 10);
    Communicator communicator(&prot, addr);

    // Aguarda liberação do semaphore pelo pai
    sem_wait(semaphore);
    std::cout << "\033[1B\rSent: 0\033[K\033[1A" << std::flush;
    for (int j = 0; j < num_messages_per_comm;) {
      Message msg = Message(communicator.addr(), Protocol::Address(nic.address(), 11), MESSAGE_SIZE);
      memset(msg.data(), 0, MESSAGE_SIZE);
      // Registra o timestamp no envio
      auto t_send = high_resolution_clock::now();
      int64_t send_time_us =
          duration_cast<microseconds>(t_send.time_since_epoch()).count();
      memcpy(msg.data(), &send_time_us, sizeof(send_time_us));

      // Envia a mensagem e incrementa o contador caso seja bem-sucedido
      std::cout << "\033[1B\rSent: " << std::dec << j << "\033[K\033[1A"
                << std::flush;
      if (communicator.send(&msg)) {
        sleep(0);
        j++;
      }
    }
    exit(0);
  } else {
    // Processo pai: recebe mensagens
    SocketNIC nic(INTERFACE_NAME);
    auto &prot = Protocol::getInstance(&nic);
    Protocol::Address addr =
        Protocol::Address(nic.address(), 11);
    Communicator communicator(&prot, addr);

    // Libera o semaphore para que o filho inicie o envio

    long long total_latency_us = 0;
    int msg_count = 0;
    Message msg(MESSAGE_SIZE);

    sem_post(semaphore);
    std::cout << "\033[2B\rReceived: 0\033[K\033[2A" << std::flush;

    auto future = std::async(std::launch::async, [&]() {
      for (int j = 0; j < num_messages_per_comm; j++) {
        memset(msg.data(), 0, MESSAGE_SIZE);
        if (!communicator.receive(&msg)) {
          cerr << "Erro ao receber mensagem no processo " << getpid() << endl;
          exit(1);
        }
        // Registra o timestamp na recepção
        auto t_recv = high_resolution_clock::now();
        int64_t t_sent_us;
        memcpy(&t_sent_us, msg.data(), sizeof(t_sent_us));

        // Calcula a latência
        auto t_recv_us =
            duration_cast<microseconds>(t_recv.time_since_epoch()).count();
        long long latency_us = t_recv_us - t_sent_us;
        total_latency_us += latency_us;
        msg_count++;
        std::cout << "\033[2B\rReceived: " << std::dec << msg_count
                  << "\033[K\033[2A" << std::flush;
      }
    });

    bool timeout = false;

    if (future.wait_for(std::chrono::seconds(timeout_sec)) ==
        std::future_status::timeout) {
      std::cerr << "Timeout na recepção de mensagens." << std::endl;
      timeout = true;
    }

    // Aguarda o término do filho
    int status;
    wait(&status);

    double avg_latency_us =
        (msg_count > 0 ? static_cast<double>(total_latency_us) / msg_count : 0);
    cout << "Latência média observada: " << avg_latency_us << " μs" << endl;

    // Libera recursos do semaphore compartilhado
    sem_destroy(semaphore);
    munmap(semaphore, sizeof(sem_t));

    if (timeout) {
      exit(0);
    }
  }

  return 0;
}

// g++ -std=c++20 tests/src/api_test.cc -Iinclude -o api_test src/engine.cc
// src/ethernet.cc src/message.cc src/utils.cc sudo ./api_test
