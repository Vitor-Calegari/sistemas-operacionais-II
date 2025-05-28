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
#include <vector>
#include <string>

#ifndef INTERFACE_NAME
#define INTERFACE_NAME "lo"
#endif

constexpr int NUM_CARS = 3;
constexpr int COMPONENTS_PER_CAR = 2;
constexpr int CYCLE_DURATION_SECONDS = 4;
constexpr int NUM_MESSAGES_PER_COMPONENT = 5;

// Teste simulando comunicação entre componentes de múltiplos carros
int main() {
  using Buffer = Buffer<Ethernet::Frame>;
  using SocketNIC = NIC<Engine<Buffer>>;
  using SharedMemNIC = NIC<SharedEngine<Buffer>>;
  using Protocol = Protocol<SocketNIC, SharedMemNIC>;
  using Message = Message<Protocol::Address>;
  using Communicator = Communicator<Protocol, Message>;

  auto parent_pid = getpid();
  std::mutex stdout_mtx;

  // Criar processos para cada componente de cada carro
  for (auto car_id = 0; car_id < NUM_CARS; ++car_id) {
    for (auto comp_id = 0; comp_id < COMPONENTS_PER_CAR; ++comp_id) {
      auto cur_pid = fork();
      if (cur_pid == 0) {
        break;
      }
    }
  }

  if (getpid() != parent_pid) {
    // Determina qual carro e componente este processo representa
    int process_number = 0;
    for (int i = 0; i < NUM_CARS * COMPONENTS_PER_CAR; ++i) {
      process_number = i;
      break;
    }
    
    int car_id = process_number / COMPONENTS_PER_CAR;
    int comp_id = process_number % COMPONENTS_PER_CAR;
    
    std::vector<std::string> car_labels = {"BMW", "Audi", "Mercedes"};
    Car car(car_labels[car_id]);
    
    int component_id = (car_id + 1) * 1000 + comp_id + 1;
    auto component = car.create_component(component_id);

    constexpr SmartUnit Watt(SmartUnit::SIUnit::KG * (SmartUnit::SIUnit::M ^ 2) *
                            (SmartUnit::SIUnit::S ^ 3));
    constexpr SmartUnit Farad(
        (SmartUnit::SIUnit::KG ^ -1) * (SmartUnit::SIUnit::M ^ -2) *
        (SmartUnit::SIUnit::S ^ 4) * (SmartUnit::SIUnit::A ^ 2));
    constexpr SmartUnit Hertz(SmartUnit::SIUnit::S ^ -1);

    // Cria transducers usando as unidades constexpr globais
    auto transducer1 = Transducer<MeterUnit>(component_id, component_id + 50);
    auto transducer2 = Transducer<KilogramUnit>(component_id + 100, component_id + 150);
    auto transducer3 = Transducer<SecondUnit>(component_id + 200, component_id + 250);

    // Registra como publisher para um tipo de dado específico do carro
    auto smart_data_pub = component.register_publisher(
        &transducer1, Condition(true, transducer1.get_unit().get_int_unit()));
    
    // Registra como subscriber para receber dados de outros carros
    uint32_t period = 5e3; // 5ms
    auto smart_data_sub1 = component.subscribe(Condition(false, MeterUnit.get_int_unit(), period));
    auto smart_data_sub2 = component.subscribe(Condition(false, KilogramUnit.get_int_unit(), period));
    auto smart_data_sub3 = component.subscribe(Condition(false, SecondUnit.get_int_unit(), period));

    std::this_thread::sleep_for(std::chrono::seconds(CYCLE_DURATION_SECONDS));

    // Verifica se este componente é líder
    if (car.prot.amILeader()) {
      std::cout << "[TEST] " << get_timestamp() << " Car " << car.label 
                << " Component " << component_id << " (PID: " << getpid() 
                << ") is LEADER" << std::endl;
    } else {
      std::cout << "[TEST] " << get_timestamp() << " Car " << car.label 
                << " Component " << component_id << " (PID: " << getpid() 
                << ") is FOLLOWER" << std::endl;
    }

    std::thread publisher_thread([&]() {
      for (int msg = 0; msg < NUM_MESSAGES_PER_COMPONENT; ++msg) {
        try {
          // Obtém dados do transducer
          std::vector<std::byte> data(MeterUnit.get_value_size_bytes());
          transducer1.get_data(data.data());

          // Publica dados usando smart_data
          auto timestamp = get_timestamp();
          smart_data_pub.publish();
          
          std::cout << "[TEST] " << timestamp << " Car " << car.label 
                    << " Component " << component_id << " published data " 
                    << msg + 1 << "/" << NUM_MESSAGES_PER_COMPONENT 
                    << " (data size: " << data.size() << " bytes)" << std::endl;
                    
          std::this_thread::sleep_for(std::chrono::milliseconds(800));
        } catch (const std::exception& e) {
          std::cout << "[TEST] " << get_timestamp() << " Car " << car.label 
                    << " Component " << component_id 
                    << " exception publishing: " << e.what() << std::endl;
        }
      }
    });

    std::thread subscriber_thread([&]() {
      for (int msg = 0; msg < NUM_MESSAGES_PER_COMPONENT * 2; ++msg) {
        try {
          Message message1 = Message(sizeof(SmartData<Communicator, Condition>::Header) +
                                    MeterUnit.get_value_size_bytes());
          message1.getControl()->setType(Control::Type::PUBLISH);

          if (smart_data_sub1.receive(&message1)) {
            auto timestamp = get_timestamp();
            std::cout << "[TEST] " << timestamp << " Car " << car.label 
                      << " Component " << component_id 
                      << " received Meter data " << msg + 1 << " (size: " 
                      << message1.size() << " bytes)" << std::endl;
          }

          Message message2 = Message(sizeof(SmartData<Communicator, Condition>::Header) +
                                    KilogramUnit.get_value_size_bytes());
          message2.getControl()->setType(Control::Type::PUBLISH);

          if (smart_data_sub2.receive(&message2)) {
            auto timestamp = get_timestamp();
            std::cout << "[TEST] " << timestamp << " Car " << car.label 
                      << " Component " << component_id 
                      << " received Kilogram data " << msg + 1 << " (size: " 
                      << message2.size() << " bytes)" << std::endl;
          }

          Message message3 = Message(sizeof(SmartData<Communicator, Condition>::Header) +
                                    SecondUnit.get_value_size_bytes());
          message3.getControl()->setType(Control::Type::PUBLISH);

          if (smart_data_sub3.receive(&message3)) {
            auto timestamp = get_timestamp();
            std::cout << "[TEST] " << timestamp << " Car " << car.label 
                      << " Component " << component_id 
                      << " received Second data " << msg + 1 << " (size: " 
                      << message3.size() << " bytes)" << std::endl;
          }

          std::this_thread::sleep_for(std::chrono::milliseconds(400));
        } catch (const std::exception& e) {
          std::cout << "[TEST] " << get_timestamp() << " Car " << car.label 
                    << " Component " << component_id 
                    << " exception receiving: " << e.what() << std::endl;
        }
      }
    });
    publisher_thread.join();
    subscriber_thread.join();
  } else {
    std::cout << "[TEST] " << get_timestamp() << " Parent process monitoring " 
              << NUM_CARS * COMPONENTS_PER_CAR << " components across " 
              << NUM_CARS << " cars" << std::endl;

    for (int i = 0; i < NUM_CARS * COMPONENTS_PER_CAR; ++i) {
      int status;
      pid_t finished_pid = wait(&status);
    }
  }

  return 0;
}
// g++ -std=c++20 -g tests/src/e4/e4_components_many_cars.cc -Iinclude -Itests/include -o many_cars_test src/ethernet.cc src/utils.cc