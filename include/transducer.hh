#ifndef TRANSDUCER_HH
#define TRANSDUCER_HH

#include "smart_unit.hh"
#include <random>

template <SmartUnit Unit,
          typename Distribution = std::uniform_int_distribution<int>>
class Transducer {
public:
  Transducer(int lower, int upper)
      : _lower(lower), _upper(upper), _rng(_rd()), _dist(_lower, _upper) {
  }

  int get_data() {
    return _dist(_rng);
  }

  constexpr SmartUnit get_unit() const {
    return Unit;
  }

private:
  const int _lower, _upper;

  std::random_device _rd{};
  std::mt19937 _rng;
  Distribution _dist;
};

#endif
