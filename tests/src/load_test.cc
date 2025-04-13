#include <iostream>
#include <unistd.h>
#include <sys/wait.h>
#include <cstdlib>
#include <semaphore.h>
#include <sys/mman.h>
#include "communicator.hh"
#include "protocol.hh"
#include "engine.hh"
#include "nic.hh"
#include "message.hh"  // classe Message que espera o tamanho da mensagem

#ifndef INTERFACE_NAME
#define INTERFACE_NAME "lo"
#endif

// Constantes globais para o teste de carga
const int num_communicators = 15;
const int num_messages_per_comm = 100;
const std::size_t MESSAGE_SIZE = 256; 

const auto INTERFACE_NAME = "lo";

int main() {
    // Cria um semaphore compartilhado entre processos
    sem_t *semaphore = static_cast<sem_t*>(
        mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE,
             MAP_SHARED | MAP_ANONYMOUS, -1, 0)
    );
    if (semaphore == MAP_FAILED) {
        std::cerr << "Erro ao criar o semaphore." << std::endl;
        exit(1);
    }
    sem_init(semaphore, 1, 0);  // 0 = inicializa sem permissão para prosseguir

    for (int i = 0; i < num_communicators; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            std::cerr << "Erro ao criar processo" << std::endl;
            exit(1);
        }
        if (pid == 0) {  
            // Código do processo-filho
            NIC<Engine> nic(INTERFACE_NAME);
            auto prot = Protocol<NIC<Engine>>::getInstance(&nic);

            // Crie um endereço único para cada processo – ajuste conforme sua implementação
            typename Protocol<NIC<Engine>>::Address addr;  
            // Por exemplo: addr = typename Protocol<NIC<Engine>>::Address(getpid());

            Communicator<Protocol<NIC<Engine>>> communicator(prot, addr);

            // Aguarda até que o processo pai libere o semaphore
            sem_wait(semaphore);

            int j = 0;
            while(j < num_messages_per_comm) {
                // Envia mensagens
                Message msg(MESSAGE_SIZE);  // Cria mensagem do tamanho definido
                if (communicator.send(&msg)) {
                    j++;
                }
            }
            exit(0);  // Conclui com sucesso no processo-filho
        }
    }

    // Processo pai - Cria seu próprio comunicador
    NIC<Engine> nic(INTERFACE_NAME);
    auto prot = Protocol<NIC<Engine>>::getInstance(&nic);
    typename Protocol<NIC<Engine>>::Address addr;
    Communicator<Protocol<NIC<Engine>>> communicator(prot, addr);
    Message msg(MESSAGE_SIZE);

    // Libera o semaphore para que todos os filhos possam prosseguir com os envios
    for (int i = 0; i < num_communicators; i++) {
        sem_post(semaphore);
    }

    for (int j = 0; j < num_communicators * num_messages_per_comm; j++){
        //std::cout << j << std::endl;
        if (!communicator.receive(&msg)) {
            std::cerr << "Erro no recebimento da mensagem no processo " 
                  << getpid() << std::endl;
            exit(1);
        }}

    // Processo pai aguarda todos os filhos finalizarem
    int status;
    while (wait(&status) > 0);

    std::cout << "Teste de carga concluído com sucesso." << std::endl;
    
    // Libera os recursos do semaphore
    sem_destroy(semaphore);
    munmap(semaphore, sizeof(sem_t));

    return 0;
}

//g++ -std=c++20 tests/src/load_test.cc -Iinclude -o load_test src/engine.cc src/ethernet.cc src/message.cc src/utils.cc
// ./load_test