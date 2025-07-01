#ifndef CONDITION_HH
#define CONDITION_HH

#include <cstdint>

class Condition {
public:
  Condition(bool isPub, uint32_t u, uint32_t p = 0)
      : isPub(isPub), unit(u), period(p) {
  }

  struct Data {
    uint32_t unit;
    uint32_t period;
  };

public:
  bool isPub;
  uint32_t unit;
  uint32_t period;

  friend bool operator==(const Condition &lhs, const Condition &rhs) {
    // lhs é a condição do SmartData
    // rhs é a condição gerada a partir da mensagem
    bool ret = false;
    if (lhs.isPub && !rhs.isPub && lhs.unit == rhs.unit) {
      // SmartData é Publisher, a mensagem recebido é Subscribe
      // e a unidade de Smartdata é igual a da mensagem
      ret = true;
    } else if (!lhs.isPub && rhs.isPub && lhs.unit == rhs.unit &&
               rhs.period % lhs.period == 0) {
      // SmartData é Subscriber, a mensagem recebida é Publish,
      // a unidade de Smartdata é igual a da mensagem e o periodo
      // em que a mensagem foi enviada é divisível pelo do Smartdata
      ret = true;
    }
    return ret;
  }

  friend bool operator<(const Condition &lhs, const Condition &rhs) {
    return lhs.period < rhs.period;
  }
};

#endif
