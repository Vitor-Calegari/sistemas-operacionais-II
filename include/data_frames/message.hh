#ifndef MESSAGE_HH
#define MESSAGE_HH

#include "control.hh"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <tuple>

template <typename Addr, typename Protocol>
class Message {
public:
  Message(Addr src, Addr dst, std::size_t payload_size,
          Control ctrl = Control(Control::Type::COMMON),
          Protocol *prot = nullptr)
      : _prot(prot), _source_addr(src), _dest_addr(dst), _ctrl(ctrl),
        _timestamp(0), _payload_size(payload_size) {
    _data = new std::byte[_payload_size];
    std::fill(_data, _data + _payload_size, std::byte(0));
  }

  Message(std::size_t payload_size,
          Control ctrl = Control(Control::Type::COMMON),
          Protocol *prot = nullptr)
      : _prot(prot), _source_addr(Addr()), _dest_addr(Addr()), _ctrl(ctrl),
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
  double *getCoordX() {
    if (std::isnan(_coord_x)) {
      std::tie(_coord_x, _coord_y) = _prot->getLocation();
    }

    return &_coord_x;
  }

  double *getCoordY() {
    if (std::isnan(_coord_y)) {
      std::tie(_coord_x, _coord_y) = _prot->getLocation();
    }

    return &_coord_y;
  }

  int64_t *timestamp() {
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
  Protocol *_prot;
  Addr _source_addr;
  Addr _dest_addr;
  Control _ctrl;
  double _coord_x = std::numeric_limits<double>::quiet_NaN();
  double _coord_y = std::numeric_limits<double>::quiet_NaN();
  int64_t _timestamp;
  std::size_t _payload_size;
  std::byte *_data;
} __attribute__((packed));

#endif
