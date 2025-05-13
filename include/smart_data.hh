#ifndef SMART_DATA_HH
#define SMART_DATA_HH

#include "concurrent_observer.hh"
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
  typedef typename Transducer::Unit; // TODO talvez tenha que definir isso em
                                     // tranducer
public:
  // MTU disponível para data
  // TODO Implementar MTU no communicator e Header da Message para facilitar calculo de tamanho máximo
  // TODO Ou implementar outra forma de tipar o campo de dados
  inline static const unsigned int MTU = Communicator::MTU - sizeof(Message::Header);
  typedef unsigned char Data[MTU];
  class Packet {
  public:
    Packet() {
    }
    uint32_t unit;
    unsigned int period;
    template <typename T>
    T *data() {
      return reinterpret_cast<T *>(&_data);
    }

  private:
    Data _data;
  } __attribute__((packed));

public:
  SmartData(Communicator *communicator, Transducer *transd)
      : _communicator(communicator), _transd(transd) {
    _communicator->attatch(this, _transd->get_unit().get_int_unit());
  }

  ~SmartData() {
    _communicator->detach(this, _transd->get_unit().get_int_unit());
  }

  void subscribe() {
    // TODO Mandar mensagem de subscribe pelo Communicator
    return;
  }

  bool receive(Message *message) {
    // Block until a notification is triggered.
    Buffer *buf = Observer::updated();

    // TODO Mandar pro communicator para fazer unmarshal da mensagem

    int recv_size = 0;

    return recv_size > 0;
  }

  void initPeriocT() {
    // TODO É necessário implementar o novo sistema de publisher subscribe
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
          std::cout << (int)((unsigned char *)&data)[i] << " ";
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
      _communicator->free(buf);
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
  // --------------------------

  // Subscriber ---------------
  struct Subscriber {
    Address origin;
    unsigned int period;
  };
  std::vector<Subscriber> subscribers{};
  pthread_mutex_t _subscribersMutex = PTHREAD_MUTEX_INITIALIZER;
  // --------------------------

  Communicator *_communicator;
  Transducer *_transd;
};

#endif
