#include "message.hh"

#include <cstddef>

Message::Message(std::size_t msg_size) : _size(msg_size) {
  _data = new std::byte[_size];
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
