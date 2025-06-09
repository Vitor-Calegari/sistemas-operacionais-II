#include "car.hh"
#include "communicator.hh"
#include "engine.hh"
#include "message.hh"
#include "nic.hh"
#include "protocol.hh"
#include "shared_engine.hh"
#include "smart_data.hh"
#include "smart_unit.hh"
#include "transducer.hh"
#include <array>
#include <cassert>
#include <csignal>
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>

constexpr int NUM_PUB_THREADS = 4;
constexpr int NUM_SUB_THREADS = 6;
constexpr int SUB_NUM_WANTED_MESSAGES = 5;

#ifndef INTERFACE_NAME
#define INTERFACE_NAME "lo"
#endif

std::string formatTimestamp(uint64_t timestamp_us) {
  auto time_point = std::chrono::system_clock::time_point(std::chrono::microseconds(timestamp_us));
  std::time_t time_t = std::chrono::system_clock::to_time_t(time_point);
  std::tm *tm = std::localtime(&time_t);

  std::ostringstream oss;
  oss << std::put_time(tm, "%H:%M:%S") << "."
      << std::setw(6) << std::setfill('0') << (timestamp_us % 1000000) << " us";
  return oss.str();
}

int main() {
  using Buffer = Buffer<Ethernet::Frame>;
  using SocketNIC = NIC<Engine<Buffer>>;
  using SharedMemNIC = NIC<SharedEngine<Buffer>>;
  using Protocol = Protocol<SocketNIC, SharedMemNIC, NavigatorDirected>;
  using Message = Message<Protocol::Address>;
  using Communicator = Communicator<Protocol, Message>;

  std::mutex stdout_mtx;

  std::mutex sub_count_mutex;
  std::condition_variable cond_all_subscribers_done;

  int num_subscribers_done = 0;

  auto publisher_task = [&]<SmartUnit T>(const int thread_id, Car *car,
                                         Transducer<T> *transducer) {
    auto comp = car->create_component(thread_id);

    auto smart_data = comp.register_publisher(
        transducer, Condition(true, transducer->unit.get_int_unit()));

    std::unique_lock<std::mutex> cv_lock(sub_count_mutex);
    cond_all_subscribers_done.wait(cv_lock, [&num_subscribers_done]() {
      return num_subscribers_done == NUM_SUB_THREADS;
    });
  };

  auto subscriber_task = [&](const int thread_id, Car *car,
                             const SmartUnit unit) {
    auto comp = car->create_component(thread_id);

    uint32_t period = (thread_id + 1) * 5e3;
    auto smart_data =
        comp.subscribe(Condition(false, unit.get_int_unit(), period));

    std::unique_lock<std::mutex> stdout_lock(stdout_mtx);
    stdout_lock.unlock();

    for (int j = 1; j <= SUB_NUM_WANTED_MESSAGES; ++j) {
      Message message =
          Message(sizeof(SmartData<Communicator, Condition>::Header) +
                  unit.get_value_size_bytes());
      message.getControl()->setType(Control::Type::PUBLISH);
      if (!smart_data.receive(&message)) {
        std::cerr << "Erro ao receber mensagem na thread " << thread_id
                  << std::endl;
        exit(1);
      } else {
        stdout_lock.lock();
        std::cout << "[Msg sent at: " << formatTimestamp(*message.timestamp())  << "] " << std::dec << car->label
                  << " (thread " << thread_id << "): Received (" << j << "): ";
        for (size_t i = 0; i < message.size(); i++) {
          std::cout << std::hex << static_cast<int>(message.data()[i]) << " ";
        }
        std::cout << std::endl;
        stdout_lock.unlock();
      }
    }
    sub_count_mutex.lock();
    num_subscribers_done++;
    sub_count_mutex.unlock();
    cond_all_subscribers_done.notify_all();
  };

  std::vector<Car> cars = { Car("Citroën"), Car("Peugeot") };

  constexpr SmartUnit Watt(SmartUnit::SIUnit::KG * (SmartUnit::SIUnit::M ^ 2) *
                           (SmartUnit::SIUnit::S ^ 3));
  constexpr SmartUnit Farad(
      (SmartUnit::SIUnit::KG ^ -1) * (SmartUnit::SIUnit::M ^ -2) *
      (SmartUnit::SIUnit::S ^ 4) * (SmartUnit::SIUnit::A ^ 2));
  constexpr SmartUnit Hertz(SmartUnit::SIUnit::S ^ -1);
  std::vector<SmartUnit> units = { Watt, Farad, Hertz };

  std::array<std::thread, NUM_PUB_THREADS> pub_threads;
  std::array<std::thread, NUM_SUB_THREADS> sub_threads;

  for (int i = 0; i < NUM_SUB_THREADS; ++i) {
    sub_threads[i] = std::thread(subscriber_task, i + NUM_PUB_THREADS,
                                 &cars[i % 2], units[i % 3]);
  }

  auto transd1 = Transducer<Watt>(0, 1000);
  auto transd2 = Transducer<Farad>(0, 2000);
  auto transd3 = Transducer<Hertz>(0, 3000);
  auto transd4 = Transducer<Hertz>(15000, 30000);

  pub_threads[0] = std::thread(publisher_task, 0, &cars[0], &transd1);
  pub_threads[1] = std::thread(publisher_task, 1, &cars[1], &transd2);
  pub_threads[2] = std::thread(publisher_task, 2, &cars[0], &transd3);
  pub_threads[3] = std::thread(publisher_task, 3, &cars[1], &transd4);

  for (int i = 0; i < NUM_PUB_THREADS; ++i) {
    pub_threads[i].join();
  }

  for (int i = 0; i < NUM_SUB_THREADS; ++i) {
    sub_threads[i].join();
  }

  return 0;
}
