#ifndef SHARED_ENGINE_HH
#define SHARED_ENGINE_HH

#include <cerrno>
#include <cstdio>
#include <cstring>

#include <arpa/inet.h>
#include <linux/filter.h>
#include <linux/if_packet.h>
#include <net/if.h>
#include <netinet/if_ether.h>
#include <netinet/in.h>
#include <semaphore.h>
#include <semaphore>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <thread>

#include <fcntl.h>

#include "buffer.hh"
#include "ethernet.hh"

class SharedEngine {

public:
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
  template <typename Data>
  int send(Buffer<Data> *buf) {
    if (!buf)
      return -1; // Validação básica

    // Configura o endereço de destino para sendto
    struct sockaddr_ll sadr_ll;
    std::memset(&sadr_ll, 0, sizeof(sadr_ll));
    sadr_ll.sll_family = AF_PACKET;
    sadr_ll.sll_ifindex = _interface_index;
    sadr_ll.sll_halen = ETH_ALEN;

    empty.acquire();
    buffer_sem.acquire();
    std::memcpy(sadr_ll.sll_addr, buf->data()->dst.mac, ETH_ALEN);

    int send_len = sendto(_self->_socket_raw, buf->data(), buf->size(), 0,
                          (const sockaddr *)&sadr_ll, sizeof(sadr_ll));
    buffer_sem.release();
    full.release();

    if (send_len < 0) {
#ifdef DEBUG
      printf("error in sending....sendlen=%d....errno=%d\n", send_len, errno);
#endif
      return -1;
    }
    return send_len;
  }

  // Aloca memória bruta para um frame Ethernet.
  // Retorna: Ponteiro para a memória alocada (do tipo Ethernet::Frame*).
  // Lança exceção em caso de falha.
  // (Implementação atual usa 'new', futuras podem usar memória compartilhada)
  Buffer<Ethernet::Frame> *allocate_frame_memory();

  // Libera a memória previamente alocada por allocate_frame_memory.
  // Args:
  //   frame_ptr: Ponteiro para a memória a ser liberada.
  void free_frame_memory(Buffer<Ethernet::Frame> *frame_ptr);

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
  template <typename Data>
  int receive(Buffer<Data> *buf, struct sockaddr_ll &sender_addr,
              socklen_t &sender_addr_len) {
    full.acquire();
    buffer_sem.acquire();

    int buflen = recvfrom(_self->_socket_raw, buf->data(), buf->maxSize(), 0,
                          (struct sockaddr *)&sender_addr,
                          (socklen_t *)&sender_addr_len);

    if (buflen < 0) {
      buf->setSize(0); // Nenhum dado recebido agora
      buffer_sem.release();
      empty.release();

      // Erro real ou apenas indicação de não bloqueio?
      if (errno == EWOULDBLOCK || errno == EAGAIN) {
        return 0; // Não é um erro fatal em modo não bloqueante
      } else {
        perror("SharedEngine::receive recvfrom error");
        return -1; // Erro real
      }
    } else {
      // Dados recebidos com sucesso, ajusta o tamanho real do buffer.
      buf->setSize(buflen);
    }

    buffer_sem.release();
    empty.release();
    return buflen;
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
