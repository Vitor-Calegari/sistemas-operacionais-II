#include "communicator.hh"
#include "engine.hh"
#include "map.hh"
#include "message.hh"
#include "navigator.hh"
#include "nic.hh"
#include "protocol.hh"
#include "shared_engine.hh"
#include "shared_mem.hh"
#include "utils.hh"
#include <csignal>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <semaphore>
#include <sys/mman.h>
#include <sys/wait.h>

constexpr int NUM_MESSAGES_PER_SENDER = 10;
constexpr int MESSAGE_SIZE = 5;

#ifndef INTERFACE_NAME
#define INTERFACE_NAME "lo"
#endif

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
  using Communicator = Communicator<Protocol, Message>;

  constexpr auto print_addr = [](const Protocol::Address &addr) {
    for (auto k : addr.getPAddr().mac) {
      std::cout << int(k) << ' ';
    }
    std::cout << ": " << addr.getSysID() << " : " << addr.getPort();
  };

  sem_t *semaphore =
      static_cast<sem_t *>(mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE,
                                MAP_SHARED | MAP_ANONYMOUS, -1, 0));
  sem_init(semaphore, 1, 0); // Inicialmente bloqueado

  std::binary_semaphore *stdout_mtx = static_cast<std::binary_semaphore *>(
      mmap(NULL, sizeof(std::binary_semaphore), PROT_READ | PROT_WRITE,
           MAP_SHARED | MAP_ANONYMOUS, -1, 0));
  stdout_mtx->release();

  Map *map = new Map(1, 1);

  int parentPID = getpid();

  // Novo processo será o sender.
  auto ret = fork();
  bool is_sender = ret == 0;

  Topology topo = map->getTopology();
  NavigatorCommon::Coordinate point(0, 0);
  Protocol &prot =
      Protocol::getInstance(INTERFACE_NAME, getpid(), { point }, topo, 10, 0);

  if (is_sender) {
    std::thread inter_sender_thread([&]() {
      Communicator comm = Communicator(&prot, 10);

      sem_wait(semaphore);

      for (int i = 0; i < NUM_MESSAGES_PER_SENDER;) {
        Message message = Message(
            comm.addr(), Protocol::Address(prot.getNICPAddr(), parentPID, 2),
            MESSAGE_SIZE, Control(Control::Type::COMMON), &prot);

        stdout_mtx->acquire();
        std::cout << "Outer Sender: Sending (" << std::dec << i << "): ";
        for (size_t j = 0; j < message.size(); j++) {
          message.data()[j] = std::byte(randint(0, 255));
          std::cout << std::hex << static_cast<int>(message.data()[j]) << " ";
        }
        std::cout << std::endl << std::endl;
        stdout_mtx->release();

        if (comm.send(&message)) {
          i++;
        }
      }
    });

    inter_sender_thread.join();
    exit(0);
  } else {
    std::mutex cv_mtx;
    std::condition_variable cv;

    bool receiver_ready = false;

    std::thread inner_sender_thread([&]() {
      Communicator comm = Communicator(&prot, 1);
      std::unique_lock<std::mutex> cv_lock(cv_mtx);
      cv.wait(cv_lock, [&receiver_ready]() { return receiver_ready; });

      for (int i = 0; i < NUM_MESSAGES_PER_SENDER;) {
        Message message = Message(
            comm.addr(), Protocol::Address(prot.getNICPAddr(), getpid(), 2),
            MESSAGE_SIZE, Control(Control::Type::COMMON), &prot);

        stdout_mtx->acquire();
        std::cout << "Inner Sender: Sending (" << std::dec << i << "): ";
        for (size_t j = 0; j < message.size(); j++) {
          message.data()[j] = std::byte(randint(0, 255));
          std::cout << std::hex << static_cast<int>(message.data()[j]) << " ";
        }
        std::cout << std::endl << std::endl;
        stdout_mtx->release();

        if (comm.send(&message)) {
          i++;
        }
      }
    });

    std::thread receiver_thread([&]() {
      Communicator comm = Communicator(&prot, 2);
      sem_post(semaphore);
      receiver_ready = true;
      cv.notify_all();
      for (int i_m = 0; i_m < 2 * NUM_MESSAGES_PER_SENDER; ++i_m) {
        Message message =
            Message(MESSAGE_SIZE, Control(Control::Type::COMMON), &prot);

        comm.receive(&message);

        stdout_mtx->acquire();

        std::cout << std::dec << "[Msg sent at "
                  << formatTimestamp(*message.timestamp()) << " from ("
                  << *message.getCoordX() << ", " << *message.getCoordY()
                  << ") - {";
        print_addr(*message.sourceAddr());
        std::cout << "} to {";
        print_addr(*message.destAddr());
        std::cout << "}]" << std::endl;

        std::cout << "Receiver: Received (" << std::dec << i_m << "): ";
        for (size_t i = 0; i < message.size(); i++) {
          std::cout << std::hex << static_cast<int>(message.data()[i]) << " ";
        }
        std::cout << std::endl << std::endl;

        stdout_mtx->release();
      }
    });

    inner_sender_thread.join();
    receiver_thread.join();
  }

  delete map;
  if (!is_sender) {
    int status;
    wait(&status);
  }

  sem_destroy(semaphore);
  // munmap(semaphore, sizeof *semaphore);
  munmap(stdout_mtx, sizeof *stdout_mtx);

  return 0;
}
