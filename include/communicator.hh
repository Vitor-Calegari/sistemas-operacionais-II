#ifndef COMMUNICATOR_HH
#define COMMUNICATOR_HH

#include "concurrent_observer.hh"
#include "message.hh"

template <typename Channel>
class Communicator : public Concurrent_Observer<
                         typename Channel::Observer::Observed_Data,
                         typename Channel::Observer::Observing_Condition> {
  typedef Concurrent_Observer<typename Channel::Observer::Observed_Data,
                              typename Channel::Observer::Observing_Condition>
      Observer;

public:
  typedef typename Channel::Buffer Buffer;
  typedef typename Channel::Address Address;

public:
  Communicator(Channel *channel, Address address)
      : _channel(channel), _address(address) {
    _channel->attach(this, address);
  }

  ~Communicator() {
    Channel::detach(this, _address);
  }

  bool send(const Message *message) {
    return _channel->send(_address, Channel::Address::BROADCAST,
                          message->data(), message->size()) > 0;
  }

  bool receive(Message *message) {
    // Block until a notification is triggered.
    Buffer *buf = Observer::updated();

    Address from;
    int size = _channel->receive(buf, &from, message->data(), message->size());

    return size > 0;
  }

private:
  void update(typename Channel::Observed *obs,
              typename Channel::Observer::Observing_Condition c, Buffer *buf) {
    // Releases the thread waiting for data.
    Observer::update(c, buf);
  }

private:
  Channel *_channel;
  Address _address;
};

#endif
