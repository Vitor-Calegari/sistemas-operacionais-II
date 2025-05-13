#ifndef SMART_DATA_HH
#define SMART_DATA_HH

#include "concurrent_observer.hh"
#include "protocol.hh"
#include "smart_unit.hh"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <iostream>
#include <numeric>
#include <semaphore>
#include <thread>
#include <vector>

// Publisher.
template <typename Communicator, typename Transducer = void>
class SmartData : public Concurrent_Observer<
                      typename Communicator::CommObserver::Observed_Data,
                      typename Communicator::CommObserver::Observed_Condition> {
public:
  typedef Concurrent_Observer<
      typename Communicator::CommObserver::Observed_Data,
      typename Communicator::CommObserver::Observing_Condition>
      Observer;
  typedef typename Communicator::Buffer Buffer;
  typedef typename Communicator::Message Message;
  typedef typename Communicator::Address Address;
  typedef typename Communicator::Channel Channel;

public:
  // MTU disponível para data
  // TODO Implementar MTU no communicator e Header da Message para facilitar
  // calculo de tamanho máximo
  // TODO Ou implementar outra forma de tipar o campo de dados
  inline static const unsigned int MTU =
      Communicator::MTU - sizeof(Message::Header);
  typedef unsigned char Data[MTU];

  static constexpr size_t PERIOD_SIZE = sizeof(uint32_t);

  class Header {
  public:
    Header() : unit(0), period(0) {
    }
    uint32_t unit;
    uint32_t period;
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

  class SubPacket : public Header {
  public:
    SubPacket() {
    }
  } __attribute__((packed));

public:
  SmartData(Communicator *communicator, Transducer *transd)
      : _communicator(communicator), _transd(transd) {
    _communicator->attatch(this, _transd->get_unit().get_int_unit());
  }

  ~SmartData() {
    if (_sub_thread_running) {
      stopSubThread();
    }
    if (_sub_thread_running) {
      stopPubThread();
    }
    _communicator->detach(this, _transd->get_unit().get_int_unit());
  }

  void subscribe(uint32_t period) {
    _sub_msg = Message(
        _communicator->addr(),
        Address(_communicator->addr().getPAddr(), // Nao precisa disso aqui
                Communicator::Channel::BROADCAST_SID,
                Communicator::Channel::BROADCAST),
        Message::Type::SUBSCRIBE, sizeof(SubPacket));

    SubPacket *pkt = (SubPacket *)_sub_msg->data();
    pkt->unit = _transd->get_data()->get_int_unit();
    pkt->period = period;

    initSubThread();
    return;
  }

  bool receive([[maybe_unused]] Message *message) {
    // Block until a notification is triggered.
    Buffer *buf = Observer::updated();

    // TODO Mandar pro communicator para fazer unmarshal da mensagem

    int recv_size = 0;

    return recv_size > 0;
  }

  void update(typename Communicator::Observer::Observing_Condition c,
              typename Communicator::Observer::Observed_Data *buf) {
    // TODO Implementar peek no communicator que repasse para o peek do protocol
    unsigned char *data = _communicator->peekData(buf);
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
        Address origin = _communicator->peekOrigin(buf);
        // Obtem periodo
        std::size_t period_size;
        std::memcpy(&period_size, &data[offset], sizeof(std::size_t));
        offset += sizeof(std::size_t);
        uint32_t new_period;
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
        highest_period = std::min(highest_period, new_period);
        // TODO Remover cout
        std::cout << "Pub period: " << period << std::endl;
        period_sem.release();
      }

      // Libera buffer
      _communicator->free(buf);
    }
  }

private:
  Message create_pub_message(int data, uint32_t cur_period) {
    auto unit = _transd->get_unit();

    size_t msg_size =
        sizeof(SmartUnit) + PERIOD_SIZE + unit.get_value_size_bytes();

    Message msg(
        _communicator->addr(),
        Communicator::Address(_communicator->_channel->_rsnic->address(),
                              Channel::BROADCAST_SID, Channel::BROADCAST),
        Message::Type::PUBLISH, msg_size);

    std::memcpy(msg.data(), &unit.get_int_unit(), sizeof(SmartUnit));
    std::memcpy(msg.data() + sizeof(SmartUnit), &cur_period, PERIOD_SIZE);
    std::memcpy(msg.data() + sizeof(SmartUnit) + PERIOD_SIZE, &data,
                unit.get_value_size_bytes());

    return msg;
  }

  void initPubThread() {
    _pub_thread_running = true;
    pub_thread = std::thread([this]() {
      if (period == 0) {
        has_first_subscriber_sem.acquire();
      }

      for (int cur_period = 0; _pub_thread_running;
           cur_period = cur_period = (cur_period + 1) % highest_period) {
        period_sem.acquire();
        auto next_wakeup_t = std::chrono::steady_clock::now() +
                             std::chrono::microseconds(period);
        period_sem.release();

        int data = _transd->get_data();
        std::cout << "Produced data: ";
        for (size_t i = 0; i < sizeof(data); i++) {
          std::cout << (int)((unsigned char *)&data)[i] << " ";
        }
        std::cout << std::endl;

        auto msg = create_pub_message(data, cur_period);
        _communicator->send(&msg);

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

  void initSubThread() {
    _sub_thread_running = true;
    _sub_thread = std::thread([this]() {
      while (_sub_thread_running) {
        auto next_wakeup_t = std::chrono::steady_clock::now() +
                             std::chrono::microseconds(_sub_period);
        _communicator->send(&_sub_msg);
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
  // Pub Thread ---------------
  std::atomic<bool> _pub_thread_running = false;
  uint32_t period = 0;
  uint32_t highest_period = 0;
  std::thread pub_thread;
  std::binary_semaphore has_first_subscriber_sem{ 0 };
  std::binary_semaphore period_sem{ 1 };
  // --------------------------

  // Periodic Subscribe Thread ---------------
  std::atomic<bool> _sub_thread_running = false;
  const uint32_t _sub_period = 1e6;
  std::thread _sub_thread;
  Message _sub_msg;
  // -----------------------------------------

  // Subscriber ---------------
  struct Subscriber {
    Address origin;
    uint32_t period;
  };
  std::vector<Subscriber> subscribers{};
  pthread_mutex_t _subscribersMutex = PTHREAD_MUTEX_INITIALIZER;
  // --------------------------

  Communicator *_communicator;
  Transducer *_transd;
};

#endif
