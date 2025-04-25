#ifndef SHARED_ENGINE_HH
#define SHARED_ENGINE_HH

#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstring>

#include <queue>
#include <semaphore>

#include "ethernet.hh"

template <typename Buffer>
class SharedEngine {
public:
  static const unsigned int BUFFER_SIZE = 1024;

  // Construtor: Cria e configura o socket raw.
  SharedEngine(const char *interface_name)
      : empty(1024), _interface_name(interface_name) {
    _self = this;

#ifdef DEBUG
    // Print Debug -------------------------------------------------------
    std::cout << "SharedEngine initialized for interface " << _interface_name
              << std::endl;
#endif
  }

  // Destrutor: Fecha o socket.
  ~SharedEngine() {
#ifdef DEBUG
    std::cout << "SharedEngine for interface " << _interface_name
              << " destroyed." << std::endl;
#endif
  }

  // Envia dados usando um buffer pré-preenchido.
  // Args:
  //   buf: Ponteiro para o Buffer contendo os dados a serem enviados.
  // Returns:
  //   Número de bytes enviados ou -1 em caso de erro.
  int send(Buffer *buf) {
    bool acquired = empty.try_acquire();
    if (acquired) {
      buffer_sem.acquire();
      eth_buf.push(*buf);
      buffer_sem.release();
      full.release();

      if (obj == nullptr) {
        perror("Handler not binded");
        exit(EXIT_FAILURE);
      }

      _self->handler(_self->obj);
      return eth_buf.front().size();
    } else {
      return -1;
    }
  }

  // Recebe dados do socket raw.
  // Args:
  //   buf: Referência a um Buffer onde os dados recebidos serão armazenados. O
  //   buffer deve ser pré-alocado com capacidade suficiente.
  // Returns:
  //   Número de bytes recebidos, 0 se não houver dados (não bloqueante), ou -1
  //   em caso de erro real.
  int receive(Buffer *buf) {
    bool acquired = full.try_acquire();
    if (acquired) {
      buffer_sem.acquire();

      auto &cur_buf = eth_buf.front();
      std::memcpy(buf->data(), cur_buf.data(), cur_buf.size());
      buf->setSize(cur_buf.size());
      eth_buf.pop();
  
      buffer_sem.release();
      empty.release();
      return buf->size();
    } else {
      return -1;
    }
  }

public:
  const Ethernet::Address &getAddress() {
    return _address;
  }

  template <typename T, void (T::*handle_signal)()>
  static void bind(T *obj) {
    _self->obj = obj;
    _self->handler = &handlerWrapper<T, handle_signal>;
  }

  void stopRecv() {
    return;
  }

  void turnRecvOn() {
    return;
  }

private:
  std::binary_semaphore buffer_sem{ 1 };
  std::counting_semaphore<> full{ 0 };
  std::counting_semaphore<> empty;

  std::queue<Buffer> eth_buf;
  size_t eth_buf_idx = BUFFER_SIZE - 1;

  template <typename T, void (T::*handle_signal)()>
  static void handlerWrapper(void *obj) {
    T *typedObj = static_cast<T *>(obj);
    (typedObj->*handle_signal)();
  }

  // Socket é um inteiro pois seu valor representa um file descriptor
  int _interface_index;
  const char *_interface_name;
  Ethernet::Address _address;

  static void *obj;
  static void (*handler)(void *);
  static SharedEngine *_self;
};

template <typename Buffer>
SharedEngine<Buffer> *SharedEngine<Buffer>::_self = nullptr;

template <typename Buffer>
void *SharedEngine<Buffer>::obj = nullptr;

template <typename Buffer>
void (*SharedEngine<Buffer>::handler)(void *) = nullptr;

#endif
