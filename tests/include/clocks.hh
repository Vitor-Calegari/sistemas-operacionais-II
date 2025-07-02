#ifndef CLOCKS_HH
#define CLOCKS_HH

#include <chrono>

class GlobalTime {
public:
    static GlobalTime &getInstance() {
      static GlobalTime instance{};
      return instance;
    }
  
    int64_t get_program_init() { return program_init; }
private:
    GlobalTime(GlobalTime const &) = delete;
    void operator=(GlobalTime const &) = delete;
    GlobalTime() : program_init(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count()){
    }
    int64_t program_init;
};

#endif