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

constexpr int NUM_SEND_THREADS = 1;
constexpr int NUM_RECV_THREADS = 2;
constexpr int NUM_MESSAGES_PER_RECV_THREAD = 35;

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

  SocketNIC rsnic(INTERFACE_NAME);
  SharedMemNIC smnic(INTERFACE_NAME);
  Protocol &prot = Protocol::getInstance(&rsnic, &smnic, getpid());

  std::mutex stdout_mtx;

  std::mutex cv_mtx;
  std::condition_variable cv;

  int receivers_ready = 0;
  constexpr SmartUnit Meter(SmartUnit::SIUnit::M);

  auto send_task = [&](const int thread_id) {
    Communicator communicator(&prot, thread_id);
    std::unique_lock<std::mutex> stdout_lock(stdout_mtx);
    stdout_lock.unlock();

    Transducer<Meter> transducer(0, 300000);
    SmartData<Communicator, Condition, Transducer<Meter>> smart_data(
        &communicator, &transducer, Condition(true, Meter.get_int_unit()));

    std::unique_lock<std::mutex> cv_lock(cv_mtx);
    cv.wait(cv_lock, [&receivers_ready]() {
      return receivers_ready == NUM_RECV_THREADS;
    });
  };

  auto receive_task = [&](const int thread_id) {
    Communicator communicator(&prot, thread_id + NUM_SEND_THREADS);
    uint32_t period = (thread_id + 1) * 5e3;
    SmartData<Communicator, Condition> smart_data(
        &communicator, Condition(false, Meter.get_int_unit(), period));
    std::unique_lock<std::mutex> stdout_lock(stdout_mtx);
    stdout_lock.unlock();

    for (int j = 1; j <= NUM_SEND_THREADS * NUM_MESSAGES_PER_RECV_THREAD; j++) {
      Message message = Message(sizeof(SmartData<Communicator, Condition>::Header) + Meter.get_value_size_bytes(), Message::Type::PUBLISH);
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

  std::array<std::thread, NUM_SEND_THREADS> send_threads;
  std::array<std::thread, NUM_RECV_THREADS> recv_threads;
  for (int i = 0; i < NUM_RECV_THREADS; ++i) {
    recv_threads[i] = std::thread(receive_task, i);
  }
  for (int i = 0; i < NUM_SEND_THREADS; ++i) {
    send_threads[i] = std::thread(send_task, i);
  }

  for (int i = 0; i < NUM_SEND_THREADS; ++i) {
    send_threads[i].join();
  }
  for (int i = 0; i < NUM_RECV_THREADS; ++i) {
    recv_threads[i].join();
  }

  return 0;
}
