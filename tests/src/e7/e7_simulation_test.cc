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
constexpr int64_t SIM_END = 30;//1748772002000000;

// std::string formatTimestamp(uint64_t timestamp_us) {
//   auto time_point = std::chrono::system_clock::time_point(
//       std::chrono::microseconds(timestamp_us));
//   std::time_t time_t = std::chrono::system_clock::to_time_t(time_point);
//   std::tm *tm = std::localtime(&time_t);

//   std::ostringstream oss;
//   oss << std::put_time(tm, "%H:%M:%S") << "." << std::setw(6)
//       << std::setfill('0') << (timestamp_us % 1000000) << " us";
//   return oss.str();
// }

int main() {
  using SocketNIC = NIC<Engine<Ethernet>>;
  using SharedMemNIC = NIC<SharedEngine<SharedMem>>;
  using Protocol = Protocol<SocketNIC, SharedMemNIC, NavigatorDirected>;
  using Message = Message<Protocol::Address, Protocol>;

  // constexpr auto print_addr = [](const Protocol::Address &addr) {
  //   for (auto k : addr.getPAddr().mac) {
  //     std::cout << int(k) << ' ';
  //   }
  //   std::cout << ": " << addr.getSysID() << " : " << addr.getPort();
  // };

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
    // std::mutex stdout_mtx;

    E7Car car(dataset_id, label);

    int ret = pthread_barrier_wait(barrier);
    if (ret != 0 && ret != PTHREAD_BARRIER_SERIAL_THREAD) {
        std::cerr << "Erro na barreira: " << ret << std::endl;
        exit(1);
    }

    std::atomic<bool> running = true;

    auto future = std::async(std::launch::async, [&]() {
      Message message(MESSAGE_SIZE, Control(Control::Type::COMMON), &car.prot);
      // std::unique_lock<std::mutex> stdout_lock(stdout_mtx);
      // stdout_lock.unlock();

      int64_t socket_delta = 0;
      int64_t shared_mem_delta = 0;
      int shared_mem_counter = 0;
      int socket_counter = 0;
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
          } else {
            socket_delta += recv_t - send_at;
          }
          // stdout_lock.lock();
          // std::cout << std::dec << "[Msg sent at "
          //           << formatTimestamp(*message.timestamp()) << " from ("
          //           << *message.getCoordX() << ", " << *message.getCoordY()
          //           << ") - ";
          // print_addr(*message.sourceAddr());
          // std::cout << " to ";
          // print_addr(*message.destAddr());
          // std::cout << "]" << std::endl;
          if (message.sourceAddr()->getSysID() == car.prot.getSysID()) {
            // std::cout << std::dec << " Proc (" << getpid() << ") Thread (" << 1
            // << "): Received (" << shared_mem_counter << "): ";
            shared_mem_counter++;
          } else {
            // std::cout << std::dec << " Proc (" << getpid() << ") Thread (" << 1
            //           << "): Received (" << socket_counter << "): ";
            socket_counter++;
          }
          // for (size_t i = 0; i < message.size(); i++) {
          //   std::cout << std::hex << static_cast<int>(message.data()[i]) << " ";
          // }
          // std::cout << std::endl << std::endl;
          // stdout_lock.unlock();
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
      pthread_mutex_unlock(shared_mutex);
    });

    if (future.wait_for(std::chrono::seconds(SIM_END)) ==
        std::future_status::timeout) {
      // std::cerr << "Carro(" << getpid() << ") Fim da simulação." << std::endl;
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

    // Cleanup shared memory
    munmap(shared_socket_deltas, NUM_CARS * sizeof(int64_t));
    munmap(shared_shared_mem_deltas, NUM_CARS * sizeof(int64_t));
    munmap(shared_mutex, sizeof(pthread_mutex_t));
    munmap(barrier, sizeof(pthread_barrier_t));
  }

  return 0;
}
