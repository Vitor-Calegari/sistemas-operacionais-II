#define DEBUG_SYNC
#include "car.hh"
#undef DEBUG_SYNC

#include <array>
#include <cassert>
#include <csignal>
#include <functional>
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#define NUM_MSGS 100000
#define MSG_SIZE 5

#ifndef INTERFACE_NAME
#define INTERFACE_NAME "lo"
#endif

constexpr int LEADER_CHECK_DELAY_SECONDS = 2;
constexpr int FINAL_WAIT_SECONDS = 3;
constexpr int COMPONENT_ID = 61;

void single_car() {
    Car car = Car();
    auto comp = car.create_component(COMPONENT_ID);
        
    // Aguarda um tempo para verificar se se torna líder automaticamente
    usleep(LEADER_CHECK_DELAY_SECONDS);
    
    if (car.prot.amILeader()) {
        std::cout << "Single Car (PID: " << getpid() << ") is LEADER" << std::endl;
    } else {
        std::cout << "Single Car (PID: " << getpid() << ") is NOT leader" << std::endl;
    }
    
    usleep(FINAL_WAIT_SECONDS);
    
}

int main() {
    pid_t car_pid = fork();
    
    if (car_pid == 0) {
        // Processo filho - único carro
        single_car();
        exit(0);
    } else if (car_pid > 0) {
        // Processo pai - aguarda o filho terminar        
        int status;
        waitpid(car_pid, &status, 0);        
    } else {
        std::cerr << "Fork failed for single car" << std::endl;
        return 1;
    }
    
    return 0;
}

// Compilação:
// g++ -std=c++20 -g tests/src/e4/e4_single_car_test.cc -Iinclude -Itests/include -o single_car_test src/ethernet.cc src/utils.cc