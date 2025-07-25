#ifndef SIMULATION_TIMESTAMP
#define SIMULATION_TIMESTAMP 1748768400000000
#endif

#define USE_NAVIGATORCSV

#include "car.hh"
#include "map.hh"
#include "clocks.hh"
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

#ifndef INTERFACE_NAME
#define INTERFACE_NAME "lo"
#endif

constexpr int NUM_CARS = 15;
constexpr int MESSAGE_SIZE = 92;
constexpr int64_t SIM_END = 60;

std::string formatTimestamp(uint64_t timestamp_us) {
  auto time_point = std::chrono::system_clock::time_point(
      std::chrono::microseconds(timestamp_us));
  std::time_t time_t = std::chrono::system_clock::to_time_t(time_point);
  std::tm *tm = std::localtime(&time_t);

  std::ostringstream oss;
  oss << std::put_time(tm, "%H:%M:%S") << "." << std::setw(6)
      << std::setfill('0') << (timestamp_us % 1000000) << " us";
  return oss.str();
}

int main() {
  using SocketNIC = NIC<Engine<Ethernet>>;
  using SharedMemNIC = NIC<SharedEngine<SharedMem>>;
  using Protocol = Protocol<SocketNIC, SharedMemNIC, NavigatorDirected>;
  using Message = Message<Protocol::Address, Protocol>;

  constexpr auto print_addr = [](const Protocol::Address &addr) {
    for (auto k : addr.getPAddr().mac) {
      std::cout << int(k) << ' ';
    }
    std::cout << ": " << addr.getSysID() << " : " << addr.getPort();
  };

  Map *map = new Map(3, 3);

  auto parent_pid = getpid();

  [[maybe_unused]] GlobalTime &g = GlobalTime::getInstance();

  std::cout << "Inicializando Simulação" << std::endl;
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
    std::mutex stdout_mtx;

    csv::CSVReader _reader("tests/dataset/dynamics-vehicle_" + dataset_id + ".csv");
    csv::CSVRow row;
    _reader.read_row(row);
    int64_t init_timestamp = row["timestamp"].get<int64_t>();
    CSVReaderSingleTone::getInstance("tests/dataset/dynamics-vehicle_" + dataset_id + ".csv");
    
    std::this_thread::sleep_until(std::chrono::steady_clock::now() + std::chrono::microseconds(init_timestamp - SIMULATION_TIMESTAMP));     

    E7Car car(dataset_id, label);

    std::atomic<bool> running = true;

    auto future = std::async(std::launch::async, [&]() {
      Message message(MESSAGE_SIZE, Control(Control::Type::COMMON), &car.prot);
      std::unique_lock<std::mutex> stdout_lock(stdout_mtx);
      stdout_lock.unlock();

      int shared_mem_counter = 0;
      int socket_counter = 0;
      while (running) {
        memset(message.data(), 0, MESSAGE_SIZE);
        if (!car.receive(&message)) {
          std::cerr << "Erro ao receber mensagem na thread " << 1 << std::endl;
          exit(1);
        } else {
          stdout_lock.lock();
          std::cout << std::dec << "[Msg sent at "
                    << formatTimestamp(*message.timestamp()) << " from ("
                    << *message.getCoordX() << ", " << *message.getCoordY()
                    << ") - ";
          print_addr(*message.sourceAddr());
          std::cout << " to ";
          print_addr(*message.destAddr());
          std::cout << "]" << std::endl;
          if (message.sourceAddr()->getSysID() == car.prot.getSysID()) {
            std::cout << std::dec << " Proc (" << getpid() << ") Thread (" << 1
                      << "): Received (" << shared_mem_counter << "): ";
            shared_mem_counter++;
          } else {
            std::cout << std::dec << " Proc (" << getpid() << ") Thread (" << 1
                      << "): Received (" << socket_counter << "): ";
            socket_counter++;
          }
          for (size_t i = 0; i < message.size(); i++) {
            std::cout << std::hex << static_cast<int>(message.data()[i]) << " ";
          }
          std::cout << std::endl << std::endl;
          stdout_lock.unlock();
        }
      }
    });

    if (future.wait_for(std::chrono::seconds(SIM_END)) ==
        std::future_status::timeout) {
      std::cerr << "Carro(" << getpid() << ") Fim da simulação." << std::endl;
      running = false;
      future.wait();
    }
  } else {
    for (int i = 0; i < NUM_CARS; ++i) {
      wait(nullptr);
    }
    delete map;
  }

  return 0;
}
