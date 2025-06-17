#ifndef SHARED_ENGINE_HH
#define SHARED_ENGINE_HH

#include "buffer.hh"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <thread>

#include <unordered_map>

#include "ethernet.hh"

template <typename DataWrapper>
class SharedEngine {
public:
  using FrameClass = DataWrapper;

public:
  // Construtor: Cria e configura o socket raw.
  SharedEngine(const char *interface_name) : _interface_name(interface_name) {
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
    int ret = -1;
    try {
      std::thread::id thread_id = std::this_thread::get_id();
      buffer_sem.acquire();
      unm_buf[thread_id] = *buf;
      buffer_sem.release();
      _self->handler(_self->obj);
      ret = buf->size();
    } catch (const std::exception &e) {
      ret = -1;
    }
    return ret;
  }

  // Recebe dados do socket raw.
  // Args:
  //   buf: Referência a um Buffer onde os dados recebidos serão armazenados. O
  //   buffer deve ser pré-alocado com capacidade suficiente.
  // Returns:
  //   Número de bytes recebidos, 0 se não houver dados (não bloqueante), ou -1
  //   em caso de erro real.
  int receive(Buffer *buf) {
    int ret = -1;
    Buffer *buf_temp = nullptr;
    try {
      std::thread::id thread_id = std::this_thread::get_id();
      buffer_sem.acquire();
      if (unm_buf.find(thread_id) != unm_buf.end()) {
        buf_temp = &unm_buf[thread_id];
        std::memcpy(buf->data<FrameClass::Frame>(),
                    buf_temp->data<FrameClass::Frame>(), buf_temp->size());
        buf->setSize(buf_temp->size());
        ret = buf->size();
        unm_buf.erase(thread_id);
      } else {
        ret = -1;
      }
      buffer_sem.release();
    } catch (const std::exception &e) {
      ret = -1;
    }
    return ret;
  }

public:
  const Ethernet::Address &getAddress() {
    return Ethernet::ZERO;
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
  std::unordered_map<std::thread::id, Buffer> unm_buf;

  template <typename T, void (T::*handle_signal)()>
  static void handlerWrapper(void *obj) {
    T *typedObj = static_cast<T *>(obj);
    (typedObj->*handle_signal)();
  }

  int _interface_index;
  const char *_interface_name;

  static void *obj;
  static void (*handler)(void *);
  static SharedEngine *_self;
};

template <typename DataWrapper>
SharedEngine<DataWrapper> *SharedEngine<DataWrapper>::_self = nullptr;

template <typename DataWrapper>
void *SharedEngine<DataWrapper>::obj = nullptr;

template <typename DataWrapper>
void (*SharedEngine<DataWrapper>::handler)(void *) = nullptr;

#endif
