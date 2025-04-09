#include <iostream>
#include <unistd.h>
#include <sys/wait.h>
#include <cstdlib>
#include "communicator.hh"
#include "protocol.hh"
#include "engine.hh"
#include "nic.hh"
#include "message.hh"  // classe Message que espera o tamanho da mensagem

// Constantes globais para o teste de carga
const int num_communicators = 25;
const int num_messages_per_comm = 100;
const std::size_t MESSAGE_SIZE = 256; 

int main() {
    for (int i = 0; i < num_communicators; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            std::cerr << "Erro ao criar processo" << std::endl;
            exit(1);
        }
        if (pid == 0) {  
            // Código do processo-filho
            NIC<Engine> nic("lo");
            auto prot = Protocol<NIC<Engine>>::getInstance(&nic);
            
            // Crie um endereço único para cada processo – ajuste conforme sua implementação
            typename Protocol<NIC<Engine>>::Address addr;  
            // Exemplo: addr = typename Protocol<NIC<Engine>>::Address(getpid());
            
            Communicator<Protocol<NIC<Engine>>> communicator(prot, addr);
            
            for (int j = 0; j < num_messages_per_comm; j++) {
                Message msg(MESSAGE_SIZE);  // Passe o tamanho da mensagem
                // Preencher a mensagem com os dados necessários...
                if (!communicator.send(&msg)) {
                    std::cerr << "Erro no envio da mensagem no processo " << getpid() << std::endl;
                    exit(1);
                }
                if (!communicator.receive(&msg)) {
                    std::cerr << "Erro no recebimento da mensagem no processo " << getpid() << std::endl;
                    exit(1);
                }
            }
            exit(0);  // Sucesso no processo-filho
        }
    }
    
    // Processo pai aguarda todos os filhos
    int status;
    bool sucesso = true;
    for (int i = 0; i < num_communicators; i++) {
        wait(&status);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            sucesso = false;
        }
    }
    
    if (sucesso)
        std::cout << "Teste de carga concluído com sucesso." << std::endl;
    else
        std::cerr << "Falha no teste de carga." << std::endl;
    
    return 0;
}
