#ifndef MESSAGE_HH
#define MESSAGE_HH

#include "control.hh"
#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>

template <typename Addr>
class Message {
public:
  Message(Addr src, Addr dst, std::size_t payload_size,
          Control ctrl = Control(Control::Type::COMMON))
      : _source_addr(src), _dest_addr(dst), _ctrl(ctrl),
        _timestamp(0), _payload_size(payload_size) {
    _data = new std::byte[_payload_size];
    std::fill(_data, _data + _payload_size, std::byte(0));
  }

  Message(std::size_t payload_size, Control ctrl = Control(Control::Type::COMMON))
      : _source_addr(Addr()), _dest_addr(Addr()), _ctrl(ctrl),
        _timestamp(0), _payload_size(payload_size) {
    _data = new std::byte[_payload_size];
    std::fill(_data, _data + _payload_size, std::byte(0));
  }

  ~Message() {
    delete[] _data;
  }

  Addr *sourceAddr() {
    return &_source_addr;
  }

  Addr *destAddr() {
    return &_dest_addr;
  }

  void setControl(uint8_t new_ctrl) {
    _ctrl = static_cast<Control>(new_ctrl);
  }

  Control *getControl() {
    return &_ctrl;
  }
  #pragma GCC diagnostic push
  #pragma GCC diagnostic ignored "-Waddress-of-packed-member"
  uint64_t *timestamp() {
    return &_timestamp;
  }
  #pragma GCC diagnostic pop

  void setSize(std::size_t new_size) {
    _payload_size = new_size;
  }

  std::size_t size() {
    return _payload_size;
  }

  std::byte *data() const {
    return _data;
  }

  template <typename T>
  T *data() {
    return std::bit_cast<T *>(&_data);
  }

private:
  Addr _source_addr;
  Addr _dest_addr;
  Control _ctrl;
  uint64_t _timestamp;
  std::size_t _payload_size;
  std::byte *_data;
} __attribute__((packed));

#endif
