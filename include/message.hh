#ifndef MESSAGE_HH
#define MESSAGE_HH

#include <cstddef>

class Message {
public:
  Message(std::size_t msg_size);
  ~Message();

  std::byte *data() const;
  std::size_t size() const;

private:
  std::byte *_data;
  const std::size_t _size;
};

#endif
