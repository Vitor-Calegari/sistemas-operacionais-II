#ifndef CONDITION_HH
#define CONDITION_HH

#include <cstdint>

class Condition {
public:
  Condition(uint32_t u, uint32_t p) : unit(u), period(p) {
  }

public:
  uint32_t unit;
  uint32_t period;

  friend bool operator==(const Condition &lhs, const Condition &rhs) {
    return (lhs.unit == rhs.unit && lhs.period % rhs.period);
  }

  friend bool operator<(const Condition &lhs, const Condition &rhs) {
    return lhs.period < rhs.period;
  }
};

#endif
