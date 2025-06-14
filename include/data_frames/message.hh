#ifndef MESSAGE_HH
#define MESSAGE_HH

#include "control.hh"
#include "mac.hh"
#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>

template <typename Addr>
class MessageCommon {
public:
  MessageCommon(Addr src, Addr dst, std::size_t payload_size,
                Control ctrl = Control(Control::Type::COMMON))
      : _source_addr(src), _dest_addr(dst), _ctrl(ctrl),
        _payload_size(payload_size) {
    _data = new std::byte[_payload_size];
    std::fill(_data, _data + _payload_size, std::byte(0));
  }

  MessageCommon(std::size_t payload_size,
                Control ctrl = Control(Control::Type::COMMON))
      : _source_addr(Addr()), _dest_addr(Addr()), _ctrl(ctrl),
        _payload_size(payload_size) {
    _data = new std::byte[_payload_size];
    std::fill(_data, _data + _payload_size, std::byte(0));
  }

  virtual ~MessageCommon() {
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

  virtual double *getCoordX() = 0;
  virtual double *getCoordY() = 0;
  virtual uint64_t *timestamp() = 0;
  virtual MAC::Tag *tag() = 0;

private:
  Addr _source_addr;
  Addr _dest_addr;
  Control _ctrl;
  std::size_t _payload_size;
  std::byte *_data;
} __attribute__((packed));

template <typename Addr>
class Message : public MessageCommon<Addr> {
public:
  using Base = MessageCommon<Addr>;

  Message(Addr src, Addr dst, std::size_t payload_size,
          Control ctrl = Control(Control::Type::COMMON))
      : Base(src, dst, payload_size, ctrl), _timestamp(0) {
  }

  Message(std::size_t payload_size,
          Control ctrl = Control(Control::Type::COMMON))
      : Base(payload_size, ctrl), _timestamp(0) {
  }

  ~Message() = default;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
  double *getCoordX() {
    return &_coord_x;
  }

  double *getCoordY() {
    return &_coord_y;
  }

  uint64_t *timestamp() {
    return &_timestamp;
  }
#pragma GCC diagnostic pop

  MAC::Tag *tag() {
    return &_tag;
  }

private:
  double _coord_x;
  double _coord_y;
  uint64_t _timestamp;
  MAC::Tag _tag{};
} __attribute__((packed));

template <typename Addr>
class IntraMessage : public MessageCommon<Addr> {
public:
  using Base = MessageCommon<Addr>;

  IntraMessage(Addr src, Addr dst, std::size_t payload_size,
               Control ctrl = Control(Control::Type::COMMON))
      : Base(src, dst, payload_size, ctrl) {
  }

  IntraMessage(std::size_t payload_size,
               Control ctrl = Control(Control::Type::COMMON))
      : Base(payload_size, ctrl) {
  }

  ~IntraMessage() = default;

  // TODO! Retornar algo nos campos abaixo.
  double *getCoordX() {
    return {};
  }

  double *getCoordY() {
    return {};
  }

  uint64_t *timestamp() {
    return {};
  }

  MAC::Tag *tag() {
    return {};
  }
} __attribute__((packed));

#endif
