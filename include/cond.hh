#ifndef CONDITION_HH
#define CONDITION_HH

#include <cstdint>

class Condition {
public:
    uint32_t unit;
    uint32_t period;
    bool operator==(const Condition& other) {
        return (unit == other.unit && period % other.period);
    }
};

#endif