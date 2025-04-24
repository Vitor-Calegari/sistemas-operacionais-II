#ifndef ENGINE_HH
#define ENGINE_HH

#include <cerrno>
#include <condition_variable>
#include <csignal>
#include <cstdio>
#include <cstring>

#include <arpa/inet.h>
#include <iostream>
#include <linux/filter.h>
#include <linux/if_packet.h>
#include <mutex>
#include <net/if.h>
#include <netinet/if_ether.h>
#include <netinet/in.h>
#include <semaphore.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <thread>

#include <fcntl.h>

#include "buffer.hh"
#include "ethernet.hh"

template <typename Buffer>
class Engine {

public:
  // Construtor: Cria e configura o socket raw.
  Engine(const char *interface_name)
      : _interface_name(interface_name), _thread_running(true),
        engine_lock(engine_lock_mutex) {
    _self = this;
    // AF_PACKET para receber pacotes incluindo cabeçalhos da camada de enlace
    // SOCK_RAW para criar um raw socket
    _socket_raw = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (Engine::getSocketFd() == -1) {
      perror("socket creation");
      exit(EXIT_FAILURE);
    }

    // Declara filtro BPF
    // Para adicionar mais protocolos, verificar documentação do BPF:
    // https://www.kernel.org/doc/Documentation/networking/filter.txt
    struct sock_filter bpf_code[] = {
      // Verifica o ethertype (ex: 0x88B5)
      { 0x28, 0, 0, 0x0000000c }, // ldh [12] (Load half word into A)
      { 0x15, 0, 1, 0x000088b5 }, // jeq 0x88B5? Se não, pula 1 instrução
      { 0x06, 0, 0, 0x0000ffff }, // Retorna o quadro
      { 0x06, 0, 0, 0x00000000 }, // Descarta quadro
    };

    struct sock_fprog bpf_prog = {
      .len = sizeof(bpf_code) / sizeof(bpf_code[0]),
      .filter = bpf_code,
    };

    // Tenta aplicar filtro
    if (setsockopt(Engine::getSocketFd(), SOL_SOCKET, SO_ATTACH_FILTER,
                   &bpf_prog, sizeof(bpf_prog))) {
      perror("setsockopt");
      exit(1);
    }

    int broadcastEnable = 1;
    if (setsockopt(Engine::getSocketFd(), SOL_SOCKET, SO_BROADCAST,
                   &broadcastEnable, sizeof(broadcastEnable)) < 0) {
      perror("setsockopt (SO_BROADCAST)");
      exit(EXIT_FAILURE);
    }

    if (!get_interface_info()) {
      // Tratamento de erro mais robusto pode ser necessário
      perror("NIC Error: interface info");
      exit(EXIT_FAILURE);
    }

    confSignalReception();

    setupSignalHandler();

    // Bind no socket de receive
    struct sockaddr_ll sll_receive;
    std::memset(&sll_receive, 0, sizeof(sll_receive));
    sll_receive.sll_family = AF_PACKET;
    sll_receive.sll_protocol = htons(ETH_P_ALL);
    sll_receive.sll_ifindex = if_nametoindex(_interface_name);
    if (::bind(_socket_raw, (struct sockaddr *)&sll_receive,
               sizeof(sll_receive)) < 0) {
      perror("bind (receive socket)");
      exit(EXIT_FAILURE);
    }

#ifdef DEBUG
    // Print Debug -------------------------------------------------------

    std::cout << "Engine initialized for interface " << _interface_name
              << " with MAC ";
    // Imprime o MAC Address (formatação manual)
    for (int i = 0; i < 6; ++i)
      std::cout << std::hex << (int)_address.mac[i] << (i < 5 ? ":" : "");
    std::cout << std::dec << " and index " << _interface_index << std::endl;
#endif
  }

  // Destrutor: Fecha o socket.
  ~Engine() {
    if (recvThread.joinable()) {
      recvThread.join();
    }

    if (_socket_raw != -1) {
      close(_socket_raw);
    }

#ifdef DEBUG
    std::cout << "Engine for interface " << _interface_name << " destroyed."
              << std::endl;
#endif
  }

  // Envia dados usando um buffer pré-preenchido.
  // Args:
  //   buf: Ponteiro para o Buffer contendo os dados a serem enviados.
  //   sadr_ll: Ponteiro para a estrutura de endereço do destinatário
  //   (sockaddr_ll).
  // Returns:
  //   Número de bytes enviados ou -1 em caso de erro.
  int send(Buffer *buf) {
    if (!buf)
      return -1; // Validação básica

    // Configura o endereço de destino para sendto
    struct sockaddr_ll sadr_ll;
    std::memset(&sadr_ll, 0, sizeof(sadr_ll));
    sadr_ll.sll_family = AF_PACKET;
    sadr_ll.sll_ifindex = _interface_index;
    sadr_ll.sll_halen = ETH_ALEN;
    std::memcpy(sadr_ll.sll_addr, buf->data()->dst.mac, ETH_ALEN);

    int send_len = sendto(_self->_socket_raw, buf->data(), buf->size(), 0,
                          (const sockaddr *)&sadr_ll, sizeof(sadr_ll));
    if (send_len < 0) {
#ifdef DEBUG
      printf("error in sending....sendlen=%d....errno=%d\n", send_len, errno);
#endif
      return -1;
    }
    return send_len;
  }

  // Obtém informações da interface (MAC, índice) usando ioctl.
  bool get_interface_info() {
    struct ifreq ifr;
    std::memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, _interface_name, IFNAMSIZ - 1);
    ifr.ifr_name[IFNAMSIZ - 1] = '\0'; // Garante terminação nula

    // Obter o endereço MAC (Hardware Address)
    if (ioctl(Engine::getSocketFd(), SIOCGIFHWADDR, &ifr) == -1) {
      perror("Engine Error: ioctl SIOCGIFHWADDR failed");
      return false;
    }

    // Copia o endereço MAC da estrutura ifreq para o membro _address
    // Usa o construtor de Address que recebe unsigned char[6]
    _address = Ethernet::Address(
        reinterpret_cast<const unsigned char *>(ifr.ifr_hwaddr.sa_data));

    // Caso a interface utilizada seja loopback, geralmente o endereço dela é
    // 00:00:00:00:00:00 Verifica se o MAC obtido não é zero
    if (strcmp(_interface_name, "lo") != 0 &&
        !_address) { // Usa o operator bool() da Address
      std::cerr << "Engine Error: Obtained MAC address is zero for "
                << _interface_name << std::endl;
      return false;
    }

    return true;
  }

  // Recebe dados do socket raw.
  // Args:
  //   buf: Referência a um Buffer onde os dados recebidos serão armazenados. O
  //   buffer deve ser pré-alocado com capacidade suficiente.
  // Returns:
  //   Número de bytes recebidos, 0 se não houver dados (não bloqueante), ou -1
  //   em caso de erro real.
  int receive(Buffer *buf) {
    struct sockaddr_ll sender_addr;
    socklen_t sender_addr_len = sizeof(sender_addr);

    int buflen = recvfrom(_self->_socket_raw, buf->data(), buf->maxSize(), 0,
                          (struct sockaddr *)&sender_addr,
                          (socklen_t *)&sender_addr_len);
    if (buflen < 0) {
      // Erro real ou apenas indicação de não bloqueio?
      if (errno == EWOULDBLOCK || errno == EAGAIN) {
        buf->setSize(0); // Nenhum dado recebido agora
        return 0;        // Não é um erro fatal em modo não bloqueante
      } else {
        perror("Engine::receive recvfrom error");
        buf->setSize(0); // Indica erro zerando o tamanho
        return -1;       // Erro real
      }
    } else {
      // Dados recebidos com sucesso, ajusta o tamanho real do buffer.
      buf->setSize(buflen);
      return buflen;
    }
    return buflen;
  }
  // Configura o handler de sinal (SIGIO).
  // Args:
  //   func: Função que tratará o sinal.
  void setupSignalHandler() {
    // Armazena a função de callback
    struct sigaction sigAction;
    sigAction.sa_handler = Engine::signalHandler;
    sigAction.sa_flags = SA_RESTART;

    // Limpa possiveis sinais existentes antes da configuracao
    sigemptyset(&sigAction.sa_mask);

    // Configura sigaction
    // nullptr indica que nao queremos salvar a sigaction anterior
    if (sigaction(SIGIO, &sigAction, nullptr) < 0) {
      perror("sigaction");
      exit(EXIT_FAILURE);
    }
  }

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

  void stopRecv() {
    pthread_mutex_lock(&_threadStopMutex);
    _thread_running = 0;
    pthread_mutex_unlock(&_threadStopMutex);

    if (engine_lock.owns_lock()) {
      engine_lock.unlock();
    }
    engine_cond.notify_one();
  }

  void turnRecvOn() {
    recvThread = std::thread([this]() {
      while (true) {
        engine_cond.wait(engine_lock);
        pthread_mutex_lock(&_threadStopMutex);
        if (!_thread_running) {
          break;
        }
        pthread_mutex_unlock(&_threadStopMutex);
        _self->handler(_self->obj);
      }
    });
  }

private:
  template <typename T, void (T::*handle_signal)()>
  static void handlerWrapper(void *obj) {
    T *typedObj = static_cast<T *>(obj);
    (typedObj->*handle_signal)();
  }

  // Configura a recepção de sinais SIGIO para o socket.
  void confSignalReception() {
    // Configura processo como ´dono´ do socket para poder receber
    // sinais, como o SIGIO
    if (fcntl(Engine::getSocketFd(), F_SETOWN, getpid()) < 0) {
      perror("fcntl F_SETOWN");
      exit(EXIT_FAILURE);
    }

    // Set interfacace index -------------------------------------
    struct ifreq ifr;
    strncpy(ifr.ifr_name, _interface_name, IFNAMSIZ - 1);
    if (ioctl(_socket_raw, SIOCGIFINDEX, &ifr) == -1) {
      perror("ioctl SIOCGIFINDEX");
      exit(EXIT_FAILURE);
    }
    _interface_index = ifr.ifr_ifindex;

    // Obtem flags do socket para não sobrescreve-las posteriormente
    int flags = fcntl(Engine::getSocketFd(), F_GETFL);
    if (flags < 0) {
      perror("fcntl F_GETFL");
      exit(EXIT_FAILURE);
    }

    // O_ASYNC faz com que o socket levante o sinal SIGIO quando operacoes de
    // I/O acontecerem O_NONBLOCK faz com que operações normalmente bloqueantes
    // não bloqueiem
    if (fcntl(Engine::getSocketFd(), F_SETFL, flags | O_ASYNC | O_NONBLOCK) <
        0) {
      perror("fcntl F_SETFL");
      exit(EXIT_FAILURE);
    }
  }

  // Socket é um inteiro pois seu valor representa um file descriptor
  int _socket_raw;
  int _interface_index;
  const char *_interface_name;
  Ethernet::Address _address;

  // Função estática para envelopar a função que tratará a interrupção
  static void signalHandler([[maybe_unused]] int sig) {
    if (_self->engine_lock.owns_lock()) {
      _self->engine_lock.unlock();
    }
    _self->engine_cond.notify_one();
  }

  static void *obj;
  static void (*handler)(void *);

  static Engine *_self;

  // ---- Controle da thread de recepcao ----
  std::thread recvThread;
  bool _thread_running;
  std::condition_variable engine_cond;
  std::mutex engine_lock_mutex;
  std::unique_lock<std::mutex> engine_lock;
  pthread_mutex_t _threadStopMutex = PTHREAD_MUTEX_INITIALIZER;
};

template <typename Buffer>
Engine<Buffer> *Engine<Buffer>::_self = nullptr;

template <typename Buffer>
void *Engine<Buffer>::obj = nullptr;

template <typename Buffer>
void (*Engine<Buffer>::handler)(void *) = nullptr;

#endif
