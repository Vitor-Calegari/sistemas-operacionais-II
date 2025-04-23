#include "shared_engine.hh"
#include <algorithm>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <new>
#include <unistd.h>

template <typename Buffer>
SharedEngine<Buffer> *SharedEngine<Buffer>::_self = nullptr;

template <typename Buffer>
void *SharedEngine<Buffer>::obj = nullptr;

template <typename Buffer>
void (*SharedEngine<Buffer>::handler)(void *) = nullptr;

template <typename Buffer>
SharedEngine<Buffer>::SharedEngine(const char *interface_name)
    : empty(1024), _interface_name(interface_name), _thread_running(true) {
  _self = this;
  std::fill(eth_buf, eth_buf + BUFFER_SIZE, nullptr);

  if (sem_init(&_engineSemaphore, 0, 0) != 0) {
    perror("sem_init");
    exit(EXIT_FAILURE);
  }

#ifdef DEBUG
  // Print Debug -------------------------------------------------------

  std::cout << "SharedEngine initialized for interface " << _interface_name
            << " with MAC ";
  // Imprime o MAC Address (formatação manual)
  for (int i = 0; i < 6; ++i)
    std::cout << std::hex << (int)_address.mac[i] << (i < 5 ? ":" : "");
  std::cout << std::dec << " and index " << _interface_index << std::endl;
#endif
}

template <typename Buffer>
void SharedEngine<Buffer>::turnRecvOn() {
  recvThread = std::thread([this]() {
    while (1) {
      sem_wait(&_engineSemaphore);
      pthread_mutex_lock(&_threadStopMutex);
      if (!_thread_running) {
        break;
      }
      pthread_mutex_unlock(&_threadStopMutex);
      _self->handler(_self->obj);
    }
  });
}

template <typename Buffer>
SharedEngine<Buffer>::~SharedEngine() {
  if (sem_destroy(&_engineSemaphore) != 0) {
    perror("sem_destroy");
    exit(EXIT_FAILURE);
  }

  if (recvThread.joinable()) {
    recvThread.join();
  }

  // if (_socket_raw != -1) {
  //   close(_socket_raw);
  // }

#ifdef DEBUG
  std::cout << "SharedEngine for interface " << _interface_name << " destroyed."
            << std::endl;
#endif
}

// Aloca memória para um frame. Placeholder com 'new'.
template <typename Buffer>
Buffer *SharedEngine<Buffer>::allocate_frame_memory() {
  /*try {
    // Aloca e retorna ponteiro para um Ethernet::Frame.
    Buffer<Ethernet::Frame> *frame_ptr =
        new Buffer<Ethernet::Frame>(Ethernet::MAX_FRAME_SIZE_NO_FCS);
    frame_ptr->data()->clear();
    return frame_ptr;
  } catch (const std::bad_alloc &e) {
    std::cerr << "SharedEngine Error: Failed to allocate frame memory - "
              << e.what() << std::endl;
    throw;
  }*/
  return nullptr;
}

// Libera memória do frame. Placeholder com 'delete'.
template <typename Buffer>
void SharedEngine<Buffer>::free_frame_memory(Buffer *frame_ptr) {
  if (frame_ptr != nullptr)
    delete frame_ptr;
}

// Função estática para envelopar a função que tratará a interrupção
template <typename Buffer>
void SharedEngine<Buffer>::signalHandler([[maybe_unused]] int sig) {
  sem_post(&(_self->_engineSemaphore));
}

template <typename Buffer>
void SharedEngine<Buffer>::stopRecvThread() {
  pthread_mutex_lock(&_threadStopMutex);
  _thread_running = 0;
  pthread_mutex_unlock(&_threadStopMutex);
  sem_post(&_engineSemaphore);
}
