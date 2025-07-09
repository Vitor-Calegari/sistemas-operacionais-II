#ifndef DEBUG_TIMESTAMP
#define DEBUG_TIMESTAMP

#include "clocks.hh"
#include <vector>
#include <cstdint>
#include <iostream>
#include <unistd.h>
#include <mutex>

class GlobalTimestamps {
public:
    static GlobalTimestamps &getInstance() {
      static GlobalTimestamps instance{};
      return instance;
    }

    void addTopDownDelay(int64_t top, int64_t down) {
        std::lock_guard<std::mutex> lock(_mutex_top); // Protege o acesso a _observers
        GlobalTime &g = GlobalTime::getInstance();

        int64_t delay = down - top;

        _max_top_down_delay = std::max(_max_top_down_delay, delay);
        _min_top_down_delay = std::min(_min_top_down_delay, delay);
        double timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch() -
            std::chrono::microseconds(g.get_program_init())).count() / 1e6;
        std::pair<double, int64_t> delay_pair{timestamp, delay};
        _top_down_delays.push_back(delay_pair);
    }

    void addBottomUpDelay(int64_t bottom, int64_t top) {
        std::lock_guard<std::mutex> lock(_mutex_bottom); // Protege o acesso a _observers
        GlobalTime &g = GlobalTime::getInstance();

        int64_t delay = top - bottom;

        _max_bottom_up_delay = std::max(_max_bottom_up_delay, delay);
        _min_bottom_up_delay = std::min(_min_bottom_up_delay, delay);
        double timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch() -
            std::chrono::microseconds(g.get_program_init())).count() / 1e6;
        std::pair<double, int64_t> delay_pair{timestamp, delay};
        _bottom_up_delays.push_back(delay_pair);
    }
  
    std::vector<std::pair<double, int64_t>> getTopDownDelays() { return _top_down_delays; }

    std::vector<std::pair<double, int64_t>> getBottomUpDelays() { return _bottom_up_delays; }

    int64_t getMaxTopDownDelay() { return _max_top_down_delay; }

    int64_t getMinTopDownDelay() { return _min_top_down_delay; }

    int64_t getMaxBottomUpDelay() { return _max_bottom_up_delay; }

    int64_t getMinBottomUpDelay() { return _min_bottom_up_delay; }

    void printDelays() {
        std::cout << "(PID: " << getpid() << ")"<< " (top down): ";
        for (auto [t, _] : _top_down_delays) {
          std::cout << t << ", ";
        }
        std::cout << std::endl;
        for (auto [_, d] : _top_down_delays) {
        std::cout << d << ", ";
        }
        std::cout << std::endl << std::endl;
        std::cout << "Bottom up: ";
        for (auto [t, _] : _bottom_up_delays) {
          std::cout << t << ", ";
        }
        std::cout << std::endl;
        for (auto [_, d] : _bottom_up_delays) {
          std::cout << d << ", ";
        }
        std::cout << std::endl << std::endl;
        std::cout << "Max top down: " << _max_top_down_delay << std::endl;
        std::cout << "Min top down: " << _min_top_down_delay << std::endl;
        std::cout << "Max bottom up: " << _max_bottom_up_delay << std::endl;
        std::cout << "Min bottom up: " << _min_bottom_up_delay << std::endl;
    }

private:
    GlobalTimestamps(GlobalTimestamps const &) = delete;
    void operator=(GlobalTimestamps const &) = delete;
    GlobalTimestamps(){}

    std::vector<std::pair<double, int64_t>> _top_down_delays{};
    std::vector<std::pair<double, int64_t>> _bottom_up_delays{};
    int64_t _max_top_down_delay = INT64_MIN;
    int64_t _min_top_down_delay = INT64_MAX;
    int64_t _max_bottom_up_delay = INT64_MIN;
    int64_t _min_bottom_up_delay = INT64_MAX;
    std::mutex _mutex_top{};
    std::mutex _mutex_bottom{};
};

#endif