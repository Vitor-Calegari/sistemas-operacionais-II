#ifndef COMMUNICATOR_HH
#define COMMUNICATOR_HH

#include "concurrent_observer.hh"
#include "concurrent_observed.hh"
#include "cond.hh"
#include <iostream>

template <typename Channel, typename Message>
class Communicator
    : public Concurrent_Observer<
          typename Channel::Observer::Observed_Data,
          typename Channel::Observer::Observing_Condition>,
      public Concurrent_Observed<typename Channel::Observer::Observed_Data,
      Condition> {
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
    uint8_t type = *message->getType();
    return _channel->send(*message->sourceAddr(), *message->destAddr(),
    type,
                          message->data(), message->size()) > 0;
  }

  bool receive(Message *message) {
    // Block until a notification is triggered.
    Buffer *buf = Observer::updated();
    uint8_t type;
    int size =
        _channel->receive(buf, message->sourceAddr(), message->destAddr(),
        &type,
    message->data(), message->size());
    message->setType(type);
    message->setSize(size);
    return size > 0;
  }

private:
  void update(typename Channel::Observer::Observing_Condition c, Buffer *buf) {
    Message msg = Message(Channel::MTU, Message::Type::COMMOM);
    _channel->unmarshal(&msg, buf);
    if (*msg.getType() == Message::Type::COMMOM) {
      std::cout << "A" << std::endl;
      // Releases the thread waiting for data.
      Observer::update(c, buf);
    } else {
      Condition *cond = (Condition *)(msg.data());
      if (!this->notify(*cond, buf)) {
        _channel->free(buf);
      }
    }
  }

private:
  Channel *_channel;
  Address _address;
};

#endif
