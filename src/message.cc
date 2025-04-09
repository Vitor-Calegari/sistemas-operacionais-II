#include "message.hh"
#include <algorithm>
#include <cstddef>

Message::Message(std::size_t msg_size) : _size(msg_size) {
  _data = new std::byte[_size];
  std::fill(_data, _data + _size, std::byte(0));
}

Message::~Message() {
  delete[] _data;
}

std::byte *Message::data() const {
  return _data;
}

std::size_t Message::size() const {
  return _size;
}
