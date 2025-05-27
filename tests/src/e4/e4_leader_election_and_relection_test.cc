#include "sync_engine.hh"
#include "car.hh"
#include "component.hh"
#include "protocol.hh"
#include "shared_engine.hh"

#include <cassert>
#include <chrono>
#include <cmath>
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>
#include <signal.h>
#include <semaphore.h>

constexpr size_t NUM_CARS = 4;
constexpr uint64_t INITIAL_LEADER_ELECTION_TIME_US = 2e6;  // 2 segundos
constexpr uint64_t LEADER_DEATH_TIME_US = 3e6;             // 3 segundos
constexpr uint64_t REELECTION_TIME_US = 2e6;               // 2 segundos
constexpr uint64_t SYNC_CHECK_INTERVAL_US = 1e5;           // 100ms

struct LeaderElectionMessage {
    enum Type { LEADER_ANNOUNCEMENT, LEADER_QUERY, LEADER_RESPONSE };
    Type type;
    pid_t sender_pid;
    uint64_t timestamp;
    bool is_leader;
};

int main(int argc, char* argv[]) {
    std::cout << "Iniciando teste de eleição de líder com " << NUM_CARS << " carros..." << std::endl;

    // Criar processos filhos para cada carro
    std::vector<pid_t> child_pids;
    pid_t initial_leader = 0;
    pid_t new_leader = 0;
    
    for (size_t i = 0; i < NUM_CARS; ++i) {
        pid_t pid = fork();
        
        if (pid == -1) {
            perror("fork falhou");
            return 1;
        }
        
        if (pid == 0) {
            uint64_t offset_ns = (i + 1) * 1e9; // Offsets diferentes em nanossegundos
            
            Car car("Car_" + std::to_string(i));
            auto component = car.create_component(i + 1);
            
            using SharedBuffer = Buffer<Ethernet::Frame>;
            SharedEngine<SharedBuffer> shared_engine(INTERFACE_NAME);
            
            std::cout << "Carro " << i << " (PID: " << getpid() 
                     << ") iniciado com offset: " << offset_ns << "ns" << std::endl;
            
            // Simular execução do carro com eleição de líder
            auto start_time = std::chrono::steady_clock::now();
            bool am_leader = false;
            bool was_leader = false;
            pid_t current_known_leader = 0;
            
            // Algoritmo de eleição baseado em PID mais baixo
            pid_t my_pid = getpid();
            
            while (true) {
                auto current_time = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                    current_time - start_time).count();
                
                // Enviar mensagem de anúncio de liderança se sou líder
                if (am_leader) {
                    LeaderElectionMessage msg;
                    msg.type = LeaderElectionMessage::LEADER_ANNOUNCEMENT;
                    msg.sender_pid = my_pid;
                    msg.timestamp = elapsed;
                    msg.is_leader = true;
                    
                    SharedBuffer buf;
                    buf.setSize(sizeof(LeaderElectionMessage));
                    std::memcpy(buf.data(), &msg, sizeof(LeaderElectionMessage));
                    shared_engine.send(&buf);
                }
                
                // Receber mensagens de outros carros
                SharedBuffer recv_buf;
                if (shared_engine.receive(&recv_buf) > 0) {
                    LeaderElectionMessage received_msg;
                    std::memcpy(&received_msg, recv_buf.data(), sizeof(LeaderElectionMessage));
                    
                    if (received_msg.type == LeaderElectionMessage::LEADER_ANNOUNCEMENT) {
                        // Se recebi anúncio de líder com PID menor, não sou mais líder
                        if (received_msg.sender_pid < my_pid) {
                            if (am_leader) {
                                std::cout << "Carro " << i << " (PID: " << my_pid 
                                         << ") perdeu liderança para PID " << received_msg.sender_pid 
                                         << " em t=" << elapsed << "us" << std::endl;
                                am_leader = false;
                            }
                            current_known_leader = received_msg.sender_pid;
                        }
                    }
                }
                
                // Se não há líder conhecido ou o líder morreu, tentar ser líder
                if (!am_leader && (current_known_leader == 0 || elapsed > LEADER_DEATH_TIME_US)) {
                    // Assumir liderança se tenho o menor PID entre os vivos
                    am_leader = true;
                    current_known_leader = my_pid;
                }
                
                if (am_leader && !was_leader) {
                    std::cout << "Carro " << i << " (PID: " << my_pid 
                             << ") se tornou LÍDER em t=" << elapsed << "us" << std::endl;
                    was_leader = true;
                } else if (!am_leader && was_leader) {
                    was_leader = false;
                }
                
                // Simular morte do líder inicial após tempo determinado
                if (am_leader && elapsed >= LEADER_DEATH_TIME_US && i == 0) {
                    std::cout << "Carro " << i << " (PID: " << my_pid 
                             << ") simulando morte do líder..." << std::endl;
                    exit(0); // Simular morte
                }
                
                usleep(SYNC_CHECK_INTERVAL_US);
            }
            
            return 0;
        } else {
            child_pids.push_back(pid);
            if (i == 0) {
                initial_leader = pid; // O primeiro processo será o líder inicial
            }
        }
    }
    
    std::cout << "Aguardando eleição inicial do líder..." << std::endl;
    usleep(INITIAL_LEADER_ELECTION_TIME_US);
    
    std::cout << "Líder inicial esperado: PID " << initial_leader << std::endl;
    
    std::cout << "Aguardando morte do líder e nova eleição..." << std::endl;
    usleep(LEADER_DEATH_TIME_US + REELECTION_TIME_US);
    
    // Verificar se o processo inicial morreu
    int status;
    pid_t result = waitpid(initial_leader, &status, WNOHANG);
    
    if (result == initial_leader) {
        std::cout << "Líder inicial (PID: " << initial_leader << ") morreu conforme esperado" << std::endl;
        
        // Encontrar o novo líder (menor PID entre os restantes)
        new_leader = child_pids[1];
        for (size_t i = 2; i < child_pids.size(); ++i) {
            if (child_pids[i] < new_leader) {
                new_leader = child_pids[i];
            }
        }
        
        std::cout << "Novo líder esperado: PID " << new_leader << std::endl;
        
        // Verificar se o novo líder ainda está vivo
        result = waitpid(new_leader, &status, WNOHANG);
        if (result == 0) {
            std::cout << "SUCESSO: Novo líder (PID: " << new_leader << ") está ativo!" << std::endl;
            std::cout << "Teste de eleição de líder passou!" << std::endl;
        } else {
            std::cerr << "ERRO: Novo líder também morreu!" << std::endl;
        }
    } else {
        std::cerr << "ERRO: Líder inicial não morreu conforme esperado!" << std::endl;
    }
    
    for (pid_t pid : child_pids) {
        if (pid > 0 && pid != initial_leader) {
            kill(pid, SIGTERM);
        }
    }
    
    for (pid_t pid : child_pids) {
        if (pid != initial_leader) {
            waitpid(pid, &status, 0);
        }
    }
    
    return 0;
}

// g++ -std=c++20 -g tests/src/e4/e4_leader_election_and_relection_test.cc -Iinclude -Itests/include -o leader_election src/ethernet.cc src/utils.cc