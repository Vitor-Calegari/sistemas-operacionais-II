#ifndef MESSAGE_HH
#define MESSAGE_HH

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>

template <typename Addr>
class Message {
public:
  enum Type : uint8_t { COMMON, PUBLISH, SUBSCRIBE };

public:
  Message(Addr src, Addr dst, std::size_t payload_size,
          Type type = Type::COMMON)
      : _source_addr(src), _dest_addr(dst), _msg_type(type),
        _payload_size(payload_size) {
    _data = new std::byte[_payload_size];
    std::fill(_data, _data + _payload_size, std::byte(0));
  }

  Message(std::size_t payload_size, Type type = Type::COMMON)
      : _source_addr(Addr()), _dest_addr(Addr()), _msg_type(type),
        _payload_size(payload_size) {
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

  void setType(uint8_t new_type) {
    _msg_type = static_cast<Type>(new_type);
  }

  Type *getType() {
    return &_msg_type;
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

private:
  Addr _source_addr;
  Addr _dest_addr;
  Type _msg_type;
  std::size_t _payload_size;
  std::byte *_data;
} __attribute__((packed));

#endif
