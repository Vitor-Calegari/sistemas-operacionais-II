#ifndef COMMUNICATOR_HH
#define COMMUNICATOR_HH

#include "concurrent_observer.hh"
#include <thread>
#include <numeric>
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
      : _thread_running(0), _channel(channel),
        _address(Address(channel->getNICPAddr(), channel->getSysID(), port)) {
    _channel->attach(this, _address.getPort());
  }

  ~Communicator() {
    if (_thread_running) {
      stopPThread();
    }
    _channel->detach(this, _address.getPort());
  }

  Address addr() {
    return _address;
  }

  bool send(Message *message) {
    return _channel->send(*message->sourceAddr(), *message->destAddr(),
                          *message->getIsPub(), message->data(),
                          message->size()) > 0;
  }

  bool receive(Message *message) {
    // Block until a notification is triggered.
    Buffer *buf = Observer::updated();

    int size = _channel->receive(buf, message->sourceAddr(),
                                 message->destAddr(), message->getIsPub(),
                                 message->data(), message->size());
    message->setSize(size);

    return size > 0;
  }

  void initPeriocT() {
    _thread_running = true;
    pThread = std::thread([this]() {
      while (_thread_running) {
        auto next_wakeup_t = std::chrono::steady_clock::now() + std::chrono::milliseconds(interval);
        // TODO Send to all subscribers
        // TODO A thread poderia acordar a cada intervalo porém só envia quem ta no tempo correto
        Message msg = Message(comm.addr(),
                              Address(),
                              _type_size,
                              true);
        std::memcpy(msg->data(), data, _type_size);
        for (auto subscriber : subscribers) {
          *msg->destAddr() = subscriber.addr;
          send(msg);
        }
        std::this_thread::sleep_until(next_wakeup_t);
      }
    });
  }

private:
  void update([[maybe_unused]] typename Channel::Observed *obs,
              typename Channel::Observer::Observing_Condition c, Buffer *buf) {
    bool isPub = _channel->peekIsPub(buf);
    if (isPub) {
      // Releases the thread waiting for data.
      Observer::update(c, buf);
    } else {
      unsigned int new_inter = _channel->peekInterval(buf);
      interval = std::gdc(interval, new_inter);
      // TODO Adiciona na lista de subscribers
    }
  }

  void stopPThread() {
    _thread_running = 0;
    if (pThread.joinable()) {
      pThread.join();
    }
  }

private:
  unsigned int _type_size = 5;  // TODO Colocar type_size de acordo com o tipo do dado produzido
  bool _thread_running;
  unsigned int interval;
  std::thread pThread;
  Channel *_channel;
  Address _address;
};

#endif
