#include "car.hh"
#include "cond.hh"
#include "smart_data.hh"
#include "transducer.hh"
#include "smart_unit.hh"
#include "message.hh"
#include <chrono>
#include <cmath>
#include <iostream>
#include <unistd.h>
#include <sys/wait.h>
#include <cassert>
#include <vector>

constexpr size_t TOTAL_MESSAGES_TO_SEND = 20; // Publisher enviará mais mensagens
constexpr size_t SUBSCRIBE_AFTER_N_MESSAGES = 5; // Subscriber conecta após estas mensagens
constexpr size_t MESSAGES_TO_RECEIVE_BY_SUBSCRIBER = TOTAL_MESSAGES_TO_SEND - SUBSCRIBE_AFTER_N_MESSAGES;
constexpr uint32_t DEFAULT_PERIOD_US = 10000;  // 10 ms
constexpr double TOLERANCE = 0.15;              // 15% de tolerância para temporização


int main(int argc, char* argv[]) {
    uint32_t period_us = (argc > 1) ? std::stoul(argv[1]) : DEFAULT_PERIOD_US;

    Car car = Car();
    Transducer<SmartUnit(SmartUnit::SIUnit::M)> transd(0, 255);
    Condition cond(true, SmartUnit(SmartUnit::SIUnit::M).get_int_unit(), period_us);

    auto component_pub = car.create_component(1);
    auto pub_sd = component_pub.template register_publisher<Condition, Transducer<SmartUnit(SmartUnit::SIUnit::M)>>(&transd, cond);

    pid_t pid = fork();
    if (pid == -1) {
        perror("fork falhou");
        return 1;
    }

    if (pid == 0) {
        for (size_t i = 0; i < TOTAL_MESSAGES_TO_SEND; ++i) {
            using Message = Message<Car::Protocol::Address>;
            Message message = Message(8 + SmartUnit(SmartUnit::SIUnit::M).get_value_size_bytes(), Message::Type::PUBLISH);
            usleep(period_us);
        }
        return 0;
    }
    usleep(period_us * SUBSCRIBE_AFTER_N_MESSAGES + (period_us / 2)); // Adiciona um pequeno buffer

    std::cout << "Subscriber se conectando após " << SUBSCRIBE_AFTER_N_MESSAGES << " mensagens do publisher..." << std::endl;

    Condition cond_sub(false, SmartUnit(SmartUnit::SIUnit::M).get_int_unit(), period_us);
    auto component_sub = car.create_component(2); // Obtenha o componente para a porta 2
    auto sub_sd = component_sub.template subscribe<Condition>(cond_sub); // Passe Meter como argumento de template não-tipo

    std::vector<uint64_t> stamps;
    stamps.reserve(MESSAGES_TO_RECEIVE_BY_SUBSCRIBER);

    using Message = Message<Car::Protocol::Address>;
    Message message = Message(8 + SmartUnit(SmartUnit::SIUnit::M).get_value_size_bytes(), Message::Type::PUBLISH);

    for (size_t i = 0; i < MESSAGES_TO_RECEIVE_BY_SUBSCRIBER; ++i) {
        if (!sub_sd.receive(&message)) {
            std::cerr << "Subscriber falhou ao receber a mensagem " << i << std::endl;
            break; 
        }
        stamps.push_back(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count()
        );
    }

    int status;
    waitpid(pid, &status, 0);

    if (stamps.size() < MESSAGES_TO_RECEIVE_BY_SUBSCRIBER) {
        std::cerr << "ERRO: Subscriber recebeu menos mensagens do que o esperado. Esperado: "
                  << MESSAGES_TO_RECEIVE_BY_SUBSCRIBER << ", Recebido: " << stamps.size() << std::endl;
        return 1;
    }
    
    std::cout << "Subscriber recebeu " << stamps.size() << " mensagens." << std::endl;

    if (MESSAGES_TO_RECEIVE_BY_SUBSCRIBER > 1 && stamps.size() > 1) { // Precisa de pelo menos duas mensagens para verificar o período
        for (size_t i = 1; i < stamps.size(); ++i) {
            uint64_t delta = stamps[i] - stamps[i - 1];
            double diff_abs = std::abs(static_cast<double>(delta) - period_us);
            std::cout << "Intervalo " << i << ": " << delta << "us, Erro: " << (diff_abs / period_us) * 100 << "%" << std::endl;
            assert((diff_abs / period_us) <= TOLERANCE);
        }
    }


    std::cout << "Teste de resubscribe e temporização de resposta periódica passou para period_us="
              << period_us << "us\n";

    return 0;
}