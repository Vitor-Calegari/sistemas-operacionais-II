#ifndef CONTROL_HH
#define CONTROL_HH

#include <cstdint>

struct Control {
  enum Type : uint8_t {
    COMMON = 0x00,    // 0000 0000
    PUBLISH = 0x10,   // 0001 0000
    SUBSCRIBE = 0x20, // 0010 0000
    ANNOUNCE = 0x30,  // 0011 0000
    PTP = 0x40,       // 0100 0000
    MAC = 0x50        // 0101 0000
  };

  Control(Type type = COMMON) : value(static_cast<uint8_t>(type)) {
  }
  Control(uint8_t ctrl = 0) : value(ctrl) {
  }

  static constexpr uint8_t SYNC_MASK = 0x80; // 1000 0000
  static constexpr uint8_t TYPE_MASK = 0x70; // 0111 0000

  bool isSynchronized() const {
    return value & SYNC_MASK;
  }
  void setSynchronized(bool sync) {
    value = (value & ~SYNC_MASK) | (sync ? SYNC_MASK : 0);
  }

  Type getType() const {
    return static_cast<Type>(value & TYPE_MASK);
  }
  void setType(Type type) {
    value = (value & ~TYPE_MASK) | (static_cast<uint8_t>(type) & TYPE_MASK);
  }
  uint8_t value;
} __attribute__((packed));

#endif
