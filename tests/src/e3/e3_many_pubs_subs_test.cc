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

constexpr int NUM_PUB_THREADS = 3;
constexpr int NUM_SUB_THREADS = 4;
constexpr int NUM_MESSAGES_PER_RECV_THREAD = 18;

#ifndef INTERFACE_NAME
#define INTERFACE_NAME "lo"
#endif

int main() {
  using Buffer = Buffer<Ethernet::Frame>;
  using SocketNIC = NIC<Engine<Buffer>>;
  using SharedMemNIC = NIC<SharedEngine<Buffer>>;
  using Protocol = Protocol<SocketNIC, SharedMemNIC>;
  using Message = Message<Protocol::Address>;
  using Communicator = Communicator<Protocol, Message>;

  std::mutex stdout_mtx;

  std::mutex cv_mtx;
  std::condition_variable cv;

  int receivers_ready = 0;

  constexpr SmartUnit Watt(SmartUnit::SIUnit::KG * (SmartUnit::SIUnit::M ^ 2) *
                           (SmartUnit::SIUnit::S ^ 3));
  constexpr SmartUnit Farad(
      (SmartUnit::SIUnit::KG ^ -1) * (SmartUnit::SIUnit::M ^ -2) *
      (SmartUnit::SIUnit::S ^ 4) * (SmartUnit::SIUnit::A ^ 2));
  constexpr SmartUnit Hertz(SmartUnit::SIUnit::S ^ -1);

  Car car = Car();

  auto publisher_task = [&]<SmartUnit T>(const int thread_id,
                                         Transducer<T> *transducer) {
    auto comp = car.create_component(thread_id);

    std::unique_lock<std::mutex> stdout_lock(stdout_mtx);
    stdout_lock.unlock();

    // Transducer<unit> transducer(0, 300000);
    auto smart_data = comp.register_publisher(
        transducer, Condition(true, Watt.get_int_unit()));

    std::unique_lock<std::mutex> cv_lock(cv_mtx);
    cv.wait(cv_lock, [&receivers_ready]() {
      return receivers_ready == NUM_SUB_THREADS;
    });
  };

  auto subscriber_task = [&](const int thread_id, const SmartUnit unit) {
    auto comp = car.create_component(thread_id + NUM_PUB_THREADS);

    uint32_t period = (thread_id + 1) * 5e3;
    auto smart_data =
        comp.subscribe(Condition(false, unit.get_int_unit(), period));

    std::unique_lock<std::mutex> stdout_lock(stdout_mtx);
    stdout_lock.unlock();

    for (int j = 1; j <= NUM_PUB_THREADS * NUM_MESSAGES_PER_RECV_THREAD; j++) {
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
        std::cout << std::dec << "Thread (" << thread_id << "): Received (" << j
                  << "): ";
        for (size_t i = 0; i < message.size(); i++) {
          std::cout << std::hex << static_cast<int>(message.data()[i]) << " ";
        }
        std::cout << std::endl;
        stdout_lock.unlock();
      }
    }
    cv_mtx.lock();
    receivers_ready++;
    cv_mtx.unlock();
    cv.notify_all();
  };

  std::array<std::thread, NUM_PUB_THREADS> send_threads;
  std::array<std::thread, NUM_SUB_THREADS> recv_threads;

  std::vector<SmartUnit> a = { Watt, Farad };
  for (int i = 0; i < NUM_SUB_THREADS; ++i) {
    recv_threads[i] = std::thread(subscriber_task, i, a[i % 2]);
  }

  auto t1 = Transducer<Watt>(0, 1000);
  auto t2 = Transducer<Farad>(0, 2000);
  auto t3 = Transducer<Hertz>(0, 3000);

  send_threads[0] = std::thread(publisher_task, 0, &t1);
  send_threads[1] = std::thread(publisher_task, 1, &t1);
  send_threads[2] = std::thread(publisher_task, 2, &t3);
  // send_threads[0] = std::thread(publisher_task, 0, Transducer<Watt>(0,
  // 1000)); send_threads[1] = std::thread(publisher_task, 0,
  // Transducer<Farad>(0, 2000)); send_threads[2] = std::thread(publisher_task,
  // 0, Transducer<Hertz>(0, 3000));

  for (int i = 0; i < NUM_PUB_THREADS; ++i) {
    send_threads[i].join();
  }
  for (int i = 0; i < NUM_SUB_THREADS; ++i) {
    recv_threads[i].join();
  }

  return 0;
}
