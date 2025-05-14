#include "communicator.hh"
#include "engine.hh"
#include "message.hh"
#include "nic.hh"
#include "protocol.hh"
#include "shared_engine.hh"
#include <iostream>
#include <thread>
#include <vector>
#include <mutex>
#include <memory>  // Para std::unique_ptr
#include <unistd.h>
#include <sys/wait.h>
#include <string>
#include <cstring>
#include <chrono>

// Interface padrão se não for especificada
#ifndef INTERFACE_NAME
#define INTERFACE_NAME "lo"
#endif

// Função componente simplificada - apenas inicializa a estrutura sem lógica de comunicação
void componente_thread(int carro_id, int comp_id, 
                       const std::string& interface_name, std::mutex& stdout_mtx) {
    // Definições de tipos do protocolo
    using Buffer = Buffer<Ethernet::Frame>;
    using SocketNIC = NIC<Engine<Buffer>>;
    using SharedMemNIC = NIC<SharedEngine<Buffer>>;
    using Protocol = Protocol<SocketNIC, SharedMemNIC>;
    using Message = Message<Protocol::Address>;
    using Communicator = Communicator<Protocol, Message>;
    
    // Utilizando smart pointers para gestão automática de memória
    std::unique_ptr<SocketNIC> rsnic(new SocketNIC(interface_name.c_str()));
    std::unique_ptr<SharedMemNIC> smnic(new SharedMemNIC(interface_name.c_str()));
    Protocol& prot = Protocol::getInstance(rsnic.get(), smnic.get(), getpid());
    
    // Criação do comunicador para este componente
    Communicator communicator(&prot, comp_id);
    
    {
        std::lock_guard<std::mutex> lock(stdout_mtx);
        std::cout << "[Carro " << carro_id << "] Componente " << comp_id 
                << " iniciado (PID=" << getpid() << ", Thread: " 
                << std::this_thread::get_id() << ")" << std::endl;
    }
    
    // Aqui o componente estaria pronto para ser usado pelos testes
    // Simula um tempo de vida do componente - em uma aplicação real
    // esse tempo seria determinado pela lógica do teste ou por sinais externos
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    {
        std::lock_guard<std::mutex> lock(stdout_mtx);
        std::cout << "[Carro " << carro_id << "] Componente " << comp_id 
                << " finalizado" << std::endl;
    }
    
    // Smart pointers cuidarão da limpeza automaticamente
}

// Função que inicializa um carro como processo com componentes como threads
void iniciar_carro(int carro_id, int num_componentes, const std::string& interface_name) {
    std::mutex stdout_mtx;
    std::cout << "=== Carro " << carro_id << " iniciado (PID=" << getpid() << ") ===" << std::endl;
    
    // Cria threads para cada componente
    std::vector<std::thread> componentes;
    for (int i = 0; i < num_componentes; i++) {
        componentes.emplace_back(componente_thread, carro_id, i, interface_name, std::ref(stdout_mtx));
    }
    
    // Aguarda as threads terminarem
    for (auto& thread : componentes) {
        thread.join();
    }
    
    std::cout << "=== Carro " << carro_id << " finalizado (PID=" << getpid() << ") ===" << std::endl;
    exit(0); // Termina o processo do carro
}

// Função principal
int main(int argc, char* argv[]) {
    // Parâmetros configuráveis pela linha de comando
    int num_carros = 2;
    int num_componentes = 3;
    std::string interface = INTERFACE_NAME;
    
    // Lê parâmetros da linha de comando
    if (argc > 1) num_carros = std::stoi(argv[1]);
    if (argc > 2) num_componentes = std::stoi(argv[2]);
    if (argc > 3) interface = argv[3];
    
    std::cout << "Iniciando simulação com " << num_carros << " carros, cada um com " 
              << num_componentes << " componentes (interface: " << interface << ")" << std::endl;
    
    // Cria processos para cada carro
    std::vector<pid_t> carro_pids;
    for (int i = 0; i < num_carros; i++) {
        pid_t pid = fork();
        
        if (pid < 0) {
            std::cerr << "Erro ao criar processo para carro " << i+1 << std::endl;
            continue;
        }
        
        if (pid == 0) {
            // Processo filho - executa o carro
            iniciar_carro(i+1, num_componentes, interface);
            // O filho termina aqui com exit(0) dentro da função iniciar_carro
        } else {
            // Processo pai - armazena PID do carro
            carro_pids.push_back(pid);
        }
    }
    
    // Processo pai aguarda todos os carros terminarem
    for (pid_t pid : carro_pids) {
        int status;
        waitpid(pid, &status, 0);
    }
    
    std::cout << "Simulação finalizada. Todos os carros terminaram." << std::endl;
    
    return 0;
}