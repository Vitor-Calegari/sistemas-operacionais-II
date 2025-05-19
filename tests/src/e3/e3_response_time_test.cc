#include "car.hh"
#include "component.hh"
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

constexpr size_t NUM_MESSAGES = 10;
constexpr uint32_t DEFAULT_PERIOD_US = 10000;  // 10 ms
constexpr double TOLERANCE = 0.1;               // 10%

int main(int argc, char* argv[]) {
    uint32_t period_us = (argc > 1) ? std::stoul(argv[1]) : DEFAULT_PERIOD_US;

    pid_t pid = fork();

if (pid == 0) {
    Car car = Car();
    Transducer<SmartUnit(SmartUnit::SIUnit::M)> transd(0, 255);
    Condition cond(true, SmartUnit(SmartUnit::SIUnit::M).get_int_unit(), period_us);
    auto pub_comp = car.create_component(1).template register_publisher<Condition, Transducer<SmartUnit(SmartUnit::SIUnit::M)>>(&transd, cond);
    for (size_t i = 0; i < NUM_MESSAGES; ++i) {
        using Message = Message<Car::Protocol::Address>;
        Message message = Message(8 + SmartUnit(SmartUnit::SIUnit::M).get_value_size_bytes(), Message::Type::PUBLISH);
        usleep(1000000000);
    }
    return 0;
}

    Car car = Car();
    Condition cond(false, SmartUnit(SmartUnit::SIUnit::M).get_int_unit(), period_us);
    auto sub_comp = car.create_component(1).template subscribe<Condition>(cond);

    std::vector<uint64_t> stamps;
    stamps.reserve(NUM_MESSAGES);
    using Message = Message<Car::Protocol::Address>;
    Message message = Message(8 + SmartUnit(SmartUnit::SIUnit::M).get_value_size_bytes(), Message::Type::PUBLISH);

    


    for (size_t i = 0; i < NUM_MESSAGES; ++i) {
        std::cout << "Periodic response timing test passed for period_us="
              << period_us << "us\n";
        sub_comp.receive(&message);
        std::cout << "Periodic response timing test passed for period_us="
              << period_us << "us\n";
        stamps.push_back(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count()
        );
    }

    int status;
    waitpid(pid, &status, 0);

    for (size_t i = 1; i < NUM_MESSAGES; ++i) {
        uint64_t delta = stamps[i] - stamps[i - 1];
        double diff = std::abs(static_cast<double>(delta) - period_us);
        assert((diff / period_us) <= TOLERANCE);
    }

    std::cout << "Periodic response timing test passed for period_us="
              << period_us << "us\n";

    return 0;
}