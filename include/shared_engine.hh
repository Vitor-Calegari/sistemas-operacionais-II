#ifndef SHARED_ENGINE_HH
#define SHARED_ENGINE_HH

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstring>

#include <arpa/inet.h>
#include <linux/filter.h>
#include <linux/if_packet.h>
#include <net/if.h>
#include <netinet/if_ether.h>
#include <netinet/in.h>
#include <queue>
#include <semaphore.h>
#include <semaphore>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <thread>

#include <fcntl.h>

#include "ethernet.hh"

template <typename Buffer>
class SharedEngine {
public:
  static const unsigned int BUFFER_SIZE = 1024;

  // Construtor: Cria e configura o socket raw.
  SharedEngine(const char *interface_name);

  // Destrutor: Fecha o socket.
  ~SharedEngine();

  // Envia dados usando um buffer pré-preenchido.
  // Args:
  //   buf: Ponteiro para o Buffer contendo os dados a serem enviados.
  //   sadr_ll: Ponteiro para a estrutura de endereço do destinatário
  //   (sockaddr_ll).
  // Returns:
  //   Número de bytes enviados ou -1 em caso de erro.
  int send(Buffer *buf) {
    if (buf == nullptr) {
      return -1; // Validação básica
    }

    empty.acquire();
    buffer_sem.acquire();

    eth_buf.push(buf);

    buffer_sem.release();
    full.release();

    return eth_buf.front()->size();
  }

  // Aloca memória bruta para um frame Ethernet.
  // Retorna: Ponteiro para a memória alocada (do tipo Ethernet::Frame*).
  // Lança exceção em caso de falha.
  // (Implementação atual usa 'new', futuras podem usar memória compartilhada)
  Buffer *allocate_frame_memory();

  // Libera a memória previamente alocada por allocate_frame_memory.
  // Args:
  //   frame_ptr: Ponteiro para a memória a ser liberada.
  void free_frame_memory(Buffer *frame_ptr);

  // Obtém informações da interface (MAC, índice) usando ioctl.
  bool get_interface_info();

  // Recebe dados do socket raw.
  // Args:
  //   buf: Referência a um Buffer onde os dados recebidos serão armazenados. O
  //   buffer deve ser pré-alocado com capacidade suficiente. sender_addr:
  //   Referência a uma estrutura sockaddr_ll onde o endereço do remetente será
  //   armazenado. sender_addr_len: Referência ao tamanho da estrutura de
  //   endereço do remetente (entrada/saída).
  // Returns:
  //   Número de bytes recebidos, 0 se não houver dados (não bloqueante), ou -1
  //   em caso de erro real.
  int receive(Buffer *buf) {
    full.acquire();
    buffer_sem.acquire();

    auto cur_buf = eth_buf.front();
    eth_buf.pop();

    std::copy(cur_buf, cur_buf + cur_buf->size(), buf);

    buffer_sem.release();
    empty.release();

    return buf->size();
  }

  // Configura o handler de sinal (SIGIO).
  // Args:
  //   func: Função que tratará o sinal.
  void setupSignalHandler();

  // Retorna o descritor do socket raw.
  // Necessário para operações como ioctl na classe NIC.
  int getSocketFd() const {
    return _socket_raw;
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
  void stopRecvThread();
  void turnRecvOn();

private:
  std::binary_semaphore buffer_sem{ 1 };
  std::counting_semaphore<> full{ 0 };
  std::counting_semaphore<> empty;

  std::queue<Buffer *> eth_buf;
  size_t eth_buf_idx = BUFFER_SIZE - 1;

  template <typename T, void (T::*handle_signal)()>
  static void handlerWrapper(void *obj) {
    T *typedObj = static_cast<T *>(obj);
    (typedObj->*handle_signal)();
  }

  // Configura a recepção de sinais SIGIO para o socket.
  void confSignalReception();

  // Socket é um inteiro pois seu valor representa um file descriptor
  int _socket_raw;
  int _interface_index;
  const char *_interface_name;
  Ethernet::Address _address;

  static void signalHandler(int sig);

  static void *obj;
  static void (*handler)(void *);

  static SharedEngine *_self;

  // ---- Controle da thread de recepcao ----
  std::thread recvThread;
  bool _thread_running;
  sem_t _engineSemaphore;
  pthread_mutex_t _threadStopMutex = PTHREAD_MUTEX_INITIALIZER;
};

#endif
