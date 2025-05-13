#ifndef COMMUNICATOR_HH
#define COMMUNICATOR_HH

#include "concurrent_observer.hh"

template <typename Channel, typename Message>
class Communicator : public Concurrent_Observer<
                         typename Channel::Observer::Observed_Data,
                         typename Channel::Observer::Observing_Condition> {
  typedef Concurrent_Observer<typename Channel::Observer::Observed_Data,
                              typename Channel::Observer::Observing_Condition>
      Observer;

public:
  typedef typename Channel::Buffer Buffer;
  typedef typename Channel::Address Address;
  typedef typename Channel::Port Port;

public:
  Communicator(Channel *channel, Port port)
      : _channel(channel),
        _address(Address(channel->getNICPAddr(), channel->getSysID(), port)) {
    _channel->attach(this, _address.getPort());
  }

  ~Communicator() {
    _channel->detach(this, _address.getPort());
  }

  Address addr() {
    return _address;
  }

  bool send(Message *message) {
    return _channel->send(*message->sourceAddr(), *message->destAddr(),
                          message->data(), message->size()) > 0;
  }

  bool receive(Message *message) {
    // Block until a notification is triggered.
    Buffer *buf = Observer::updated();

    int size =
        _channel->receive(buf, message->sourceAddr(), message->destAddr(),
                          message->data(), message->size());
    message->setSize(size);

    return size > 0;
  }

private:
  void update([[maybe_unused]] typename Channel::Observed *obs,
              typename Channel::Observer::Observing_Condition c, Buffer *buf) {
    // Releases the thread waiting for data.
    Observer::update(c, buf);
  }

private:
  Channel *_channel;
  Address _address;
};

#endif
