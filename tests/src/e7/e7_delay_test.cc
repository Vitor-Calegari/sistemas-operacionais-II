#ifndef SIMULATION_TIMESTAMP
#define SIMULATION_TIMESTAMP 1748768400000000
#endif

#define TURN_PTP_OFF

#include "car.hh"
#include "map.hh"
#include "shared_mem.hh"
#include "utils.hh"

#include <array>
#include <cassert>
#include <csignal>
#include <future>
#include <iomanip>
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>

#ifndef INTERFACE_NAME
#define INTERFACE_NAME "lo"
#endif

constexpr int NUM_CARS = 15;
constexpr int MESSAGE_SIZE = 92;
constexpr int64_t SIM_END = 30;

int main() {
  using SocketNIC = NIC<Engine<Ethernet>>;
  using SharedMemNIC = NIC<SharedEngine<SharedMem>>;
  using Protocol = Protocol<SocketNIC, SharedMemNIC, NavigatorDirected>;
  using Message = Message<Protocol::Address, Protocol>;

  Map *map = new Map(3, 3);

  // Inicializa barreira
  pthread_barrierattr_t barrier_attr;
  pthread_barrierattr_init(&barrier_attr);
  pthread_barrierattr_setpshared(&barrier_attr, PTHREAD_PROCESS_SHARED);
  pthread_barrier_t *barrier = (pthread_barrier_t *)mmap(NULL, sizeof(pthread_barrier_t),
    PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  pthread_barrier_init(barrier, &barrier_attr, NUM_CARS);
  
  int64_t *shared_socket_deltas = (int64_t *)mmap(NULL, NUM_CARS * sizeof(int64_t),
    PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  int64_t *shared_shared_mem_deltas = (int64_t *)mmap(NULL, NUM_CARS * sizeof(int64_t),
    PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  pthread_mutex_t *shared_mutex = (pthread_mutex_t *)mmap(NULL, sizeof(pthread_mutex_t),
    PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  int64_t *max_shared = (int64_t *)mmap(NULL, sizeof(int64_t),
    PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  int64_t *min_shared = (int64_t *)mmap(NULL, sizeof(int64_t),
    PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  int64_t *max_socket = (int64_t *)mmap(NULL, sizeof(int64_t),
    PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  int64_t *min_socket = (int64_t *)mmap(NULL, sizeof(int64_t),
    PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);

  *max_shared = INT64_MIN;
  *min_shared = INT64_MAX;
  *max_socket = INT64_MIN;
  *min_socket = INT64_MAX;
  
  pthread_mutexattr_t mutex_attr;
  pthread_mutexattr_init(&mutex_attr);
  pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);
  pthread_mutex_init(shared_mutex, &mutex_attr);

  for (int i = 0; i < NUM_CARS; i++) {
    shared_socket_deltas[i] = 0;
    shared_shared_mem_deltas[i] = 0;
  }

  auto parent_pid = getpid();

  std::cout << "Inicializando teste de delay de envio" << std::endl;
  std::cout << "Tempo de simulação: " << SIM_END << " segundos" << std::endl;

  std::array<std::string, NUM_CARS> labels = {"1124", "1272", "1947", "680", "1426", "1503", "532", "757", "313", "2101", "1580", "1870", "1349", "2024", "1047"};
  std::string label = "";
  std::string dataset_id = "";
  [[maybe_unused]] GlobalTime &g = GlobalTime::getInstance();
  for (auto i = 0; i < NUM_CARS; ++i) {
    label = labels[i];
    dataset_id = std::to_string(i);
    auto cur_pid = fork();
    if (cur_pid == 0) {
      std::cout << "Veículo criado: PID = " << getpid() << std::endl;
      break;
    }
  }

  if (getpid() != parent_pid) {
    E7Car car(dataset_id, label);

    int ret = pthread_barrier_wait(barrier);
    if (ret != 0 && ret != PTHREAD_BARRIER_SERIAL_THREAD) {
        std::cerr << "Erro na barreira: " << ret << std::endl;
        exit(1);
    }

    std::atomic<bool> running = true;

    auto future = std::async(std::launch::async, [&]() {
      Message message(MESSAGE_SIZE, Control(Control::Type::COMMON), &car.prot);

      int64_t socket_delta = 0;
      int64_t shared_mem_delta = 0;
      int shared_mem_counter = 0;
      int socket_counter = 0;
      int64_t max_sock_int = INT64_MIN;
      int64_t min_sock_int = INT64_MAX;
      int64_t max_shared_int = INT64_MIN;
      int64_t min_shared_int = INT64_MAX;
      while (running) {
        memset(message.data(), 0, MESSAGE_SIZE);
        if (!car.receive(&message)) {
          std::cerr << "Erro ao receber mensagem na thread " << 1 << std::endl;
          exit(1);
        } else {
          int64_t send_at = *message.timestamp();
          int64_t recv_t = car.get_timestamp();
          if (message.sourceAddr()->getSysID() == car.prot.getSysID()) {
            shared_mem_delta += recv_t - send_at;
            if (recv_t - send_at > max_shared_int) {
              max_shared_int = recv_t - send_at;
            } else if (recv_t - send_at < min_shared_int) {
              min_shared_int = recv_t - send_at;
            }
            shared_mem_counter++;
          } else {
            socket_delta += recv_t - send_at;
            if (recv_t - send_at > max_sock_int) {
              max_sock_int = recv_t - send_at;
            } else if (recv_t - send_at < min_sock_int) {
              min_sock_int = recv_t - send_at;
            }
            socket_counter++;
          }
        }
      }
      int64_t mean_socket_delta = 0;
      if (socket_delta != 0) {
        mean_socket_delta = socket_delta / socket_counter;
      }
      int64_t mean_shared_mem_delta = 0;
      if (shared_mem_delta != 0) {
        mean_shared_mem_delta = shared_mem_delta / shared_mem_counter;
      }
      pthread_mutex_lock(shared_mutex);
      shared_socket_deltas[std::stoi(car._dataset_id)] = mean_socket_delta;
      shared_shared_mem_deltas[std::stoi(car._dataset_id)] = mean_shared_mem_delta;
      if (max_sock_int > *max_socket) {
        *max_socket = max_sock_int;
      }
      if (min_sock_int < *min_socket) {
        *min_socket = min_sock_int;
      }
      if (max_shared_int > *max_shared) {
        *max_shared = max_shared_int;
      }
      if (min_shared_int < *min_shared) {
        *min_shared = min_shared_int;
      }
      pthread_mutex_unlock(shared_mutex);
    });

    if (future.wait_for(std::chrono::seconds(SIM_END)) ==
        std::future_status::timeout) {
      running = false;
      future.wait();
    }
  } else {
    for (int i = 0; i < NUM_CARS; ++i) {
      wait(nullptr);
    }
    delete map;
    int64_t mean_socket_delay = 0;
    for (int i = 0; i < NUM_CARS; i++) {
      mean_socket_delay += shared_socket_deltas[i];
    }
    mean_socket_delay /= NUM_CARS;
    std::cout << "Média de delay de envio Socket: " << mean_socket_delay << " microseconds" << std::endl;
    int64_t mean_shared_mem_delay = 0;
    for (int i = 0; i < NUM_CARS; i++) {
      mean_shared_mem_delay += shared_shared_mem_deltas[i];
    }
    mean_shared_mem_delay /= NUM_CARS;
    std::cout << "Média de delay de envio SharedMem: " << mean_shared_mem_delay << " microseconds" << std::endl;

    std::cout << "Máximo delay de envio Socket: " << *max_socket << " microseconds" << std::endl;
    std::cout << "Mínimo delay de envio Socket: " << *min_socket << " microseconds" << std::endl;
    std::cout << "Máximo delay de envio SharedMem: " << *max_shared << " microseconds" << std::endl;
    std::cout << "Mínimo delay de envio SharedMem: " << *min_shared << " microseconds" << std::endl;
    // Cleanup shared memory
    munmap(max_shared, sizeof(int64_t));
    munmap(min_shared, sizeof(int64_t));
    munmap(max_socket, sizeof(int64_t));
    munmap(min_socket, sizeof(int64_t));
    munmap(shared_socket_deltas, NUM_CARS * sizeof(int64_t));
    munmap(shared_shared_mem_deltas, NUM_CARS * sizeof(int64_t));
    munmap(shared_mutex, sizeof(pthread_mutex_t));
    munmap(barrier, sizeof(pthread_barrier_t));
  }

  return 0;
}
