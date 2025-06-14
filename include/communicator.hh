#ifndef COMMUNICATOR_HH
#define COMMUNICATOR_HH

#include "concurrent_observed.hh"
#include "concurrent_observer.hh"
#include "cond.hh"
#include "control.hh"
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
  typedef Concurrent_Observed<typename Channel::Observer::Observed_Data,
                              Condition>
      CommObserver;
  typedef typename Channel::Buffer Buffer;
  typedef typename Channel::Address Address;
  typedef typename Channel::Port Port;
  typedef Message CommMessage;
  typedef Channel CommChannel;

  static const unsigned int MTU = Channel::MTU;

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
    return _channel->send(*message->sourceAddr(), *message->destAddr(), *message->getControl(),
                          message->data(), message->size()) > 0;
  }

  bool receive(Message *message) {
    // Block until a notification is triggered.
    Buffer *buf = Observer::updated();
    return this->unmarshal(message, buf);
  }

  bool unmarshal(Message *message, Buffer *buf) {
    int size = _channel->receive(
        buf, message->sourceAddr(), message->destAddr(), message->getControl(),
        message->getCoordX(), message->getCoordY(), message->timestamp(),
        message->tag(), message->data(), message->size());
    message->setSize(size);
    return size > 0;
  }

  Address peek_msg_origin_addr(Buffer *buf) {
    return _channel->peekOrigin(buf);
  }

  char *peek_msg_data(Buffer *buf) {
    return _channel->peekPacketData(buf);
  }

  void free(Buffer *buf) {
    _channel->free(buf);
  }

private:
  void update(typename Channel::Observer::Observing_Condition c, Buffer *buf) {
    Control::Type type = _channel->getPType(buf);
    if (type == Control::Type::COMMON) {
      // Releases the thread waiting for data.
      Observer::update(c, buf);
    } else {
      Condition::Data *cond_data = (Condition::Data *)peek_msg_data(buf);
      bool isPub = type == Control::Type::PUBLISH;
      Condition cond = Condition(isPub, cond_data->unit, cond_data->period);
      if (!this->notify(cond, buf)) {
        _channel->free(buf);
      }
    }
  }

private:
  Channel *_channel;
  Address _address;
};

#endif
