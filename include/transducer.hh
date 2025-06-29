#ifndef TRANSDUCER_HH
#define TRANSDUCER_HH

#include "smart_unit.hh"
#include <cstdint>
#include <random>

template <SmartUnit Unit>
class TransducerCommon {
public:
  virtual void get_data(std::byte *data) = 0;

  constexpr SmartUnit get_unit() const {
    return Unit;
  }
};

template <SmartUnit Unit,
          typename Distribution = std::uniform_int_distribution<uint8_t>>
class TransducerRandom : public TransducerCommon<Unit> {
public:
  static constexpr SmartUnit unit = Unit;
  TransducerRandom(int lower, int upper)
      : _lower(lower), _upper(upper), _rng(_rd()), _dist(_lower, _upper) {
  }

  void get_data(std::byte *data) {
    int len = Unit.get_value_size_bytes();
    for (int i = 0; i < len; ++i) {
      data[i] = static_cast<std::byte>(_dist(_rng));
    }
    return;
  }

private:
  const int _lower, _upper;

  std::random_device _rd{};
  std::mt19937 _rng;
  Distribution _dist;
};

#endif
