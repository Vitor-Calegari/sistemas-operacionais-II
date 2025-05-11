#ifndef COMMUNICATOR_HH
#define COMMUNICATOR_HH

#include "concurrent_observer.hh"
#include "smart_unit.hh"
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <iostream>
#include <numeric>
#include <semaphore>
#include <thread>
#include <vector>
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
        _address(Address(channel->getNICPAddr(), channel->getSysID(), port)),
        _transd(transd) {
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
    uint32_t unit = message->getUnit()->get_int_unit();
    return _channel->send(*message->sourceAddr(), *message->destAddr(),
                          *message->getIsPub(), unit, message->data(),
                          message->size()) > 0;
  }

  bool receive(Message *message) {
    // Block until a notification is triggered.
    Buffer *buf = Observer::updated();

    uint32_t unit = 0;

    int size = _channel->receive(buf, message->sourceAddr(),
                                 message->destAddr(), message->getIsPub(),
                                 &unit, message->data(), message->size());
    message->setUnit(SmartUnit(unit));
    message->setSize(size);

    return size > 0;
  }

  void initPeriocT() {
    _thread_running = true;
    pThread = std::thread([this]() {
      if (period == 0) {
        has_first_subscriber_sem.acquire();
      }

      for (int num_micro_steps = 0; _thread_running;) {
        period_sem.acquire();
        auto next_wakeup_t = std::chrono::steady_clock::now() +
                             std::chrono::microseconds(period);
        auto cur_period = period;
        std::cout << "Cur time: " << num_micro_steps << std::endl;
        period_sem.release();

        int data = _transd->get_data();
        std::cout << "Data: " << data << std::endl;

        pthread_mutex_lock(&_subscribersMutex);
        std::vector<Subscriber> subs = subscribers;
        pthread_mutex_unlock(&_subscribersMutex);

        for (auto subscriber : subs) {
          if (num_micro_steps % subscriber.period == 0) {
            Message msg(addr(), subscriber.origin,
                        _transd->get_unit().get_value_size_bytes(), true,
                        _transd->get_unit());
            std::memcpy(msg.data(), &data, sizeof(data));
            send(&msg);
            std::cout << "Enviou ao " << subscriber.origin << std::endl;
          }
        }

        std::this_thread::sleep_until(next_wakeup_t);
        num_micro_steps += cur_period;
      }
    });
  }

  void update(typename Channel::Observer::Observing_Condition c,
              typename Channel::Observer::Observed_Data *buf) {
    bool isPub = _channel->peekIsPub(buf);
    if (isPub) {
      // Releases the thread waiting for data.
      Observer::update(c, buf);
    } else {
      Address origin = _channel->peekOrigin(buf);
      unsigned int new_period = _channel->peekPeriod(buf);

      // Adiciona novo subscriber
      pthread_mutex_lock(&_subscribersMutex);
      subscribers.push_back(Subscriber{ origin, new_period });
      std::cout << "Subscriber " << origin << ' ' << new_period << " added"
                << std::endl;
      pthread_mutex_unlock(&_subscribersMutex);

      period_sem.acquire();
      if (period == 0) {
        period = new_period;
        has_first_subscriber_sem.release();
      } else {
        period = std::gcd(period, new_period);
      }
      std::cout << "Period: " << period << std::endl;
      period_sem.release();

      // Libera buffer
      _channel->free(buf);
    }
  }

private:
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

  std::binary_semaphore has_first_subscriber_sem{ 0 };
  std::binary_semaphore period_sem{ 1 };
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

  Address addr() {
    return _address;
  }

  bool send(Message *message) {
    uint32_t unit = message->getUnit()->get_int_unit();
    return _channel->send(*message->sourceAddr(), *message->destAddr(),
                          *message->getIsPub(), unit, message->data(),
                          message->size()) > 0;
  }

  bool receive(Message *message) {
    // Block until a notification is triggered.
    Buffer *buf = Observer::updated();

    uint32_t unit = 0;

    int size = _channel->receive(buf, message->sourceAddr(),
                                 message->destAddr(), message->getIsPub(),
                                 &unit, message->data(), message->size());
    message->setUnit(SmartUnit(unit));
    message->setSize(size);

    return size > 0;
  }

  void update(typename Channel::Observer::Observing_Condition c,
              typename Channel::Observer::Observed_Data *buf) {
    bool isPub = _channel->peekIsPub(buf);
    if (isPub) {
      // Releases the thread waiting for data.
      Observer::update(c, buf);
    } else {
      // Libera buffer
      _channel->free(buf);
    }
  }

private:
private:
  Channel *_channel;
  Address _address;
};

#endif
