#ifndef MESSAGE_HH
#define MESSAGE_HH

#include <algorithm>
#include <cstddef>
template <typename Addr>
class Message {
public:
  Message(Addr src, Addr dst, std::size_t payload_size)
      : _source_addr(src), _dest_addr(dst), _payload_size(payload_size) {
    _data = new std::byte[_payload_size];
    std::fill(_data, _data + _payload_size, std::byte(0));
  }

  Message(std::size_t payload_size)
      : _source_addr(Addr()), _dest_addr(Addr()), _payload_size(payload_size) {
    _data = new std::byte[_payload_size];
    std::fill(_data, _data + _payload_size, std::byte(0));
  }

  ~Message() {
    delete[] _data;
  }

  Addr * sourceAddr() { return &_source_addr; }

  Addr * destAddr() { return &_dest_addr; }

  void setSize(std::size_t new_size) { _payload_size = new_size; }

  std::size_t size() {
    return _payload_size;
  }
  
  std::byte *data() const {
    return _data;
  }

private:
  Addr _source_addr;
  Addr _dest_addr;
  std::size_t _payload_size;
  std::byte *_data;
};

#endif
