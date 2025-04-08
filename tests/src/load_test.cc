#include <iostream>
#include <chrono>
#include <thread>
#include <vector>
#include <mutex>

// Inclua os headers da API conforme sua implementação.
#include "communicator.hh"
#include "protocol.hh"
#include "engine.hh"
#include "nic.hh"

const int NUM_REMOTES = 25;            // 25 comunicadores remotos (carros)
const int NUM_MESSAGES_PER_REMOTE = 100; // Número de mensagens por comunicador

std::mutex consoleMutex;

// Exemplo de mensagem de teste: além do payload padrão, inclui um identificador do remetente.
class TestMessage : public Message {
public:
    int senderId; // ID do carro remoto que envia a mensagem

    TestMessage() : senderId(-1) {
        for (size_t i = 0; i < sizeof(payload); i++)
            payload[i] = 'A';
    }
    virtual size_t size() const override { return sizeof(payload); }
    virtual void* data() override { return static_cast<void*>(payload); }
private:
    char payload[256];
};

// Para facilitar a leitura, definimos aliases para os tipos da API.
using EngineT = Engine;
using NICT = NIC<EngineT>;
using ProtocolT = Protocol<NICT>;
using CommunicatorT = Communicator<ProtocolT>;

int main() {
    // Inicializa os componentes básicos da API.
    EngineT engine;
    NICT nic;
    // (Se necessário, inicialize configurações específicas da NIC aqui.)
    ProtocolT protocol(&nic);

    // Cria o comunicador central (o carro que receberá as mensagens) com um endereço fixo (por exemplo, porta 0).
    ProtocolT::Address centralAddr(nic.address(), 0);
    CommunicatorT centralComm(&protocol, centralAddr);

    // Vetores para armazenar os comunicadores remotos e as threads de teste.
    std::vector<CommunicatorT*> remoteComms;
    std::vector<std::thread> remoteThreads;

    // Cria os 25 comunicadores remotos com endereços distintos (por exemplo, portas 1 a 25)
    for (int i = 1; i <= NUM_REMOTES; i++) {
        ProtocolT::Address remoteAddr(nic.address(), i);
        auto comm = new CommunicatorT(&protocol, remoteAddr);
        remoteComms.push_back(comm);

        // Cada carro remoto executa em uma thread: envia NUM_MESSAGES_PER_REMOTE mensagens e aguarda respostas.
        remoteThreads.push_back(std::thread([comm, i, centralAddr]() {
            for (int j = 0; j < NUM_MESSAGES_PER_REMOTE; j++) {
                TestMessage msg;
                msg.senderId = i;  // Indica qual carro está enviando.
                auto t_start = std::chrono::high_resolution_clock::now();

                if (!comm->send(&msg)) {
                    std::lock_guard<std::mutex> lock(consoleMutex);
                    std::cerr << "[Remote " << i << "] Falha no envio da mensagem " << j << "\n";
                    continue;
                }

                TestMessage reply;
                if (!comm->receive(&reply)) {
                    std::lock_guard<std::mutex> lock(consoleMutex);
                    std::cerr << "[Remote " << i << "] Falha no recebimento da resposta " << j << "\n";
                    continue;
                }
                auto t_end = std::chrono::high_resolution_clock::now();
                auto latency = std::chrono::duration_cast<std::chrono::microseconds>(t_end - t_start).count();
                std::lock_guard<std::mutex> lock(consoleMutex);
                std::cout << "[Remote " << i << "] Mensagem " << j << " - Latência: " << latency << " µs\n";
            }
        }));
    }

    // Thread central: recebe todas as mensagens dos remotos e responde para cada uma.
    // Supondo que a API de receive permite que o TestMessage recebido contenha o senderId.
    // Para enviar a resposta direcionada, usamos a função estática ProtocolT::send(), que recebe
    // o endereço de origem (central) e o destino (construído com base no senderId).
    std::thread centralThread([&centralComm, &protocol, &nic]() {
        int totalMessages = NUM_REMOTES * NUM_MESSAGES_PER_REMOTE;
        for (int k = 0; k < totalMessages; k++) {
            TestMessage req;
            if (!centralComm.receive(&req)) {
                std::lock_guard<std::mutex> lock(consoleMutex);
                std::cerr << "[Central] Falha no recebimento de uma mensagem\n";
                continue;
            }
            int senderId = req.senderId;
            TestMessage rep;
            // Constrói o endereço do carro remoto a partir do ID recebido.
            ProtocolT::Address remoteAddr(nic.address(), senderId);
            // Envia a resposta direcionada: o método estático envia usando o endereço da central como origem.
            int ret = ProtocolT::send(centralComm.address(), remoteAddr, rep.data(), rep.size());
            if (ret <= 0) {
                std::lock_guard<std::mutex> lock(consoleMutex);
                std::cerr << "[Central] Falha no envio da resposta para o Remote " << senderId << "\n";
            }
        }
    });

    // Aguarda o término de todas as threads remotas.
    for (auto &t : remoteThreads) {
        if (t.joinable())
            t.join();
    }
    if (centralThread.joinable())
        centralThread.join();

    std::cout << "Teste de carga concluído.\n";

    // Libera os comunicadores remotos.
    for (auto comm : remoteComms)
        delete comm;

    return 0;
}


// Compilação:
//g++ -std=c++11 load_test.cc -I../../include -o load_test -lpthread 
