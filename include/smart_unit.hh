#ifndef SMART_UNIT_HH
#define SMART_UNIT_HH

#include <cstdint>

#ifdef DEBUG
#include <iostream>
#endif

class SmartUnit {
public:
  enum T : uint32_t { DIGITAL = 0, SI = 1 };
  enum N : uint32_t { INT32 = 0, INT64 = 1, FLOAT32 = 2, FLOAT64 = 3 };
  enum M : uint32_t { DIRECT = 0, MULT_INV = 1, LOG = 2, MULT_INV_LOG = 3 };

  enum class SIUnit : uint32_t { CD = 0, MOL, K, A, S, KG, M, RAD, SR };

private:
  static constexpr uint32_t NUMBER_OF_UNITS = 9;
  static constexpr int32_t BASE_UNIT_VALUE = 4;

  enum Sizes : uint32_t { T_BITS = 1, N_BITS = 2, M_BITS = 2, UNIT_BITS = 3 };

  enum Offsets : uint32_t {
    T = Sizes::UNIT_BITS * NUMBER_OF_UNITS + Sizes::M_BITS + Sizes::N_BITS,
    N = Sizes::UNIT_BITS * NUMBER_OF_UNITS + Sizes::M_BITS,
    M = Sizes::UNIT_BITS * NUMBER_OF_UNITS,

    STERADIAN = Sizes::UNIT_BITS * static_cast<uint32_t>(SIUnit::SR),
    RADIAN = Sizes::UNIT_BITS * static_cast<uint32_t>(SIUnit::RAD),
    METER = Sizes::UNIT_BITS * static_cast<uint32_t>(SIUnit::M),
    KILOGRAM = Sizes::UNIT_BITS * static_cast<uint32_t>(SIUnit::KG),
    SECOND = Sizes::UNIT_BITS * static_cast<uint32_t>(SIUnit::S),
    AMPERE = Sizes::UNIT_BITS * static_cast<uint32_t>(SIUnit::A),
    KELVIN = Sizes::UNIT_BITS * static_cast<uint32_t>(SIUnit::K),
    MOLE = Sizes::UNIT_BITS * static_cast<uint32_t>(SIUnit::MOL),
    CANDELA = Sizes::UNIT_BITS * static_cast<uint32_t>(SIUnit::CD)
  };

  struct UnitStruct {
    uint32_t t : Sizes::T_BITS;
    uint32_t n : Sizes::N_BITS;
    uint32_t m : Sizes::M_BITS;

    uint32_t steradian : Sizes::UNIT_BITS;
    uint32_t radian : Sizes::UNIT_BITS;
    uint32_t meter : Sizes::UNIT_BITS;
    uint32_t kilogram : Sizes::UNIT_BITS;
    uint32_t second : Sizes::UNIT_BITS;
    uint32_t ampere : Sizes::UNIT_BITS;
    uint32_t kelvin : Sizes::UNIT_BITS;
    uint32_t mole : Sizes::UNIT_BITS;
    uint32_t candela : Sizes::UNIT_BITS;
  } __attribute__((packed));

private:
  static constexpr uint32_t get_mask(uint32_t bit_size) {
    return (1 << bit_size) - 1;
  }

  static constexpr uint32_t get_bits_at(uint32_t val, uint32_t mask,
                                        uint32_t pos) {
    return (val & (mask << pos)) >> pos;
  }

  // Idealmente seria usado std::bit_cast ao invés disso. No entanto, Clang não
  // dá suporte à std::bit_cast constexpr com bit-fields.
  static constexpr struct UnitStruct unit_int_to_struct(const uint32_t unit) {
    uint32_t t = get_bits_at(unit, get_mask(Sizes::T_BITS), Offsets::T);
    uint32_t n = get_bits_at(unit, get_mask(Sizes::N_BITS), Offsets::N);
    uint32_t m = get_bits_at(unit, get_mask(Sizes::M_BITS), Offsets::M);

    auto unit_mask = get_mask(Sizes::UNIT_BITS);

    uint32_t steradian = get_bits_at(unit, unit_mask, Offsets::STERADIAN);
    uint32_t radian = get_bits_at(unit, unit_mask, Offsets::RADIAN);
    uint32_t meter = get_bits_at(unit, unit_mask, Offsets::METER);
    uint32_t kilogram = get_bits_at(unit, unit_mask, Offsets::KILOGRAM);
    uint32_t second = get_bits_at(unit, unit_mask, Offsets::SECOND);
    uint32_t ampere = get_bits_at(unit, unit_mask, Offsets::AMPERE);
    uint32_t kelvin = get_bits_at(unit, unit_mask, Offsets::KELVIN);
    uint32_t mole = get_bits_at(unit, unit_mask, Offsets::MOLE);
    uint32_t candela = get_bits_at(unit, unit_mask, Offsets::CANDELA);

    struct UnitStruct unit_st {
      t, n, m, steradian, radian, meter, kilogram, second, ampere, kelvin, mole,
          candela
    };

    return unit_st;
  }

  // O uso de std::bit_cast aqui não é ideal, pois ele depende da endianness do
  // sistema.
  static constexpr uint32_t unit_struct_to_int(const struct UnitStruct unit) {
    uint32_t unit_int = 0;

    unit_int += unit.t << Offsets::T;
    unit_int += unit.n << Offsets::N;
    unit_int += unit.m << Offsets::M;

    unit_int += unit.steradian << Offsets::STERADIAN;
    unit_int += unit.radian << Offsets::RADIAN;
    unit_int += unit.meter << Offsets::METER;
    unit_int += unit.kilogram << Offsets::KILOGRAM;
    unit_int += unit.second << Offsets::SECOND;
    unit_int += unit.ampere << Offsets::AMPERE;
    unit_int += unit.kelvin << Offsets::KELVIN;
    unit_int += unit.mole << Offsets::MOLE;
    unit_int += unit.candela << Offsets::CANDELA;

    return unit_int;
  }

  static constexpr uint32_t si_unit_to_int(const SIUnit si_unit) {
    uint32_t unit_int = (T::SI << Offsets::T) + (N::INT32 << Offsets::N) +
                        (M::DIRECT << Offsets::M);

    for (uint32_t i = 0; i < NUMBER_OF_UNITS; ++i) {
      unit_int += BASE_UNIT_VALUE << (Sizes::UNIT_BITS * i);
    }
    unit_int += 1 << (Sizes::UNIT_BITS * static_cast<uint32_t>(si_unit));

    return unit_int;
  }

public:
  // Para que essa classe seja constexpr isso precisa estar público.
  struct UnitStruct _unit {};

  constexpr SmartUnit(uint32_t unit) : _unit(unit_int_to_struct(unit)) {
  }

  // Presume Int32 por padrão.
  constexpr SmartUnit(SIUnit si_unit)
      : _unit(unit_int_to_struct(si_unit_to_int(si_unit))) {
  }

#define MULT_UNIT(unit) _unit.unit += rhs._unit.unit - BASE_UNIT_VALUE
  constexpr SmartUnit &operator*=(const SmartUnit &rhs) {
    MULT_UNIT(steradian);
    MULT_UNIT(radian);
    MULT_UNIT(meter);
    MULT_UNIT(kilogram);
    MULT_UNIT(second);
    MULT_UNIT(ampere);
    MULT_UNIT(kelvin);
    MULT_UNIT(mole);
    MULT_UNIT(candela);

    return *this;
  }
#undef MULT_UNIT

  constexpr SmartUnit &operator*=(SIUnit rhs) {
    return *this *= SmartUnit(rhs);
  }

  constexpr friend SmartUnit operator*(SmartUnit lhs, const SmartUnit &rhs) {
    return lhs *= rhs;
  }

  constexpr friend SmartUnit operator*(SIUnit lhs, SIUnit rhs) {
    return SmartUnit(lhs) * rhs;
  }

  // Evita possíveis bugs, visto que, por padrão, o compilador converteria o int
  // para SmartUnit.
  constexpr friend SmartUnit operator*(SIUnit, int) = delete;
  constexpr friend SmartUnit operator*(int, SIUnit) = delete;

#define MULT_INV_UNIT(unit)                                                    \
  inv_unit._unit.unit = 2 * BASE_UNIT_VALUE - inv_unit._unit.unit

  // Implementa inverso multiplicativo da unidade.
  // TODO: Caso a unidade esteja na quarta potência negativa não há inverso
  // multiplicativo.
  constexpr friend SmartUnit mult_inv(const SmartUnit &unit) {
    SmartUnit inv_unit = unit;

    MULT_INV_UNIT(steradian);
    MULT_INV_UNIT(radian);
    MULT_INV_UNIT(meter);
    MULT_INV_UNIT(kilogram);
    MULT_INV_UNIT(second);
    MULT_INV_UNIT(ampere);
    MULT_INV_UNIT(kelvin);
    MULT_INV_UNIT(mole);
    MULT_INV_UNIT(candela);

    return inv_unit;
  }
#undef MULT_INV_UNIT

  // Exponenciação.
  constexpr SmartUnit &operator^=(int32_t rhs) {
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

  constexpr friend SmartUnit operator^(SmartUnit lhs, int32_t rhs) {
    return lhs ^= rhs;
  }

  constexpr friend SmartUnit operator^(SIUnit lhs, int32_t rhs) {
    return SmartUnit(lhs) ^ rhs;
  }

  constexpr uint32_t get_int_unit() const {
    return unit_struct_to_int(_unit);
  }

  constexpr uint32_t get_t() const {
    return _unit.t;
  }

  constexpr uint32_t get_n() const {
    return _unit.n;
  }

  constexpr uint32_t get_m() const {
    return _unit.m;
  }

  constexpr uint32_t get_value_size_bytes() const {
    if (_unit.n == N::INT32 || _unit.n == N::FLOAT32) {
      return sizeof(int32_t);
    }

    return sizeof(int64_t);
  }

  void set_t(uint32_t t) {
    _unit.t = t;
  }

  void set_n(uint32_t n) {
    _unit.n = n;
  }

  void set_m(uint32_t m) {
    _unit.m = m;
  }

#ifdef DEBUG
#define PRINT_UNIT(unit, symbol)                                               \
  std::cout << "[" symbol "^" << int32_t(_unit.unit) - BASE_UNIT_VALUE         \
            << "] ";                                                           \
  std::cout << #unit ": " << _unit.unit << std::endl;

  void print_unit() const {
    std::cout << "===============\nUNIT:\n";

    std::cout << "(T, N, M) = (" << _unit.t << ", " << _unit.n << ", "
              << _unit.m << ')' << std::endl;

    PRINT_UNIT(steradian, "sr");
    PRINT_UNIT(radian, "rad");
    PRINT_UNIT(meter, "m");
    PRINT_UNIT(kilogram, "kg");
    PRINT_UNIT(second, "s");
    PRINT_UNIT(ampere, "A");
    PRINT_UNIT(kelvin, "K");
    PRINT_UNIT(mole, "mol");
    PRINT_UNIT(candela, "cd");

    std::cout << std::endl;
  }
#undef PRINT_UNIT
#endif
};

#endif
