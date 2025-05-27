#ifndef SMART_DATA_HH
#define SMART_DATA_HH

#include "concurrent_observer.hh"
#include "ethernet.hh"
#include "protocol.hh"
#include "smart_unit.hh"
#include "utils.hh"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <iostream>
#include <map>
#include <numeric>
#include <semaphore>
#include <thread>
#include <vector>

template <typename Communicator, typename Condition>
class SmartDataCommon
    : public Concurrent_Observer<
          typename Communicator::CommObserver::Observed_Data,
          typename Communicator::CommObserver::Observing_Condition> {
public:
  class Header {
  public:
    Header() : unit(0), period(0) {
    }
    uint32_t unit;
    uint32_t period;
  } __attribute__((packed));

  // MTU disponível para data
  inline static const unsigned int MTU = Communicator::MTU - sizeof(Header);
  typedef unsigned char Data[MTU];

  class PubPacket : public Header {
  public:
    PubPacket() {
      std::memset(_data, 0, sizeof(_data));
    }
    Header *header() {
      return this;
    }
    template <typename T>
    T *data() {
      return reinterpret_cast<T *>(&_data);
    }

  private:
    Data _data;
  } __attribute__((packed));

  class SubPacket : public Header {
  public:
    SubPacket() {
    }
  } __attribute__((packed));

public:
  SmartDataCommon(Communicator *communicator) : _communicator(communicator) {
  }

protected:
  Communicator *_communicator;
};

// Publisher.
template <typename Communicator, typename Condition, typename Transducer = void>
class SmartData : public SmartDataCommon<Communicator, Condition> {
public:
  typedef typename Communicator::CommMessage Message;
  typedef typename Communicator::Address Address;
  typedef typename Communicator::CommChannel Channel;

  using Base = SmartDataCommon<Communicator, Condition>;

public:
  static constexpr size_t PERIOD_SIZE = sizeof(uint32_t);
  static constexpr size_t UNIT_SIZE = Transducer::unit.get_value_size_bytes();

public:
  SmartData(Communicator *communicator, Transducer *transd, Condition cond)
      : Base(communicator), _transd(transd), _cond(cond) {
    Base::_communicator->attach(this, _cond);
    initPubThread();
  }

  ~SmartData() {
    if (_pub_thread_running) {
      stopPubThread();
    }
    Base::_communicator->detach(this, _cond);
  }

  void update([[maybe_unused]]
              typename Communicator::CommObserver::Observing_Condition c,
              typename Communicator::CommObserver::Observed_Data *buf) {
    Message *msg = (Message *)Base::_communicator->peek_msg(buf);

    // Obtem origem
    Address origin = *msg->sourceAddr();
    // Obtem periodo
    typename Base::SubPacket *sub_pkt =
        msg->template data<typename Base::SubPacket>();
    uint32_t new_period = sub_pkt->period;

    // Adiciona novo subscriber
    pthread_mutex_lock(&_subscribersMutex);
    // Check if subscriber already exists
    bool exists = false;
    for (const auto &sub : subscribers) {
      if (sub.origin == origin && sub.period == new_period) {
        exists = true;
        break;
      }
    }
    if (!exists) {
      subscribers.push_back(Subscriber{ origin, new_period });
#ifdef DEBUG_SMD
      std::cout << get_timestamp() << " Publisher " << getpid() << ": Updating"
                << std::endl;
      std::cout << "Subscriber " << origin << ' ' << new_period << " added"
                << std::endl;
#endif
      period_sem.acquire();
      if (period == 0) {
        period = new_period;
        has_first_subscriber_sem.release();
      } else {
        period = std::gcd(period, new_period);
      }
      highest_period = std::max(highest_period, new_period);
#ifdef DEBUG_SMD
      std::cout << "New pub period: " << period << std::endl;
#endif
      period_sem.release();
    }
    last_resub[Subscriber{ origin, new_period }] =
        std::chrono::steady_clock::now();
    pthread_mutex_unlock(&_subscribersMutex);

    // Libera buffer
    Base::_communicator->free(buf);
  }

private:
  void initPubThread() {
    _pub_thread_running = true;
    pub_thread = std::thread([this]() {
      if (period == 0) {
        has_first_subscriber_sem.acquire();
#ifdef DEBUG_SMD
        std::cout << get_timestamp() << " Publisher " << getpid()
                  << ": First Subscriber received" << std::endl;
#endif
      }

      while (_pub_thread_running) {
        for (int cur_period = period; _pub_thread_running;
             cur_period = cur_period + period > highest_period
                              ? period
                              : cur_period + period) {
          for (auto &sub : subscribers) {
            auto elapsed = last_resub[sub] - std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::microseconds>(elapsed) >
                std::chrono::microseconds(_resub_tolerance)) {
              last_resub.erase(sub);

              std::cout << "UNSUBSCRIBED " << sub.origin << ' ' << sub.period
                        << std::endl;
            }
          }
          period_sem.acquire();
          auto next_wakeup_t = std::chrono::steady_clock::now() +
                               std::chrono::microseconds(period);
          period_sem.release();
          std::byte data[UNIT_SIZE];
          _transd->get_data(data);
#ifdef DEBUG_SMD
          std::cout << get_timestamp() << " Publisher " << getpid()
                    << ": Publishing" << std::endl;
          std::cout << "Produced data: ";
          for (size_t i = 0; i < sizeof(data); i++) {
            std::cout << (int)((unsigned char *)&data)[i] << " ";
          }
          std::cout << std::endl;
#endif
          auto msg = create_pub_message(data, cur_period);
          Base::_communicator->send(&msg);

          std::this_thread::sleep_until(next_wakeup_t);
        }
      }
    });
  }

  void stopPubThread() {
    _pub_thread_running = false;
    if (pub_thread.joinable()) {
      pub_thread.join();
    }
  }

  Message create_pub_message(std::byte *data, uint32_t cur_period) {
    auto unit = _transd->get_unit();

    size_t msg_size =
        SmartUnit::SIZE_BYTES + PERIOD_SIZE + unit.get_value_size_bytes();

    Message msg(Base::_communicator->addr(), // TODO! Arrumar endereço físico.
                Address(Ethernet::Address(), Channel::BROADCAST_SID,
                        Channel::BROADCAST),
                msg_size, Control::Type::PUBLISH);

    auto int_unit = unit.get_int_unit();
    std::memcpy(msg.data(), &int_unit, SmartUnit::SIZE_BYTES);
    std::memcpy(msg.data() + SmartUnit::SIZE_BYTES, &cur_period, PERIOD_SIZE);
    std::memcpy(msg.data() + SmartUnit::SIZE_BYTES + PERIOD_SIZE, data,
                UNIT_SIZE);

    return msg;
  }

private:
  const uint32_t _resub_tolerance = 3 * 3e6;

  // Pub Thread ---------------
  std::atomic<bool> _pub_thread_running = false;
  uint32_t period = 0;
  uint32_t highest_period = 0;
  std::thread pub_thread;
  std::binary_semaphore has_first_subscriber_sem{ 0 };
  std::binary_semaphore period_sem{ 1 };
  // --------------------------

  // Subscriber List ---------------
  struct Subscriber {
    Address origin;
    uint32_t period;
    friend bool operator<(const Subscriber &lhs, const Subscriber &rhs) {
      return (lhs.origin < rhs.origin) ||
             (lhs.origin == rhs.origin && lhs.period < rhs.period);
    }
  };
  std::vector<Subscriber> subscribers{};
  pthread_mutex_t _subscribersMutex = PTHREAD_MUTEX_INITIALIZER;
  std::map<struct Subscriber, std::chrono::steady_clock::time_point> last_resub;
  // -------------------------------

  Transducer *_transd = nullptr;
  Condition _cond;
};

// Subscriber.
template <typename Communicator, typename Condition>
class SmartData<Communicator, Condition, void>
    : public SmartDataCommon<Communicator, Condition> {
public:
  typedef Concurrent_Observer<
      typename Communicator::CommObserver::Observed_Data,
      typename Communicator::CommObserver::Observing_Condition>
      Observer;
  typedef typename Communicator::Buffer Buffer;
  typedef typename Communicator::CommMessage Message;
  typedef typename Communicator::CommChannel Channel;
  typedef typename Communicator::Address Address;

  using Base = SmartDataCommon<Communicator, Condition>;

public:
  SmartData(Communicator *communicator, Condition cond)
      : Base(communicator), _cond(cond) {
    Base::_communicator->attach(this, _cond);
    subscribe(_cond.period);
  }

  ~SmartData() {
    if (_sub_thread_running) {
      stopSubThread();
    }
    Base::_communicator->detach(this, _cond);
    delete _sub_msg;
  }

  bool receive(Message *msg) {
    // Block until a notification is triggered.
    Buffer *buf = Observer::updated();
    return Base::_communicator->unmarshal(msg, buf);
  }

  void update(typename Communicator::CommObserver::Observing_Condition c,
              typename Communicator::CommObserver::Observed_Data *buf) {
#ifdef DEBUG_SMD
    std::cout << get_timestamp() << " Subscriber " << getpid() << std::endl;
#endif
    Observer::update(c, buf);
  }

private:
  void subscribe(uint32_t period) {
    _sub_msg = new Message(
        Base::_communicator->addr(),
        Address(
            Base::_communicator->addr().getPAddr(), // Nao precisa disso aqui
            Channel::BROADCAST_SID, Channel::BROADCAST),
        sizeof(typename Base::SubPacket), Control::Type::SUBSCRIBE);

    typename Base::SubPacket *pkt =
        (typename Base::SubPacket *)_sub_msg->data();
    pkt->unit = _cond.unit;
    pkt->period = period;

    initSubThread();
    return;
  }

  void initSubThread() {
    _sub_thread_running = true;
    _sub_thread = std::thread([this]() {
      while (_sub_thread_running) {
        auto next_wakeup_t = std::chrono::steady_clock::now() +
                             std::chrono::microseconds(_resub_period);
#ifdef DEBUG_SMD
        std::cout << get_timestamp() << "Subscriber " << getpid() << ": Resub"
                  << std::endl;
#endif
        Base::_communicator->send(_sub_msg);
        std::this_thread::sleep_until(next_wakeup_t);
      }
    });
  }

  void stopSubThread() {
    _sub_thread_running = false;
    if (_sub_thread.joinable()) {
      _sub_thread.join();
    }
  }

private:
  // Periodic Subscribe Thread ---------------
  std::atomic<bool> _sub_thread_running = false;
  const uint32_t _resub_period = 3e6;
  std::thread _sub_thread;
  Message *_sub_msg = nullptr;
  // -----------------------------------------

  Condition _cond;
};

#endif
