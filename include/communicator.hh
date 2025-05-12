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
#include <atomic>
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
    std::size_t size = sizeof(Message) - 2 * sizeof(Address);
    std::vector<unsigned char> data(size);

    // Get message data
    uint32_t unit = message->getUnit()->get_int_unit();
    std::size_t payload_size = message->size();

    // Fill data array with message values
    int offset = 0;
    std::memcpy(data.data() + offset, message->getIsPub(), sizeof(bool));
    offset += sizeof(bool);
    std::memcpy(data.data() + offset, &unit, sizeof(uint32_t));
    offset += sizeof(uint32_t);
    std::memcpy(data.data() + offset, &payload_size, sizeof(std::size_t));
    offset += sizeof(std::size_t);
    std::memcpy(data.data() + offset, message->data(), payload_size);

    return _channel->send(*message->sourceAddr(), *message->destAddr(),
                          data.data(), size) > 0;
  }

  bool receive(Message *message) {
    // Block until a notification is triggered.
    Buffer *buf = Observer::updated();

    std::size_t data_size = sizeof(Message) - 2 * sizeof(Address);
    std::vector<unsigned char> data(data_size, 0);

    int recv_size = _channel->receive(buf, message->sourceAddr(),
                                      message->destAddr(), data.data(), data_size);

    // Copy IsPub
    int offset = 0;
    std::memcpy(message->getIsPub(), data.data() + offset, sizeof(bool));
    offset += sizeof(bool);
    // Copy Unit
    uint32_t unit = 0;
    std::memcpy(&unit, data.data() + offset, sizeof(uint32_t));
    *message->getUnit() = SmartUnit(unit);
    offset += sizeof(uint32_t);
    // Copy size
    std::size_t size = 0;
    std::memcpy(&size, data.data() + offset, sizeof(std::size_t));
    message->setSize(size);
    offset += sizeof(std::size_t);
    // Copy data
    std::size_t msg_recv_data_size = recv_size - offset;
    std::size_t payload_size = message->size() <= msg_recv_data_size ? message->size() : msg_recv_data_size;
    std::memcpy(message->data(), data.data() + offset, payload_size);

    message->setSize(payload_size);

    return recv_size > 0;
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
        std::cout << "Data: ";
        for (int i = 0; i < sizeof(data); i++) {
            std::cout << (int)((unsigned char*)&data)[i] << " ";
        }
        std::cout << std::endl;

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
    
    unsigned char * data = _channel->peekData(buf);
    bool isPub;
    std::size_t offset = 0;
    std::memcpy(&isPub, &data[offset], sizeof(bool));
    offset += sizeof(bool);
    if (isPub) {
      // Releases the thread waiting for data.
      Observer::update(c, buf);
    } else {
      // Filtra mensagens de subscribe que não são do tipo
      // produzido pelo transdutor do Communicator
      uint32_t unit;
      std::memcpy(&unit, &data[offset], sizeof(uint32_t));
      offset += sizeof(uint32_t);

      if (unit == _transd->get_unit().get_int_unit()) {
        // Obtem origem
        Address origin = _channel->peekOrigin(buf);
        // Obtem periodo
        std::size_t period_size;
        std::memcpy(&period_size, &data[offset], sizeof(std::size_t));
        offset += sizeof(std::size_t);
        unsigned int new_period;
        std::memcpy(&new_period, &data[offset], period_size);
  
        // Adiciona novo subscriber
        pthread_mutex_lock(&_subscribersMutex);
        subscribers.push_back(Subscriber{ origin, new_period });
        // TODO Remover cout
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
        // TODO Remover cout
        std::cout << "Period: " << period << std::endl;
        period_sem.release();
      }

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
  std::atomic<bool> _thread_running;
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
    std::size_t size = sizeof(Message) - 2 * sizeof(Address);
    std::vector<unsigned char> data(size);

    // Get message data
    bool *isPub = message->getIsPub();
    uint32_t unit = message->getUnit()->get_int_unit();
    std::size_t payload_size = message->size();

    // Fill data array with message values
    int offset = 0;
    std::memcpy(data.data() + offset, isPub, sizeof(bool));
    offset += sizeof(bool);
    std::memcpy(data.data() + offset, &unit, sizeof(uint32_t));
    offset += sizeof(uint32_t);
    std::memcpy(data.data() + offset, &payload_size, sizeof(std::size_t));
    offset += sizeof(std::size_t);
    std::memcpy(data.data() + offset, message->data(), payload_size);

    return _channel->send(*message->sourceAddr(), *message->destAddr(),
                          data.data(), size) > 0;
  }

  bool receive(Message *message) {
    // Block until a notification is triggered.
    Buffer *buf = Observer::updated();

    std::size_t data_size = sizeof(Message) - 2 * sizeof(Address);
    std::vector<unsigned char> data(data_size, 0);

    int recv_size = _channel->receive(buf, message->sourceAddr(),
                                      message->destAddr(), data.data(), data_size);

    // Copy IsPub
    int offset = 0;
    std::memcpy(message->getIsPub(), data.data() + offset, sizeof(bool));
    offset += sizeof(bool);
    // Copy Unit
    uint32_t unit = 0;
    std::memcpy(&unit, data.data() + offset, sizeof(uint32_t));
    *message->getUnit() = SmartUnit(unit);
    offset += sizeof(uint32_t);
    // Copy size
    std::size_t size = 0;
    std::memcpy(&size, data.data() + offset, sizeof(std::size_t));
    message->setSize(size);
    offset += sizeof(std::size_t);
    // Copy data
    std::size_t msg_recv_data_size = recv_size - offset;
    std::size_t payload_size = message->size() <= msg_recv_data_size ? message->size() : msg_recv_data_size;
    std::memcpy(message->data(), data.data() + offset, payload_size);

    message->setSize(payload_size);

    return recv_size > 0;
  }

  void update(typename Channel::Observer::Observing_Condition c,
              typename Channel::Observer::Observed_Data *buf) {
    unsigned char * data = _channel->peekData(buf);
    bool isPub;
    std::size_t offset = 0;
    std::memcpy(&isPub, &data[offset], sizeof(bool));
    if (isPub) {
      // Releases the thread waiting for data.
      Observer::update(c, buf);
    } else {
      // Libera buffer
      _channel->free(buf);
    }
  }

private:
  Channel *_channel;
  Address _address;
};

#endif
