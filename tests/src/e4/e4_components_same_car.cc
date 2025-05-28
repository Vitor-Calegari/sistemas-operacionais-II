#define DEBUG_SYNC
#include "car.hh"
#include "communicator.hh"
#include "engine.hh"
#include "message.hh"
#include "nic.hh"
#include "protocol.hh"
#include "shared_engine.hh"
#undef DEBUG_SYNC

#include <cassert>
#include <csignal>
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>
#include <thread>
#include <mutex>
#include <condition_variable>

#ifndef INTERFACE_NAME
#define INTERFACE_NAME "lo"
#endif

constexpr int NUM_COMPONENTS = 4;
constexpr int CYCLE_DURATION_SECONDS = 4;
constexpr int NUM_MESSAGES_PER_COMPONENT = 5;

// Teste simulando comunicação entre componentes do mesmo carro
int main() {
  using Buffer = Buffer<Ethernet::Frame>;
  using SocketNIC = NIC<Engine<Buffer>>;
  using SharedMemNIC = NIC<SharedEngine<Buffer>>;
  using Protocol = Protocol<SocketNIC, SharedMemNIC>;
  using Message = Message<Protocol::Address>;
  using Communicator = Communicator<Protocol, Message>;

  auto parent_pid = getpid();
  std::mutex stdout_mtx;

  std::cout << "[TEST] " << get_timestamp() 
            << " Starting same car components communication test with " 
            << NUM_COMPONENTS << " components" << std::endl;

  for (auto i = 0; i < NUM_COMPONENTS; ++i) {
    auto cur_pid = fork();
    if (cur_pid == 0) {
      break;
    }
  }

  if (getpid() != parent_pid) {
    Car car;
    
    int component_id = 1000 + (getpid() % 1000);
    auto component = car.create_component(component_id);

    std::cout << "[TEST] " << get_timestamp() << " Component " << component_id 
              << " (PID: " << getpid() << ") initialized in car: " << car.label << std::endl;

    // Período inicial para estabelecer comunicação
    std::this_thread::sleep_for(std::chrono::seconds(CYCLE_DURATION_SECONDS));

    // Verifica se este componente é líder
    if (car.prot.amILeader()) {
      std::cout << "[TEST] " << get_timestamp() << " Component " << component_id 
                << " (PID: " << getpid() << ") is LEADER" << std::endl;
    } else {
      std::cout << "[TEST] " << get_timestamp() << " Component " << component_id 
                << " (PID: " << getpid() << ") is FOLLOWER" << std::endl;
    }

    // Thread para enviar mensagens
    std::thread sender_thread([&]() {
      for (int msg = 0; msg < NUM_MESSAGES_PER_COMPONENT; ++msg) {
        try {
          // Cria mensagem simples com dados do componente
          Message message(component.addr(), 
                         Protocol::Address(Ethernet::BROADCAST_ADDRESS, 
                                         Protocol::BROADCAST_SID, 
                                         Protocol::BROADCAST), 
                         128);
          
          auto timestamp = get_timestamp();
          
          if (component.send(&message)) {
            std::cout << "[TEST] " << timestamp << " Component " << component_id 
                      << " sent message " << msg + 1 << "/" << NUM_MESSAGES_PER_COMPONENT 
                      << " (size: " << message.size() << " bytes)" << std::endl;
          } else {
            std::cout << "[TEST] " << timestamp << " Component " << component_id 
                      << " failed to send message " << msg + 1 << std::endl;
          }
                    
          std::this_thread::sleep_for(std::chrono::milliseconds(800));
        } catch (const std::exception& e) {
          std::cout << "[TEST] " << get_timestamp() << " Component " << component_id 
                    << " exception sending: " << e.what() << std::endl;
        }
      }
    });

    // Thread para receber mensagens
    std::thread receiver_thread([&]() {
      for (int msg = 0; msg < NUM_MESSAGES_PER_COMPONENT * 2; ++msg) {
        try {
          Message message(128);
          
          if (component.receive(&message)) {
            auto timestamp = get_timestamp();
            std::cout << "[TEST] " << timestamp << " Component " << component_id  
                      << message.size() << " bytes)" << std::endl;
          } else {
            std::cout << "[TEST] " << get_timestamp() << " Component " << component_id 
                      << " no message received " << msg + 1 << std::endl;
          }

          std::this_thread::sleep_for(std::chrono::milliseconds(400));
        } catch (const std::exception& e) {
          std::cout << "[TEST] " << get_timestamp() << " Component " << component_id 
                    << " exception receiving: " << e.what() << std::endl;
        }
      }
    });

    sender_thread.join();
    receiver_thread.join();

    std::cout << "[TEST] " << get_timestamp() << " Component " << component_id 
              << " (PID: " << getpid() << ") finishing execution" << std::endl;

  } else {
    std::cout << "[TEST] " << get_timestamp() << " Parent process monitoring " 
              << NUM_COMPONENTS << " components" << std::endl;

    for (int i = 0; i < NUM_COMPONENTS; ++i) {
      int status;
      pid_t finished_pid = wait(&status);
      std::cout << "[TEST] " << get_timestamp() << " Component process " 
                << finished_pid << " finished with status " << status << std::endl;
    }

    std::cout << std::endl << "[TEST] " << get_timestamp() 
              << " All components communication test completed" << std::endl;
    
    std::cout << "\n=== Test Summary ===" << std::endl;
    std::cout << "Expected behavior:" << std::endl;
    std::cout << "1. Components establish communication within the same car" << std::endl;
    std::cout << "2. Each component sends timestamped messages" << std::endl;
    std::cout << "3. Components receive messages from other components" << std::endl;
    std::cout << "4. One component becomes leader and coordinates" << std::endl;
    std::cout << "5. All messages include precise timestamps for synchronization" << std::endl;
  }

  return 0;
}

// g++ -std=c++20 -g tests/src/e4/e4_components_same_car.cc -Iinclude -Itests/include -o same_car_test src/ethernet.cc src/utils.cc