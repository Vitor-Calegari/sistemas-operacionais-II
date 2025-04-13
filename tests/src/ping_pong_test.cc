#include <iostream>
#include <unistd.h>
#include <sys/wait.h>
#include <cstdlib>
#include <semaphore.h>
#include <sys/mman.h>
#include <future>
#include "communicator.hh"
#include "protocol.hh"
#include "engine.hh"
#include "nic.hh"
#include "message.hh"  // classe Message que espera o tamanho da mensagem

#ifndef INTERFACE_NAME
#define INTERFACE_NAME "enxf8e43bf0c430"
#endif

// Constantes globais para o teste de carga
const int num_messages = 10000;
const std::size_t MESSAGE_SIZE = 256; 
const int timeout_sec = 5;

struct msg_struct {
    int counter = 0;
};

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
        Protocol<NIC<Engine>>::Address addr = Protocol<NIC<Engine>>::Address(Ethernet::ZERO, 10);
        // Por exemplo: addr = typename Protocol<NIC<Engine>>::Address(getpid());

        Communicator<Protocol<NIC<Engine>>> communicator(prot, addr);

        // Aguarda até que o processo pai libere o semaphore
        sem_wait(semaphore);

        Message send_msg(MESSAGE_SIZE);
        struct msg_struct ms;
        ms.counter = 0;
        std::memcpy(send_msg.data(), &ms, sizeof(ms));

        for (int i = 0; i < num_messages; i++) {
            // Envia
            if (!communicator.send(&send_msg)) {
                std::cout << "Inspect Proc(" << std::dec << getpid() <<"): Error sending msg " << i << std::endl;
            }
            // Recebe
            Message recv_msg(MESSAGE_SIZE);  // Cria mensagem do tamanho definido
            if (!communicator.receive(&recv_msg)) {
                std::cout << "Inspect Proc(" << std::dec << getpid() <<"): Error sending msg " << i << std::endl;
            }
            // Verifica se contou
            msg_struct * resp = reinterpret_cast<struct msg_struct*>(recv_msg.data());
            if (resp->counter == reinterpret_cast<struct msg_struct*>(send_msg.data())->counter + 1) {
                std::cout << "Inspect Proc(" << std::dec << getpid() <<"): Received " << resp->counter << std::endl;
                reinterpret_cast<struct msg_struct*>(send_msg.data())->counter = resp->counter;
            } else {
                std::cout << "Count failed." << std::endl;
                exit(1);
            }
        }
        exit(0);  // Conclui com sucesso no processo-filho
    } else {
        // Processo Pai
        NIC<Engine> nic(INTERFACE_NAME);
        auto prot = Protocol<NIC<Engine>>::getInstance(&nic);
        Protocol<NIC<Engine>>::Address addr = Protocol<NIC<Engine>>::Address(Ethernet::ZERO, 10);
        Communicator<Protocol<NIC<Engine>>> communicator(prot, addr);

        Message send_msg(MESSAGE_SIZE);
        struct msg_struct ms;
        ms.counter = 0;
        std::memcpy(send_msg.data(), &ms, sizeof(ms));

        sem_post(semaphore);
    
        int recvd = 0;
        for (int i = 0; i < num_messages; i++) {
            // Recebe
            Message recv_msg(MESSAGE_SIZE);  // Cria mensagem do tamanho definido
            if (!communicator.receive(&recv_msg)) {
                std::cout << "Counter Proc(" << std::dec << getpid() <<"): Error sending msg " << i << std::endl;
            }
            
            recvd = reinterpret_cast<struct msg_struct*>(recv_msg.data())->counter;
            std::cout << "Counter Proc(" << std::dec << getpid() <<"): Adding 1 to " << recvd << std::endl;
            reinterpret_cast<struct msg_struct*>(recv_msg.data())->counter++;

            // Envia
            if (!communicator.send(&recv_msg)) {
                std::cout << "Counter Proc(" << std::dec << getpid() <<"): Error sending msg " << i << std::endl;
            }
        }
    }

    // Processo pai aguarda todos os filhos finalizarem
    int status;
    while (wait(&status) > 0);

    std::cout << "Teste ping pong concluído" << std::endl;
    
    // Libera os recursos do semaphore
    sem_destroy(semaphore);
    munmap(semaphore, sizeof(sem_t));

    return 0;
}
