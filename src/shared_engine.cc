#include "shared_engine.hh"
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <new>
#include <unistd.h>

SharedEngine *SharedEngine::_self = nullptr;
void *SharedEngine::obj = nullptr;
void (*SharedEngine::handler)(void *) = nullptr;

SharedEngine::SharedEngine(const char *interface_name, int buffer_size)
    : empty(buffer_size), _interface_name(interface_name),
      _thread_running(true) {
  _self = this;
  // AF_PACKET para receber pacotes incluindo cabeçalhos da camada de enlace
  // SOCK_RAW para criar um raw socket
  _socket_raw = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
  if (SharedEngine::getSocketFd() == -1) {
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
  if (setsockopt(SharedEngine::getSocketFd(), SOL_SOCKET, SO_ATTACH_FILTER,
                 &bpf_prog, sizeof(bpf_prog))) {
    perror("setsockopt");
    exit(1);
  }

  int broadcastEnable = 1;
  if (setsockopt(SharedEngine::getSocketFd(), SOL_SOCKET, SO_BROADCAST,
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

void SharedEngine::turnRecvOn() {
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

SharedEngine::~SharedEngine() {
  if (sem_destroy(&_engineSemaphore) != 0) {
    perror("sem_destroy");
    exit(EXIT_FAILURE);
  }

  if (recvThread.joinable()) {
    recvThread.join();
  }

  if (_socket_raw != -1) {
    close(_socket_raw);
  }

#ifdef DEBUG
  std::cout << "SharedEngine for interface " << _interface_name << " destroyed."
            << std::endl;
#endif
}

// Aloca memória para um frame. Placeholder com 'new'.
Buffer<Ethernet::Frame> *SharedEngine::allocate_frame_memory() {
  try {
    // Aloca e retorna ponteiro para um Ethernet::Frame.
    Buffer<Ethernet::Frame> *frame_ptr =
        new Buffer<Ethernet::Frame>(Ethernet::MAX_FRAME_SIZE_NO_FCS);
    frame_ptr->data()->clear();
    return frame_ptr;
  } catch (const std::bad_alloc &e) {
    std::cerr << "SharedEngine Error: Failed to allocate frame memory - "
              << e.what() << std::endl;
    throw;
  }
}

// Libera memória do frame. Placeholder com 'delete'.
void SharedEngine::free_frame_memory(Buffer<Ethernet::Frame> *frame_ptr) {
  if (frame_ptr != nullptr)
    delete frame_ptr;
}

bool SharedEngine::get_interface_info() {
  struct ifreq ifr;
  std::memset(&ifr, 0, sizeof(ifr));
  strncpy(ifr.ifr_name, _interface_name, IFNAMSIZ - 1);
  ifr.ifr_name[IFNAMSIZ - 1] = '\0'; // Garante terminação nula

  // Obter o endereço MAC (Hardware Address)
  if (ioctl(SharedEngine::getSocketFd(), SIOCGIFHWADDR, &ifr) == -1) {
    perror("SharedEngine Error: ioctl SIOCGIFHWADDR failed");
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
    std::cerr << "SharedEngine Error: Obtained MAC address is zero for "
              << _interface_name << std::endl;
    return false;
  }

  return true;
}

void SharedEngine::setupSignalHandler() {
  // Armazena a função de callback
  struct sigaction sigAction;
  sigAction.sa_handler = SharedEngine::signalHandler;
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

void SharedEngine::confSignalReception() {
  // Configura processo como ´dono´ do socket para poder receber
  // sinais, como o SIGIO
  if (fcntl(SharedEngine::getSocketFd(), F_SETOWN, getpid()) < 0) {
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
  int flags = fcntl(SharedEngine::getSocketFd(), F_GETFL);
  if (flags < 0) {
    perror("fcntl F_GETFL");
    exit(EXIT_FAILURE);
  }

  // O_ASYNC faz com que o socket levante o sinal SIGIO quando operacoes de I/O
  // acontecerem O_NONBLOCK faz com que operações normalmente bloqueantes não
  // bloqueiem
  if (fcntl(SharedEngine::getSocketFd(), F_SETFL,
            flags | O_ASYNC | O_NONBLOCK) < 0) {
    perror("fcntl F_SETFL");
    exit(EXIT_FAILURE);
  }
}

// Função estática para envelopar a função que tratará a interrupção
void SharedEngine::signalHandler([[maybe_unused]] int sig) {
  sem_post(&(_self->_engineSemaphore));
}

void SharedEngine::stopRecvThread() {
  pthread_mutex_lock(&_threadStopMutex);
  _thread_running = 0;
  pthread_mutex_unlock(&_threadStopMutex);
  sem_post(&_engineSemaphore);
}
