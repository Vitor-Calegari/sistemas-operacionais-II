#define DEBUG_SYNC
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

    // Configurar diferentes tipos de dados para cada componente
    constexpr SmartUnit Watt(SmartUnit::SIUnit::KG * (SmartUnit::SIUnit::M ^ 2) *
                            (SmartUnit::SIUnit::S ^ 3));
    constexpr SmartUnit Farad(
        (SmartUnit::SIUnit::KG ^ -1) * (SmartUnit::SIUnit::M ^ -2) *
        (SmartUnit::SIUnit::S ^ 4) * (SmartUnit::SIUnit::A ^ 2));
    constexpr SmartUnit Hertz(SmartUnit::SIUnit::S ^ -1);

    // Cada componente usa uma unidade diferente
    auto transducer_watt = Transducer<Watt>(component_id, component_id * 10);
    auto transducer_farad = Transducer<Farad>(component_id + 100, component_id * 20);
    auto transducer_hertz = Transducer<Hertz>(component_id + 200, component_id * 30);

    // Registra como publisher para um tipo de dado
    auto smart_data_pub_watt = component.register_publisher(
        &transducer_watt, Condition(true, transducer_watt.unit.get_int_unit()));
    
    // Registra como subscriber para receber dados de outros componentes
    uint32_t period = 5e3; // 5ms
    auto smart_data_sub_watt = component.subscribe(Condition(false, Watt.get_int_unit(), period));
    auto smart_data_sub_farad = component.subscribe(Condition(false, Farad.get_int_unit(), period));
    auto smart_data_sub_hertz = component.subscribe(Condition(false, Hertz.get_int_unit(), period));

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

    // Thread para publicar dados
    std::thread publisher_thread([&]() {
      for (int msg = 0; msg < NUM_MESSAGES_PER_COMPONENT; ++msg) {
        try {
          // Obtém dados do transducer
          std::vector<std::byte> data(Watt.get_value_size_bytes());
          transducer_watt.get_data(data.data());

          // Publica dados usando smart_data
          auto timestamp = get_timestamp();
          smart_data_pub_watt.publish();
          
          std::cout << "[TEST] " << timestamp << " Component " << component_id 
                    << " published data " << msg + 1 << "/" << NUM_MESSAGES_PER_COMPONENT 
                    << " (data size: " << data.size() << " bytes)" << std::endl;
                    
          std::this_thread::sleep_for(std::chrono::milliseconds(800));
        } catch (const std::exception& e) {
          std::cout << "[TEST] " << get_timestamp() << " Component " << component_id 
                    << " exception publishing: " << e.what() << std::endl;
        }
      }
    });

    // Thread para receber dados
    std::thread subscriber_thread([&]() {
      for (int msg = 0; msg < NUM_MESSAGES_PER_COMPONENT * 2; ++msg) {
        try {
          Message message_watt = Message(sizeof(SmartData<Communicator, Condition>::Header) +
                                        Watt.get_value_size_bytes());
          message_watt.getControl()->setType(Control::Type::PUBLISH);

          if (smart_data_sub_watt.receive(&message_watt)) {
            auto timestamp = get_timestamp();
            std::cout << "[TEST] " << timestamp << " Component " << component_id 
                      << " received Watt data " << msg + 1 << " (size: " 
                      << message_watt.size() << " bytes)" << std::endl;
          }

          Message message_farad = Message(sizeof(SmartData<Communicator, Condition>::Header) +
                                          Farad.get_value_size_bytes());
          message_farad.getControl()->setType(Control::Type::PUBLISH);

          if (smart_data_sub_farad.receive(&message_farad)) {
            auto timestamp = get_timestamp();
            std::cout << "[TEST] " << timestamp << " Component " << component_id 
                      << " received Farad data " << msg + 1 << " (size: " 
                      << message_farad.size() << " bytes)" << std::endl;
          }

          Message message_hertz = Message(sizeof(SmartData<Communicator, Condition>::Header) +
                                          Hertz.get_value_size_bytes());
          message_hertz.getControl()->setType(Control::Type::PUBLISH);

          if (smart_data_sub_hertz.receive(&message_hertz)) {
            auto timestamp = get_timestamp();
            std::cout << "[TEST] " << timestamp << " Component " << component_id 
                      << " received Hertz data " << msg + 1 << " (size: " 
                      << message_hertz.size() << " bytes)" << std::endl;
          }

          std::this_thread::sleep_for(std::chrono::milliseconds(400));
        } catch (const std::exception& e) {
          std::cout << "[TEST] " << get_timestamp() << " Component " << component_id 
                    << " exception receiving: " << e.what() << std::endl;
        }
      }
    });

    publisher_thread.join();
    subscriber_thread.join();

    std::cout << "[TEST] " << get_timestamp() << " Component " << component_id 
              << " (PID: " << getpid() << ") finishing execution" << std::endl;

  } else {
    std::cout << "[TEST] " << get_timestamp() << " Parent process monitoring " 
              << NUM_COMPONENTS << " components" << std::endl;

    for (int i = 0; i < NUM_COMPONENTS; ++i) {
      int status;
      pid_t finished_pid = wait(&status);
    }
  }

  return 0;
}

// g++ -std=c++20 -g tests/src/e4/e4_components_same_car.cc -Iinclude -Itests/include -o components_same_car_test src/ethernet.cc src/utils.cc