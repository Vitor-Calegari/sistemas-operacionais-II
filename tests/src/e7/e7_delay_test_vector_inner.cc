#include <chrono>
#ifndef SIMULATION_TIMESTAMP
#define SIMULATION_TIMESTAMP 1748768400000000
#endif

#define TURN_PTP_OFF
#define DEBUG_DELAY

#include "car.hh"
#include "map.hh"
#include "shared_mem.hh"
#include "utils.hh"
#include "csv_reader_singletone.hh"

#include <array>
#include <cassert>
#include <csignal>
#include <future>
#include <iomanip>
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>
#include <cfloat>
#include <iostream>
#include <unistd.h>    // para getcwd
#include <limits.h> 

#ifndef INTERFACE_NAME
#define INTERFACE_NAME "lo"
#endif

constexpr int NUM_CARS = 15;
constexpr int MESSAGE_SIZE = 100;
constexpr int64_t NUM_ITER = 1000;

int64_t *shared_socket_deltas;
int64_t *shared_shared_mem_deltas;
pthread_mutex_t *shared_mutex;
double *max_shared;
double *min_shared;
double *max_socket;
double *min_socket;

void init_shared_vars() {
  shared_socket_deltas =
  (int64_t *)mmap(NULL, NUM_CARS * sizeof(int64_t), PROT_READ | PROT_WRITE,
                  MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  shared_shared_mem_deltas =
    (int64_t *)mmap(NULL, NUM_CARS * sizeof(int64_t), PROT_READ | PROT_WRITE,
                    MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  shared_mutex = (pthread_mutex_t *)mmap(
    NULL, sizeof(pthread_mutex_t), PROT_READ | PROT_WRITE,
    MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  max_shared =
    (double *)mmap(NULL, sizeof(double), PROT_READ | PROT_WRITE,
                    MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  min_shared =
    (double *)mmap(NULL, sizeof(double), PROT_READ | PROT_WRITE,
                    MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  max_socket =
    (double *)mmap(NULL, sizeof(double), PROT_READ | PROT_WRITE,
                    MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  min_socket =
    (double *)mmap(NULL, sizeof(double), PROT_READ | PROT_WRITE,
                    MAP_SHARED | MAP_ANONYMOUS, -1, 0);

  *max_shared = DBL_MIN;
  *min_shared = DBL_MAX;
  *max_socket = DBL_MIN;
  *min_socket = DBL_MAX;

  pthread_mutexattr_t mutex_attr;
  pthread_mutexattr_init(&mutex_attr);
  pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);
  pthread_mutex_init(shared_mutex, &mutex_attr);

  for (int i = 0; i < NUM_CARS; i++) {
  shared_socket_deltas[i] = 0;
  shared_shared_mem_deltas[i] = 0;
  }
}

void delete_shared_vars() {
  // Cleanup shared memory
  munmap(max_shared, sizeof(double));
  munmap(min_shared, sizeof(double));
  munmap(max_socket, sizeof(double));
  munmap(min_socket, sizeof(double));
  munmap(shared_socket_deltas, NUM_CARS * sizeof(int64_t));
  munmap(shared_shared_mem_deltas, NUM_CARS * sizeof(int64_t));
  munmap(shared_mutex, sizeof(pthread_mutex_t));
}

void print_results() {
  double mean_socket_delay = 0;
  for (int i = 0; i < NUM_CARS; i++) {
    mean_socket_delay += static_cast<double>(shared_socket_deltas[i]);
  }
  mean_socket_delay /= NUM_CARS;
  std::cout << "Média de delay de envio Socket: " << mean_socket_delay
            << " microseconds" << std::endl;
  double mean_shared_mem_delay = 0;
  for (int i = 0; i < NUM_CARS; i++) {
    mean_shared_mem_delay += static_cast<double>(shared_shared_mem_deltas[i]);
  }
  mean_shared_mem_delay = mean_shared_mem_delay / NUM_CARS;
  std::cout << "Média de delay de envio SharedMem: " << mean_shared_mem_delay
            << " microseconds" << std::endl;

  std::cout << "Máximo delay de envio Socket: " << *max_socket
            << " microseconds" << std::endl;
  std::cout << "Mínimo delay de envio Socket: " << *min_socket
            << " microseconds" << std::endl;
  std::cout << "Máximo delay de envio SharedMem: " << *max_shared
            << " microseconds" << std::endl;
  std::cout << "Mínimo delay de envio SharedMem: " << *min_shared
            << " microseconds" << std::endl;
}


int main() {
  using SocketNIC = NIC<Engine<Ethernet>>;
  using SharedMemNIC = NIC<SharedEngine<SharedMem>>;
  using Protocol = Protocol<SocketNIC, SharedMemNIC, NavigatorDirected>;
  using Message = Message<Protocol::Address, Protocol>;

  // Map *map = new Map(3, 3);

  init_shared_vars();

  auto parent_pid = getpid();

  std::cout << "Inicializando teste de delay de envio" << std::endl;

  std::array<std::string, NUM_CARS> labels = { "1124", "1272", "1947", "680",
                                               "1426", "1503", "532",  "757",
                                               "313",  "2101", "1580", "1870",
                                               "1349", "2024", "1047" };
  std::string label = "";
  std::string dataset_id = "";
  [[maybe_unused]]
  GlobalTime &g = GlobalTime::getInstance();
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
    std::vector<std::pair<double, int64_t>> socket_points;
    std::vector<std::pair<double, int64_t>> shared_mem_points;

    double socket_delta = 0;
    double shared_mem_delta = 0;
    int shared_mem_counter = 0;
    int socket_counter = 0;

    char cwd[PATH_MAX];  // buffer para armazenar o caminho

    if (getcwd(cwd, sizeof(cwd)) != nullptr) {
        // std::cout << "Diretório atual: " << cwd << std::endl;
    } else {
        perror("getcwd() erro");
    }
    std::string path(cwd);
    csv::CSVReader _reader(path + "/tests/dataset/dynamics-vehicle_" + dataset_id + ".csv");
    csv::CSVRow row;
    _reader.read_row(row);
    int64_t init_timestamp = row["timestamp"].get<int64_t>();
    CSVReaderSingleTone::getInstance(path + "/tests/dataset/dynamics-vehicle_" + dataset_id + ".csv");
    
    std::this_thread::sleep_until(std::chrono::steady_clock::now() + std::chrono::microseconds(init_timestamp - SIMULATION_TIMESTAMP));     

    E7Car car(dataset_id, label);
    Message message(MESSAGE_SIZE, Control(Control::Type::COMMON), &car.prot);

    for (int i = 0; i < NUM_ITER; ++i) {
      memset(message.data(), 0, MESSAGE_SIZE);
      if (!car.receive(&message)) {
        std::cerr << "Erro ao receber mensagem na thread " << 1 << std::endl;
        exit(1);
      }
    }
    socket_points = car.get_socket_delays();
    for (const auto& pair : socket_points) {
      socket_delta += static_cast<double>(pair.second);
      socket_counter++;
    }
    double max_sock_int = static_cast<double>(car.get_max_socket_delay());
    double min_sock_int = static_cast<double>(car.get_min_socket_delay());

    shared_mem_points = car.get_shared_delays();
    for (const auto& pair : shared_mem_points) {
      shared_mem_delta += static_cast<double>(pair.second);
      shared_mem_counter++;
    }
    double max_shared_int = static_cast<double>(car.get_max_shared_delay());
    double min_shared_int = static_cast<double>(car.get_min_shared_delay());

    double mean_socket_delta = 0;
    if (socket_delta != 0) {
      mean_socket_delta = socket_delta / socket_counter;
    }
    double mean_shared_mem_delta = 0;
    if (shared_mem_delta != 0) {
      mean_shared_mem_delta = shared_mem_delta / shared_mem_counter;
    }
    pthread_mutex_lock(shared_mutex);
    std::cout << "Carro " << dataset_id << " (socket): ";
    for (auto [t, _] : socket_points) {
      std::cout << t << ", ";
    }
    std::cout << std::endl;
    for (auto [_, d] : socket_points) {
      std::cout << d << ", ";
    }
    std::cout << std::endl << std::endl;

    // std::cout << "Carro " << dataset_id << " (shared mem): ";
    // for (auto [t, _] : shared_mem_points) {
    //   std::cout << t << ", ";
    // }
    // std::cout << std::endl;
    // for (auto [_, d] : shared_mem_points) {
    //   std::cout << d << ", ";
    // }
    // std::cout << std::endl << std::endl;

    shared_socket_deltas[std::stoi(car._dataset_id)] = mean_socket_delta;
    shared_shared_mem_deltas[std::stoi(car._dataset_id)] =
        mean_shared_mem_delta;
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
  } else {
    for (int i = 0; i < NUM_CARS; ++i) {
      wait(nullptr);
    }
    // delete map;
    print_results();
    delete_shared_vars();
  }

  return 0;
}
