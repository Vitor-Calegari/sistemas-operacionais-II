#include <iostream>
#include <chrono>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <algorithm>  // para std::fill

#include "communicator.hh"
#include "protocol.hh"
#include "engine.hh"
#include "nic.hh"

// Constantes globais
const int num_remotes = 25;
const int num_messages_per_remote = 100;

// Variáveis globais para sincronização
std::mutex mtx;
std::condition_variable cv;
std::atomic<int> messages_received = 0;

// Função para simular o envio de mensagens por um comunicador
void communicator_thread(int id, int num_messages, std::vector<std::unique_ptr<Message>>& received_messages) {
    for (int i = 0; i < num_messages; ++i) {
        // Cria uma mensagem padrão
        auto msg = std::make_unique<Message>(256); // Tamanho da mensagem: 256 bytes
        char* data = reinterpret_cast<char*>(msg->data());
        std::fill(data, data + msg->size(), 'A'); // Preenche o payload com 'A'

        // Adiciona o ID do remetente no início do payload
        data[0] = static_cast<char>(id);

        // Adiciona a mensagem à lista compartilhada
        {
            std::lock_guard<std::mutex> lock(mtx);
            received_messages.push_back(std::move(msg));
            messages_received++;
        }

        // Notifica o receptor
        cv.notify_all();
    }
}

// Função para processar mensagens recebidas
void receiver_thread(int total_messages, std::vector<std::unique_ptr<Message>>& received_messages) {
    int received_count = 0;

    while (received_count < total_messages) {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [&]() { return messages_received > received_count; });

        // Processa a mensagem recebida
        const auto& msg = received_messages[received_count];
        const char* data = reinterpret_cast<const char*>(msg->data());
        int senderId = static_cast<int>(data[0]); // Extrai o ID do remetente do payload
        std::cout << "Message received from sender " << senderId << std::endl;

        received_count++;
    }
}

// Função principal
int main() {
    const int total_messages = num_remotes * num_messages_per_remote;
    std::vector<std::unique_ptr<Message>> received_messages;
    received_messages.reserve(total_messages);  // Reserva espaço para evitar realocações

    // Inicia a thread do receptor
    std::thread receiver(receiver_thread, total_messages, std::ref(received_messages));

    // Inicia as threads dos comunicadores
    std::vector<std::thread> communicators;
    for (int i = 0; i < num_remotes; ++i) {
        communicators.emplace_back(communicator_thread, i, num_messages_per_remote, std::ref(received_messages));
    }

    // Aguarda todas as threads terminarem
    for (auto& communicator : communicators) {
        communicator.join();
    }
    receiver.join();

    std::cout << "All messages received successfully!" << std::endl;
    return 0;
}

//g++ -std=c++20 tests/src/load_test.cc -Iinclude -o load_test src/engine.cc src/ethernet.cc src/message.cc src/utils.cc -lpthread
// ./load_test
