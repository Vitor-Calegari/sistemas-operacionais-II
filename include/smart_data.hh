#ifndef SMART_DATA_HH
#define SMART_DATA_HH

#include "buffer.hh"
#include "concurrent_observer.hh"
#include "cond.hh"
#include "ethernet.hh"
#include "protocol.hh"
#include "smart_unit.hh"
#include "utils.hh"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <map>
#include <numeric>
#include <semaphore>
#include <thread>
#include <vector>

#ifdef DEBUG_DELAY
#include "clocks.hh"
#endif

template <typename Communicator, typename Condition>
class SmartDataCommon
    : public Concurrent_Observer<
          typename Communicator::CommObserver::Observed_Data,
          typename Communicator::CommObserver::Observing_Condition> {
public:

  enum PubType : uint8_t {
    INTERNAL,
    EXTERNAL,
    BOTH
  };

  class Header {
  public:
    Header() : unit(0), period(0) {
    }
    uint32_t unit;
    int64_t period;
  } __attribute__((packed));

  // MTU disponível para data
  inline static const unsigned int MTU = Communicator::MTU - sizeof(Header);
  typedef unsigned char Data[MTU];

  class SubPacket : public Header {
  public:
    SubPacket() : Header() {
    }
  } __attribute__((packed));

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
  using PubType = Base::PubType;
  using PubPacket = Base::PubPacket;

public:
  static constexpr size_t PERIOD_SIZE = sizeof(int64_t);
  static constexpr size_t UNIT_SIZE =
      Transducer::get_unit().get_value_size_bytes();

public:
  SmartData(Communicator *communicator, Transducer *transd, Condition cond,
            bool needExplicitSub = true)
      : Base(communicator), _transd(transd), _cond(cond),
        _needExplicitSub(needExplicitSub), _current_pub_type(PubType::BOTH) {
    Base::_communicator->attach(this, _cond);
    if (!needExplicitSub) {
      period = cond.period;
      highest_period = cond.period;
    }
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
    if (_needExplicitSub) {
      // Obtem origem
      Address origin = Base::_communicator->peek_msg_origin_addr(buf);
      // Obtem periodo
      typename Base::SubPacket *sub_pkt =
          (typename Base::SubPacket *)Base::_communicator->peek_msg_data(buf);

      int64_t new_period = sub_pkt->period;

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
      PubType sub_type = PubType::EXTERNAL;
      if (origin.getSysID() == Base::_communicator->addr().getSysID()) {
        sub_type = PubType::INTERNAL;
      }

      if (!exists) {
        subscribers.push_back(Subscriber{ origin, new_period, sub_type });
#ifdef DEBUG_SMD
        std::cout << get_timestamp() << " Publisher " << getpid()
                  << ": Updating" << std::endl;
        std::cout << "Subscriber " << origin << ' ' << new_period << " added"
                  << std::endl;
#endif
        period_sem.acquire();
        if (period == 0) {
          period = new_period;
          has_first_subscriber_sem.release();
        } else {
          period = std::gcd(period, new_period);
          updatePubType();
        }
        highest_period = std::max(highest_period, new_period);
#ifdef DEBUG_SMD
        std::cout << "New pub period: " << period << std::endl;
#endif
        period_sem.release();
      }
      last_resub[Subscriber{ origin, new_period, sub_type }] =
          std::chrono::steady_clock::now();
      pthread_mutex_unlock(&_subscribersMutex);
    }

    // Libera buffer
    Base::_communicator->free(buf);
  }

private:

  void updatePubType() {
    bool has_int = false;
    bool has_ext = false;
    for (size_t i = 0; i < subscribers.size(); ++i) {
      if (subscribers[i].type == PubType::INTERNAL) {
        has_int = true;
      } else if (subscribers[i].type == PubType::EXTERNAL) {
        has_ext = true;
      }
    }
    if (has_int && has_ext) {
      _current_pub_type = PubType::BOTH;
    } else if (has_int) {
      _current_pub_type = PubType::INTERNAL;
    } else if (has_ext) {
      _current_pub_type = PubType::EXTERNAL;
    }
  }

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

      auto now = std::chrono::steady_clock::now();
      int i = 0;
      for (int64_t cur_period = period; _pub_thread_running;
            cur_period = cur_period + period > highest_period
                            ? period
                            : cur_period + period, i++) {
        if (_needExplicitSub) { // Se tem sub explicito, precisa desinscrever
          std::vector<size_t> to_remove{};
          for (size_t i = 0; i < subscribers.size(); ++i) {
            auto &sub = subscribers[i];
            auto elapsed = std::chrono::steady_clock::now() - last_resub[sub];
            if (std::chrono::duration_cast<std::chrono::microseconds>(
                    elapsed) > std::chrono::microseconds(_resub_tolerance)) {
              last_resub.erase(sub);
              to_remove.push_back(i);
#ifdef DEBUG_SMD
              std::cout << "UNSUBSCRIBED " << sub.origin << ' ' << std::dec
                                        << sub.period << std::endl;
#endif
            }
          }
          period_sem.acquire();
          if (to_remove.size() != 0) {
            for (auto &ind : to_remove) {
              subscribers.erase(subscribers.begin() + ind);
            }
            highest_period = 0;
#ifdef DEBUG_SMD
            std::cout << "Old Period: " << std::dec << period << std::endl;
#endif
            period = 0;
            for (auto &sub : subscribers) {
              period = std::gcd(period, sub.period);
              highest_period = std::max(highest_period, sub.period);
            }
#ifdef DEBUG_SMD
            std::cout << "New Period: " << std::dec << period << std::endl;
#endif
            updatePubType();
          }
        }
        auto next_wakeup_t = now +
                              std::chrono::microseconds(period * i);
        if (_needExplicitSub) {
          period_sem.release();
        }
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
        Message msg = create_pub_message(data, cur_period);

        Base::_communicator->send(&msg);

        std::this_thread::sleep_until(next_wakeup_t);
      }
    });
  }

  void stopPubThread() {
    _pub_thread_running = false;
    if (pub_thread.joinable()) {
      pub_thread.join();
    }
  }

  Message create_pub_message(std::byte *data, int64_t cur_period) {
    auto unit = _transd->get_unit();

    size_t msg_size =
        SmartUnit::SIZE_BYTES + PERIOD_SIZE + unit.get_value_size_bytes();

    Address broadcast = Address();

    if (_current_pub_type == PubType::INTERNAL) {
      broadcast = Address(Base::_communicator->addr().getPAddr(), Base::_communicator->addr().getSysID(), Channel::BROADCAST);
    } else if (_current_pub_type == PubType::EXTERNAL) {
      broadcast = Address(Base::_communicator->addr().getPAddr(), Channel::EXT_BROADCAST, Channel::BROADCAST);
    } else if (_current_pub_type == PubType::BOTH) {
      broadcast = Address(Base::_communicator->addr().getPAddr(), Channel::UNIVERSAL_BROADCAST, Channel::BROADCAST);
    }

    Message msg(Base::_communicator->addr(),
                broadcast,
                msg_size, Control::Type::PUBLISH);

    PubPacket * pkt = (PubPacket *)msg.data();
    pkt->unit = unit.get_int_unit();
    pkt->period = cur_period;
    std::memcpy(pkt->template data<std::byte>(), data, UNIT_SIZE);
    return msg;
  }

private:
  const int64_t _resub_tolerance = 2 * 3e6;

  // Pub Thread ---------------
  std::atomic<bool> _pub_thread_running = false;
  int64_t period = 0;
  int64_t highest_period = 0;
  std::thread pub_thread;
  std::binary_semaphore has_first_subscriber_sem{ 0 };
  std::binary_semaphore period_sem{ 1 };
  // --------------------------

  // Subscriber List ---------------
  struct Subscriber {
    Address origin;
    int64_t period;
    PubType type;
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
  bool _needExplicitSub;
  PubType _current_pub_type;
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
  typedef typename Communicator::CommMessage Message;
  typedef typename Communicator::CommChannel Channel;
  typedef typename Communicator::Address Address;

  using Base = SmartDataCommon<Communicator, Condition>;

public:
  SmartData(Communicator *communicator, Condition cond,
            bool needExplicitSub = true)
      : Base(communicator), _cond(cond), _needExplicitSub(needExplicitSub) {
    Base::_communicator->attach(this, _cond);
    if (needExplicitSub) {
      subscribe(_cond.period);
    }
  }

  ~SmartData() {
    if (_sub_thread_running) {
      stopSubThread();
    }
    Base::_communicator->detach(this, _cond);
    if (_needExplicitSub) {
      delete _sub_msg;
    }
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
#ifdef DEBUG_DELAY
    int64_t send_at = Base::_communicator->peek_msg_timestamp(buf);
    int64_t recv_t = Base::_communicator->get_timestamp();
    int64_t delay = recv_t - send_at;
    GlobalTime &g = GlobalTime::getInstance();
    if (Base::_communicator->peek_msg_origin_addr(buf).getSysID() == Base::_communicator->addr().getSysID()) {
      _max_shared_delay = std::max(_max_shared_delay, delay);
      _min_shared_delay = std::min(_min_shared_delay, delay);
      double timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch() -
        std::chrono::microseconds(g.get_program_init())).count() / 1e6;
      std::pair<double, int64_t> delay_pair{timestamp, delay};
      _shared_delays.push_back(delay_pair);
    } else {
      _max_socket_delay = std::max(_max_socket_delay, delay);
      _min_socket_delay = std::min(_min_socket_delay, delay);
      double timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch() -
        std::chrono::microseconds(g.get_program_init())).count() / 1e6;
      std::pair<double, int64_t> delay_pair{timestamp, delay};
      _socket_delays.push_back(delay_pair);
    }
#endif
    Observer::update(c, buf);
  }
#ifdef DEBUG_DELAY
  std::vector<std::pair<double, int64_t>> get_socket_delays() {
    return _socket_delays;
  }
  std::vector<std::pair<double, int64_t>> get_shared_delays() {
    return _shared_delays;
  }
  int64_t get_max_socket_delay() { return _max_socket_delay; }
  int64_t get_min_socket_delay() { return _min_socket_delay; }
  int64_t get_max_shared_delay() { return _max_shared_delay; }
  int64_t get_min_shared_delay() { return _min_shared_delay; }
#endif
private:
  void subscribe(int64_t period) {
    _sub_msg = new Message(
        Base::_communicator->addr(),
        Address(
            Base::_communicator->addr().getPAddr(),
            Channel::UNIVERSAL_BROADCAST, Channel::BROADCAST),
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
  const int64_t _resub_period = 3e6;
  std::thread _sub_thread;
  Message *_sub_msg = nullptr;
  // -----------------------------------------

  Condition _cond;
  bool _needExplicitSub;
#ifdef DEBUG_DELAY
  std::vector<std::pair<double, int64_t>> _socket_delays{};
  std::vector<std::pair<double, int64_t>> _shared_delays{};
  int64_t _max_socket_delay = INT64_MIN;
  int64_t _min_socket_delay = INT64_MAX;
  int64_t _max_shared_delay = INT64_MIN;
  int64_t _min_shared_delay = INT64_MAX;
#endif
};

#endif
