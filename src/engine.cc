#include "engine.hh"
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <new>
#include <unistd.h>

Engine *Engine::_self = nullptr;
void *Engine::obj = nullptr;
void (*Engine::handler)(void *, int) = nullptr;

Engine::Engine(const char *interface_name) : _interface_name(interface_name) {
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
  if (setsockopt(Engine::getSocketFd(), SOL_SOCKET, SO_ATTACH_FILTER, &bpf_prog,
                 sizeof(bpf_prog))) {
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
  if (::bind(_socket_raw, (struct sockaddr*)&sll_receive, sizeof(sll_receive)) < 0) {
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

Engine::~Engine() {
  if (_socket_raw != -1) {
    close(_socket_raw); // Fecha o socket ao destruir o objeto Engine
  }
#ifdef DEBUG
  std::cout << "Engine for interface " << _interface_name << " destroyed."
            << std::endl;
#endif
}

// Aloca memória para um frame. Placeholder com 'new'.
Buffer<Ethernet::Frame> *Engine::allocate_frame_memory() {
  try {
    // Aloca e retorna ponteiro para um Ethernet::Frame.
    Buffer<Ethernet::Frame> *frame_ptr =
        new Buffer<Ethernet::Frame>(Ethernet::MAX_FRAME_SIZE_NO_FCS);
    frame_ptr->data()->clear();
    return frame_ptr;
  } catch (const std::bad_alloc &e) {
    std::cerr << "Engine Error: Failed to allocate frame memory - " << e.what()
              << std::endl;
    throw;
  }
}

// Libera memória do frame. Placeholder com 'delete'.
void Engine::free_frame_memory(Buffer<Ethernet::Frame> *frame_ptr) {
  delete frame_ptr;
}

bool Engine::get_interface_info() {
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

void Engine::setupSignalHandler() {
  // Armazena a função de callback
  struct sigaction sigAction;
  sigAction.sa_handler = Engine::signalHandler;
  sigAction.sa_flags = SA_RESTART;

  // Limpa possiveis sinais existentes antes da configuracao
  sigemptyset(&sigAction.sa_mask);

  // Condigura sigaction de forma que, quando SIGIO for recebido,
  // ´function´ sera chamada
  // nullptr indica que nao queremos salvar a sigaction anterior
  if (sigaction(SIGIO, &sigAction, nullptr) < 0) {
    perror("sigaction");
    exit(EXIT_FAILURE);
  }
}

void Engine::confSignalReception() {
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

  // O_ASYNC faz com que o socket levante o sinal SIGIO quando operacoes de I/O
  // acontecerem O_NONBLOCK faz com que operações normalmente bloqueantes não
  // bloqueiem
  if (fcntl(Engine::getSocketFd(), F_SETFL, flags | O_ASYNC | O_NONBLOCK) < 0) {
    perror("fcntl F_SETFL");
    exit(EXIT_FAILURE);
  }
}

// Função estática para envelopar a função que tratará a interrupção
void Engine::signalHandler(int signum) {
  if (_self->handler) {
    _self->handler(_self->obj, signum);
  }
}
