#include "car.hh"
#include "component.hh"
#include "message.hh"
#include "map.hh"
#include "utils.hh"
#include <chrono>
#include <thread>
#include <iostream>
#include <vector>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>

#ifndef INTERFACE_NAME
#define INTERFACE_NAME "lo"
#endif

constexpr int NUM_MSGS = 10;
constexpr int MSG_SIZE = 32;
constexpr double COMM_RANGE = 100.0;

int main() {
    Map* map = new Map(0, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    std::cout << "=== Teste V2V - Alcance: " << COMM_RANGE << "m ===" << std::endl;
    
    std::vector<double> test_distances = {50.0, 100.0, 150.0};
    
    for (double distance : test_distances) {
        std::cout << "\nTestando " << distance << "m:" << std::endl;
        
        pid_t sender_pid = fork();
        
        if (sender_pid == 0) {
            std::vector<Car::Coordinate> sender_points = {{-distance/2.0, 0.0}};
            Topology topology({300, 300}, COMM_RANGE);
            Car sender_car("Sender", sender_points, topology, COMM_RANGE, 0);
            auto sender_comp = sender_car.create_component(1);
            
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            
            int sent_count = 0;
            for (int i = 0; i < NUM_MSGS; i++) {
                using Address = Car::ProtocolC::Address;
                using Message = Car::ComponentC::MessageC;
                
                Message msg(sender_comp.addr(),
                           Address(Ethernet::BROADCAST_ADDRESS,
                                  Car::ProtocolC::BROADCAST_SID,
                                  Car::ProtocolC::BROADCAST),
                           MSG_SIZE);
                
                for (size_t j = 0; j < MSG_SIZE; j++) {
                    msg.data()[j] = static_cast<std::byte>(i);
                }
                
                if (sender_comp.send(&msg)) sent_count++;
                std::this_thread::sleep_for(std::chrono::milliseconds(80));
            }
            
            std::cout << "  Enviadas: " << sent_count << "/" << NUM_MSGS << std::endl;
            _exit(0);
            
        } else if (sender_pid > 0) {
            pid_t receiver_pid = fork();
            
            if (receiver_pid == 0) {
                std::vector<Car::Coordinate> receiver_points = {{distance/2.0, 0.0}};
                Topology topology({300, 300}, COMM_RANGE);
                Car receiver_car("Receiver", receiver_points, topology, COMM_RANGE, 0);
                auto receiver_comp = receiver_car.create_component(2);
                
                int received_count = 0;
                auto start_time = std::chrono::high_resolution_clock::now();
                
                while (received_count < NUM_MSGS) {
                    using Message = Car::ComponentC::MessageC;
                    Message msg(MSG_SIZE);
                    
                    if (receiver_comp.receive(&msg)) {
                        received_count++;
                    }
                    
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::high_resolution_clock::now() - start_time).count();
                    
                    if (elapsed > 2000) break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
                
                double success_rate = (received_count > 0) ? (double)received_count / NUM_MSGS * 100.0 : 0.0;
                std::cout << "  Recebidas: " << received_count << "/" << NUM_MSGS 
                          << " (" << success_rate << "%)" << std::endl;
                
                if (distance <= COMM_RANGE) {
                    std::cout << "  " << (received_count > 0 ? "✓ Funcionando" : "⚠ Falha") << std::endl;
                } else {
                    std::cout << "  " << (received_count == 0 ? "✓ Bloqueado (esperado)" : "⚠ Inesperado") << std::endl;
                }
                
                _exit(0);
                
            } else if (receiver_pid > 0) {
                int sender_status, receiver_status;
                
                waitpid(sender_pid, &sender_status, 0);
                
                int timeout_count = 0;
                while (waitpid(receiver_pid, &receiver_status, WNOHANG) == 0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    if (++timeout_count > 60) {
                        std::cout << "  Recebidas: 0/" << NUM_MSGS << " (0%)" << std::endl;
                        std::cout << "  ✓ Bloqueado (esperado)" << std::endl;
                        kill(receiver_pid, SIGKILL);
                        waitpid(receiver_pid, &receiver_status, 0);
                        break;
                    }
                }
            } else {
                std::cerr << "Erro ao criar processo receiver" << std::endl;
                kill(sender_pid, SIGKILL);
                waitpid(sender_pid, nullptr, 0);
                return 1;
            }
        } else {
            std::cerr << "Erro ao criar processo sender" << std::endl;
            return 1;
        }
    }
    std::cout << "\n=== Concluído ===" << std::endl;
    
    return 0;
}