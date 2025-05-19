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

  static constexpr uint32_t SIZE_BYTES = 4;

private:
  static constexpr uint32_t NUMBER_OF_UNITS = 9;
  static constexpr int32_t BASE_UNIT_VALUE = 4;

  enum Sizes : uint32_t {
    T_BITS = 1,
    N_BITS = 2,
    M_BITS = 2,
    UNIT_BITS = 3,
    MULTI_BITS = 2,
    DIGITAL_TYPE_BITS = 13,
    DIGITAL_LENGTH_BITS = 16
  };

  enum SIOffsets : uint32_t {
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

  enum DigitalOffsets : uint32_t {
    LENGTH = 0,
    TYPE = Sizes::DIGITAL_LENGTH_BITS,
    MULTI = TYPE + Sizes::DIGITAL_TYPE_BITS,
    DIGITAL_T = MULTI + Sizes::MULTI_BITS,
  };

  struct SIStruct {
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

  struct DigitalStruct {
    uint32_t t : Sizes::T_BITS;
    uint32_t multi : Sizes::MULTI_BITS;
    uint32_t type : Sizes::DIGITAL_TYPE_BITS;
    uint32_t length : Sizes::DIGITAL_LENGTH_BITS;
  } __attribute__((packed));

  union UnitUnion {
    struct SIStruct si;
    struct DigitalStruct digital;
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
  static constexpr union UnitUnion unit_int_to_struct(const uint32_t unit) {
    union UnitUnion unit_union;

    uint32_t t = get_bits_at(unit, get_mask(Sizes::T_BITS), SIOffsets::T);

    if (t == T::SI) {
      uint32_t n = get_bits_at(unit, get_mask(Sizes::N_BITS), SIOffsets::N);
      uint32_t m = get_bits_at(unit, get_mask(Sizes::M_BITS), SIOffsets::M);

      auto unit_mask = get_mask(Sizes::UNIT_BITS);

      uint32_t steradian = get_bits_at(unit, unit_mask, SIOffsets::STERADIAN);
      uint32_t radian = get_bits_at(unit, unit_mask, SIOffsets::RADIAN);
      uint32_t meter = get_bits_at(unit, unit_mask, SIOffsets::METER);
      uint32_t kilogram = get_bits_at(unit, unit_mask, SIOffsets::KILOGRAM);
      uint32_t second = get_bits_at(unit, unit_mask, SIOffsets::SECOND);
      uint32_t ampere = get_bits_at(unit, unit_mask, SIOffsets::AMPERE);
      uint32_t kelvin = get_bits_at(unit, unit_mask, SIOffsets::KELVIN);
      uint32_t mole = get_bits_at(unit, unit_mask, SIOffsets::MOLE);
      uint32_t candela = get_bits_at(unit, unit_mask, SIOffsets::CANDELA);

      struct SIStruct unit_st {
        t, n, m, steradian, radian, meter, kilogram, second, ampere, kelvin,
            mole, candela
      };
      unit_union.si = unit_st;
    } else {
      uint32_t multi =
          get_bits_at(unit, get_mask(Sizes::MULTI_BITS), DigitalOffsets::MULTI);
      uint32_t type = get_bits_at(unit, get_mask(Sizes::DIGITAL_TYPE_BITS),
                                  DigitalOffsets::TYPE);
      uint32_t length = get_bits_at(unit, get_mask(Sizes::DIGITAL_LENGTH_BITS),
                                    DigitalOffsets::LENGTH);

      struct DigitalStruct unit_st {
        t, multi, type, length
      };
      unit_union.digital = unit_st;
    }

    return unit_union;
  }

  // O uso de std::bit_cast aqui não é ideal, pois ele depende da endianness do
  // sistema.
  static constexpr uint32_t unit_struct_to_int(const union UnitUnion unit,
                                               uint8_t unit_type) {
    uint32_t unit_int = 0;

    if (unit_type == T::SI) {
      unit_int += unit.si.t << SIOffsets::T;
      unit_int += unit.si.n << SIOffsets::N;
      unit_int += unit.si.m << SIOffsets::M;

      unit_int += unit.si.steradian << SIOffsets::STERADIAN;
      unit_int += unit.si.radian << SIOffsets::RADIAN;
      unit_int += unit.si.meter << SIOffsets::METER;
      unit_int += unit.si.kilogram << SIOffsets::KILOGRAM;
      unit_int += unit.si.second << SIOffsets::SECOND;
      unit_int += unit.si.ampere << SIOffsets::AMPERE;
      unit_int += unit.si.kelvin << SIOffsets::KELVIN;
      unit_int += unit.si.mole << SIOffsets::MOLE;
      unit_int += unit.si.candela << SIOffsets::CANDELA;
    } else {
      unit_int += unit.digital.t << DigitalOffsets::DIGITAL_T;
      unit_int += unit.digital.multi << DigitalOffsets::MULTI;
      unit_int += unit.digital.type << DigitalOffsets::TYPE;
      unit_int += unit.digital.length << DigitalOffsets::LENGTH;
    }

    return unit_int;
  }

  static constexpr uint32_t si_unit_to_int(const SIUnit si_unit) {
    uint32_t unit_int = (T::SI << SIOffsets::T) + (N::INT32 << SIOffsets::N) +
                        (M::DIRECT << SIOffsets::M);

    for (uint32_t i = 0; i < NUMBER_OF_UNITS; ++i) {
      unit_int += BASE_UNIT_VALUE << (Sizes::UNIT_BITS * i);
    }
    unit_int += 1 << (Sizes::UNIT_BITS * static_cast<uint32_t>(si_unit));

    return unit_int;
  }

public:
  // Para que essa classe seja constexpr isso precisa estar público.
  union UnitUnion _unit {};
  uint8_t _unit_type = T::SI;

  constexpr SmartUnit(uint32_t unit)
      : _unit(unit_int_to_struct(unit)),
        _unit_type(get_bits_at(unit, get_mask(Sizes::T_BITS), SIOffsets::T)) {
  }

  // Presume Int32 por padrão.
  constexpr SmartUnit(SIUnit si_unit)
      : _unit(unit_int_to_struct(si_unit_to_int(si_unit))), _unit_type(T::SI) {
  }

// Todos os operadores somente funcionam em unidades do SI.
#define MULT_UNIT(unit) _unit.si.unit += rhs._unit.si.unit - BASE_UNIT_VALUE
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
  inv_unit._unit.si.unit = 2 * BASE_UNIT_VALUE - inv_unit._unit.si.unit

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
    return unit_struct_to_int(_unit, _unit_type);
  }

  constexpr uint32_t get_t() const {
    return _unit_type;
  }

  constexpr uint32_t get_value_size_bytes() const {
    if (_unit_type == T::SI) {
      if (_unit.si.n == N::INT32 || _unit.si.n == N::FLOAT32) {
        return sizeof(int32_t);
      }
      return sizeof(int64_t);
    } else {
      return _unit.digital.length;
    }
  }

#ifdef DEBUG
#define PRINT_UNIT(unit, symbol)                                               \
  std::cout << "[" symbol "^" << int32_t(_unit.unit) - BASE_UNIT_VALUE         \
            << "] ";                                                           \
  std::cout << #unit ": " << _unit.unit << std::endl;

  // Somente para unidades do SI.
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
