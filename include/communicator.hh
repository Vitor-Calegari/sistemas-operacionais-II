#ifndef COMMUNICATOR_HH
#define COMMUNICATOR_HH

#include "concurrent_observer.hh"
#include <vector>
#include <thread>
#include <numeric>
#include <cstring>
// Publisher.
template <typename Channel, typename Message, typename Transducer = void>
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
  Communicator(Channel *channel, Port port, Transducer *transd)
      : _thread_running(0), _channel(channel),
        _address(Address(channel->getNICPAddr(), channel->getSysID(), port),
        _transd(transd)) {
    _channel->attach(this, _address.getPort());
  }

  ~Communicator() {
    if (_thread_running) {
      stopPThread();
    }
    _channel->detach(this, _address.getPort());
  }

  Address addr() { return _address; }

  bool send(Message *message) {
    return _channel->send(*message->sourceAddr(), *message->destAddr(),
                          message->data(), message->size()) > 0;
  }

  void initPeriocT() {
    _thread_running = true;
    pThread = std::thread([this]() {
      while (_thread_running) {
        auto next_wakeup_t = std::chrono::steady_clock::now() + std::chrono::milliseconds(period);
        // TODO A thread poderia acordar a cada intervalo porém só envia quem ta no tempo correto
        Message msg = Message(addr(),
                              Address(),
                              _transd->getUnit(),
                              true);
        std::memcpy(msg->data(), _transd->get_data(), _transd->getUnit().get_n());
        pthread_mutex_lock(&_subscribersMutex);
        std::vector<Subscriber> subs = subscribers;
        pthread_mutex_unlock(&_subscribersMutex);
        for (auto subscriber : subs) {
          *(msg->destAddr()) = subscriber.origin;
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
      // Novo periodo
      unsigned int new_period = _channel->peekPeriod(buf);
      period = std::gcd(period, new_period);
      Address origin = _channel->peekOrigin(buf);
      // Adiciona novo subscriber
      pthread_mutex_lock(&_subscribersMutex);
      subscribers.emplace(origin, period);
      pthread_mutex_unlock(&_subscribersMutex);
      // Libera buffer
      _channel->free(buf);
    }
  }

  void stopPThread() {
    _thread_running = 0;
    if (pThread.joinable()) {
      pThread.join();
    }
  }

private:
  // Thread -------------------
  bool _thread_running;
  unsigned int period = 0;
  std::thread pThread;
  // -------------------------
  Channel *_channel;
  Address _address;
  Transducer *_transd;

  struct Subscriber {
    Address origin;
    unsigned int period;
  };

  std::vector<Subscriber> subscribers{};
  pthread_mutex_t _subscribersMutex = PTHREAD_MUTEX_INITIALIZER;
};

// Subscriber.
template <typename Channel, typename Message>
class Communicator<Channel, Message, void>
    : public Concurrent_Observer<
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

  Address addr() { return _address; }

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

private:
  void update([[maybe_unused]] typename Channel::Observed *obs,
              typename Channel::Observer::Observing_Condition c, Buffer *buf) {
    // Assumimos que aqui só chegam mensagens de publish
    // pois subscribes são filtrados durante a pilha
    Observer::update(c, buf);
  }

private:
  Channel *_channel;
  Address _address;
};

#endif
