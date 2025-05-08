#ifndef SMART_UNIT_HH
#define SMART_UNIT_HH

#include <bit>
#include <cstdint>

class SmartUnit {
private:
  struct UnitStruct {
    uint32_t t : 1;
    uint32_t n : 2;
    uint32_t m : 2;

    uint32_t steradian : 3;
    uint32_t radian : 3;
    uint32_t meter : 3;
    uint32_t kilogram : 3;
    uint32_t second : 3;
    uint32_t ampere : 3;
    uint32_t kelvin : 3;
    uint32_t mol : 3;
    uint32_t candela : 3;
  };

  struct UnitStruct _unit = std::bit_cast<struct UnitStruct>(0);

  static constexpr int T_OFFSET = 31;
  static constexpr int N_OFFSET = 29;
  static constexpr int M_OFFSET = 27;

  static constexpr int BASE_UNIT_VALUE = 5;

public:
  enum T : bool { DIGITAL = 0, SI = 1 };
  enum N { INT32 = 0, INT64 = 1, FLOAT32 = 2, FLOAT64 = 3 };
  enum M { DIRECT = 0, MULT_INV = 1, LOG = 2, MULT_INV_LOG = 3 };

  enum class SIUnit : uint32_t {
    CD = BASE_UNIT_VALUE,
    MOL = BASE_UNIT_VALUE << 3,
    K = BASE_UNIT_VALUE << 6,
    A = BASE_UNIT_VALUE << 9,
    S = BASE_UNIT_VALUE << 12,
    KG = BASE_UNIT_VALUE << 15,
    M = BASE_UNIT_VALUE << 18,
    RAD = BASE_UNIT_VALUE << 21,
    SR = BASE_UNIT_VALUE << 24,
  };

  SmartUnit(uint32_t unit) : _unit(std::bit_cast<struct UnitStruct>(unit)) {
  }

  // Presume Int32 por padrão.
  SmartUnit(SIUnit si_unit)
      : _unit(std::bit_cast<struct UnitStruct>(
            (T::SI << T_OFFSET) + (N::INT32 << N_OFFSET) +
            (M::DIRECT << M_OFFSET) + static_cast<uint32_t>(si_unit))) {
  }

#define MULT_UNIT(unit) _unit.unit += rhs._unit.unit - (BASE_UNIT_VALUE - 1)
  SmartUnit &operator*=(const SmartUnit &rhs) {
    MULT_UNIT(steradian);
    MULT_UNIT(radian);
    MULT_UNIT(meter);
    MULT_UNIT(kilogram);
    MULT_UNIT(second);
    MULT_UNIT(ampere);
    MULT_UNIT(kelvin);
    MULT_UNIT(mol);
    MULT_UNIT(candela);

    return *this;
  }
#undef MULT_UNIT

  SmartUnit &operator*=(SIUnit rhs) {
    *this *= SmartUnit(rhs);

    return *this;
  }

  friend SmartUnit operator*(SmartUnit lhs, const SmartUnit &rhs) {
    return lhs *= rhs;
  }

  friend SmartUnit operator*(SIUnit lhs, SIUnit rhs) {
    return SmartUnit(lhs) * SmartUnit(rhs);
  }

  friend SmartUnit operator*(SmartUnit lhs, SIUnit rhs) {
    return lhs *= SmartUnit(rhs);
  }

  friend SmartUnit operator*(SIUnit lhs, SmartUnit rhs) {
    SmartUnit smart_lhs(lhs);
    return smart_lhs *= rhs;
  }

  // Evita possíveis bugs, visto que, por padrão, o compilador converteria o int
  // para SmartUnit.
  friend SmartUnit operator*(SIUnit, int) = delete;
  friend SmartUnit operator*(int, SIUnit) = delete;

#define MULT_INV_UNIT(unit)                                                    \
  inv_unit._unit.unit = 2 * (BASE_UNIT_VALUE - 1) - inv_unit._unit.unit

  // Implementa inverso multiplicativo da unidade.
  // TODO: Caso a unidade esteja na quarta potência negativa não há inverso
  // multiplicativo.
  friend SmartUnit mult_inv(const SmartUnit &unit) {
    SmartUnit inv_unit = unit;

    MULT_INV_UNIT(steradian);
    MULT_INV_UNIT(radian);
    MULT_INV_UNIT(meter);
    MULT_INV_UNIT(kilogram);
    MULT_INV_UNIT(second);
    MULT_INV_UNIT(ampere);
    MULT_INV_UNIT(kelvin);
    MULT_INV_UNIT(mol);
    MULT_INV_UNIT(candela);

    return inv_unit;
  }
#undef MULT_INV_UNIT

  // Exponenciação.
  SmartUnit &operator^=(int rhs) {
    if (rhs == 0) {
      return *this;
    }

    if (rhs < 0) {
      *this = mult_inv(*this);
      rhs *= -1;
    }

    while (--rhs) {
      *this *= *this;
    }

    return *this;
  }

  friend SmartUnit operator^(SmartUnit lhs, int rhs) {
    lhs ^= rhs;
    return lhs;
  }

  friend SmartUnit operator^(SIUnit lhs, int rhs) {
    return SmartUnit(lhs) ^ rhs;
  }

  uint32_t get_unit() const {
    return std::bit_cast<uint32_t>(_unit);
  }

  int get_t() const {
    return _unit.t;
  }

  int get_n() const {
    return _unit.n;
  }

  int get_m() const {
    return _unit.m;
  }

  void set_t(int t) {
    _unit.t = t;
  }

  void set_n(int n) {
    _unit.n = n;
  }

  void set_m(int m) {
    _unit.m = m;
  }
};

#endif
