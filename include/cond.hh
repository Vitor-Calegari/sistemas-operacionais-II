#ifndef CONDITION_HH
#define CONDITION_HH

#include <cstdint>

class Condition {
public:
    Condition(uint32_t u, uint32_t p) : unit(u), period(p) {}
public:
    uint32_t unit;
    uint32_t period;
    bool operator==(const Condition& other) {
        return (unit == other.unit && period % other.period);
    }
};

#endif