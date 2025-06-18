#include "communicator.hh"
#include "engine.hh"
#include "map.hh"
#include "message.hh"
#include "navigator.hh"
#include "nic.hh"
#include "protocol.hh"
#include "shared_engine.hh"
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <future>
#include <iomanip>
#include <iostream>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace std;
using namespace std::chrono;

constexpr long long INITIAL_NUM_MESSAGES = 100;
constexpr long long MSG_SCALING_FACTOR = 5;
constexpr size_t MESSAGE_SIZE = 32;
constexpr int TIMEOUT_SEC = 6;

#ifndef INTERFACE_NAME
#define INTERFACE_NAME "lo"
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

  bool *has_timed_out =
      static_cast<bool *>(mmap(NULL, sizeof(bool), PROT_READ | PROT_WRITE,
                               MAP_SHARED | MAP_ANONYMOUS, -1, 0));
  *has_timed_out = false;

  using SocketNIC = NIC<Engine<Ethernet>>;
  using SharedMemNIC = NIC<SharedEngine<Ethernet>>;
  using Protocol = Protocol<SocketNIC, SharedMemNIC, NavigatorDirected>;
  using Message = Message<Protocol::Address, Protocol>;
  using Communicator = Communicator<Protocol, Message>;

  Map *map = new Map(1, 1);

  pid_t parentPID = getpid();
  pid_t pid = fork();
  if (pid < 0) {
    cerr << "Erro ao criar processo" << endl;
    exit(1);
  }

  Topology topo = map->getTopology();
  NavigatorCommon::Coordinate point(0, 0);
  Protocol &prot =
      Protocol::getInstance(INTERFACE_NAME, getpid(), { point }, topo, 10, 0);

  if (pid == 0) {
    std::thread sender_thread([&]() {
      Communicator communicator(&prot, 10);

      // Aguarda liberação do semaphore pelo pai
      sem_wait(semaphore);

      for (long long num_msgs = INITIAL_NUM_MESSAGES; !*has_timed_out;
           num_msgs *= MSG_SCALING_FACTOR) {

        std::cout << "Enviando leva de " << num_msgs
                  << " mensagens:" << std::endl;
        for (long long j = 0; j < num_msgs;) {
          Message msg =
              Message(communicator.addr(),
                      Protocol::Address(prot.getNICPAddr(), parentPID, 11),
                      MESSAGE_SIZE, Control(Control::Type::COMMON), &prot);
          memset(msg.data(), 0, MESSAGE_SIZE);
          // Registra o timestamp no envio
          auto t_send = high_resolution_clock::now();
          int64_t send_time_us =
              duration_cast<microseconds>(t_send.time_since_epoch()).count();
          memcpy(msg.data(), &send_time_us, sizeof(send_time_us));

          // Envia a mensagem e incrementa o contador caso seja bem-sucedido
          if (communicator.send(&msg)) {
            sleep(0);
            j++;
          }
        }

        sem_wait(semaphore);
      }
    });
    sender_thread.join();
    exit(0);
  } else {
    // Processo pai: recebe mensagens
    Communicator communicator(&prot, 11);

    long long total_latency_us = 0;
    int msg_count = 0;
    Message msg(MESSAGE_SIZE, Control(Control::Type::COMMON), &prot);

    bool timeout = false;
    for (long long num_msgs = INITIAL_NUM_MESSAGES; !timeout;
         num_msgs *= MSG_SCALING_FACTOR) {
      // Libera o semaphore para que o filho inicie o envio
      sem_post(semaphore);

      double max_throughput = 0;
      auto future = std::async(std::launch::async, [&]() {
        for (long long j = 0; j < num_msgs; j++) {
          memset(msg.data(), 0, MESSAGE_SIZE);
          if (!communicator.receive(&msg)) {
            cerr << "Erro ao receber mensagem no processo " << getpid() << endl;
            exit(1);
          }
          msg_count++;

          // Registra o timestamp na recepção
          auto t_recv = high_resolution_clock::now();
          int64_t t_sent_us;
          memcpy(&t_sent_us, msg.data(), sizeof(t_sent_us));

          // Calcula a latência
          auto t_recv_us =
              duration_cast<microseconds>(t_recv.time_since_epoch()).count();
          long long latency_us = t_recv_us - t_sent_us;
          total_latency_us += latency_us;
          double cur_throughput =
              1e6 * (msg_count / static_cast<double>(total_latency_us));
          max_throughput = max(max_throughput, cur_throughput);
        }
      });

      if (future.wait_for(std::chrono::seconds(TIMEOUT_SEC)) ==
          std::future_status::timeout) {
        std::cerr << "Timeout na recepção de mensagens." << std::endl;
        *has_timed_out = true;
        sem_post(semaphore);
        exit(0);
      }

      double throughput =
          1e6 * (msg_count / static_cast<double>(total_latency_us));
      cout << "Vazão média: " << fixed << setprecision(2) << throughput
           << " mensagens/s" << endl;
      cout << "Vazão máxima: " << fixed << setprecision(2) << max_throughput
           << " mensagens/s" << endl
           << endl;
    }

    map->finalizeRSU();
    // Aguarda o término do filho
    int status;
    wait(&status);
    delete map;

    // Libera recursos do semaphore compartilhado
    sem_destroy(semaphore);
    munmap(semaphore, sizeof(sem_t));
    munmap(has_timed_out, sizeof(bool));
  }

  return 0;
}
