#include "car.hh"
#include "cond.hh"
#include "smart_data.hh"
#include "transducer.hh"
#include "smart_unit.hh"
#include "message.hh"
#include <sys/mman.h>
#include <chrono>
#include <cmath>
#include <iostream>
#include <unistd.h>
#include <sys/wait.h>
#include <cassert>
#include <vector>

constexpr size_t PUBLISH_AFTER_PERIOD = 1e6;
constexpr size_t MESSAGES_TO_RECEIVE_BY_SUBSCRIBER = 25;
constexpr uint32_t DEFAULT_PERIOD_US = 5e3;


int main(int argc, char* argv[]) {
    uint32_t period_us = (argc > 1) ? std::stoul(argv[1]) : DEFAULT_PERIOD_US;
    constexpr SmartUnit Meter(SmartUnit::SIUnit::M);
    using Message = Message<Car::ProtocolC::Address>;
    sem_t *semaphore =
    static_cast<sem_t *>(mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE,
                              MAP_SHARED | MAP_ANONYMOUS, -1, 0));
    sem_init(semaphore, 1, 0); // Inicialmente bloqueado

    pid_t pid = fork();
    if (pid == -1) {
        perror("fork falhou");
        return 1;
    }

    if (pid == 0) {
        usleep(PUBLISH_AFTER_PERIOD);
        Car car = Car();
        Transducer<Meter> transd(0, 255);
        Condition cond(true, Meter.get_int_unit(), period_us);
        
        auto component_pub = car.create_component(1);
        auto pub_sd = component_pub.template register_publisher<Condition, Transducer<Meter>>(&transd, cond);
        std::cout << "Publisher se conectando após " << PUBLISH_AFTER_PERIOD << " us..." << std::endl;
        sem_wait(semaphore);
        return 0;
    }
    
    Condition cond_sub(false, Meter.get_int_unit(), period_us);
    Car car = Car();
    auto component_sub = car.create_component(2);
    auto sub_sd = component_sub.template subscribe<Condition>(cond_sub);

    for (size_t i = 0; i < MESSAGES_TO_RECEIVE_BY_SUBSCRIBER; ++i) {
        Message message = Message(8 + Meter.get_value_size_bytes(), Message::Type::PUBLISH);
        if (!sub_sd.receive(&message)) {
            std::cerr << "Subscriber falhou ao receber a mensagem " << i << std::endl;
            break; 
        }
    }

    sem_post(semaphore);

    int status;
    waitpid(pid, &status, 0);
    
    std::cout << "Subscriber recebeu " << MESSAGES_TO_RECEIVE_BY_SUBSCRIBER << " mensagens." << std::endl;

    std::cout << "Teste de resubscribe passou" << std::endl;

    return 0;
}