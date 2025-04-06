#ifndef ENGINE_HH
#define ENGINE_HH

#include <cstring>
#include <iostream>
#include <stdexcept> // Para exceções

#include <arpa/inet.h>
#include <linux/if_packet.h>
#include <netinet/if_ether.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <unistd.h> // Para close, getpid
#include <cerrno>   // Para errno
#include <cstdio>   // Para perror

#include <fcntl.h>
#include <functional>
#include <signal.h>

// Forward declarations para evitar include circular desnecessário
#include "ethernet.hh"
template<typename> class Buffer; // Forward declare Buffer

class Engine {

public:
  // Construtor: Cria e configura socket e interface.
  Engine(const char *interface_name);

  // Destrutor: Fecha o socket.
  ~Engine();

  // Proíbe cópia e atribuição (gerencia recurso único: socket)
  Engine(const Engine &) = delete;
  Engine &operator=(const Engine &) = delete;

  // --- Métodos de Alocação/Liberação de Memória ---

  // Aloca memória bruta para um frame Ethernet.
  // Retorna: Ponteiro para a memória alocada (do tipo Ethernet::Frame*).
  // Lança exceção em caso de falha.
  // (Implementação atual usa 'new', futuras podem usar memória compartilhada)
  virtual Ethernet::Frame* allocate_frame_memory();

  // Libera a memória previamente alocada por allocate_frame_memory.
  // Args:
  //   frame_ptr: Ponteiro para a memória a ser liberada.
  virtual void free_frame_memory(Ethernet::Frame* frame_ptr);


  // --- Métodos de Rede ---

  // Envia dados contidos no buffer.
  // Opera sobre buf->data() e buf->size().
  template <typename Data>
  int send(const Buffer<Data> *buf) { // Pode ser const Buffer*
    if (!buf || !buf->data())
      return -1;

    // Configura endereço destino usando dados do buffer
    struct sockaddr_ll sadr_ll;
    std::memset(&sadr_ll, 0, sizeof(sadr_ll));
    sadr_ll.sll_family = AF_PACKET;
    sadr_ll.sll_ifindex = _interface_index;
    sadr_ll.sll_halen = ETH_ALEN;
    std::memcpy(sadr_ll.sll_addr, buf->data()->dst.mac, ETH_ALEN);

    int send_len = sendto(_socket_raw, buf->data(), buf->size(), 0,
                          (const struct sockaddr *)&sadr_ll, sizeof(sadr_ll));
    if (send_len < 0) {
      // Não imprimir erro aqui, deixa a camada superior (NIC) decidir
      // perror("Engine::send sendto error");
      return -1;
    }
    return send_len;
  }

  // Recebe dados no buffer fornecido.
  // Preenche buf->data(), ajusta buf->setSize(). Retorna endereço do remetente.
  template <typename Data>
  int receive(Buffer<Data> *buf, struct sockaddr_ll &sender_addr,
              socklen_t &sender_addr_len) {
    if (!buf || !buf->data()) return -1; // Validação

    // Usa buf->capacity() para saber o tamanho máximo da leitura
    int buflen = recvfrom(_socket_raw, buf->data(), buf->capacity(), 0,
                          (struct sockaddr *)&sender_addr,
                          (socklen_t *)&sender_addr_len);
    if (buflen < 0) {
      if (errno == EWOULDBLOCK || errno == EAGAIN) {
        buf->setSize(0);
        return 0; // Não bloqueante, sem dados agora
      } else {
        // perror("Engine::receive recvfrom error"); // Deixa NIC reportar
        buf->setSize(0);
        return -1; // Erro real
      }
    } else {
      buf->setSize(buflen); // Ajusta tamanho real recebido
      return buflen;
    }
    // Linha abaixo inalcançável, removida.
    // return buflen;
  }

  // Configura o handler de sinal (usando std::function como antes).
  void setupSignalHandler(std::function<void(int)> func);

  // Retorna o descritor do socket raw.
  int getSocketFd() const { return _socket_raw; }

  // Retorna o endereço MAC da interface gerenciada pela Engine.
  const Ethernet::Address & getAddress() const { return _address; }
  // Retorna o índice da interface.
  int getInterfaceIndex() const { return _interface_index; }


private:
  // Configura a recepção de sinais SIGIO.
  void confSignalReception();
  // Obtém informações da interface (MAC, índice) usando ioctl.
  bool get_interface_info();

  // Membros
  int _socket_raw;
  int _interface_index;
  const char * _interface_name; // Nome da interface (c-string)
  Ethernet::Address _address;   // MAC address da interface

  // Membros para signal handling com std::function
  std::function<void(int)> signalHandlerFunction;
  static void signalHandler(int signum); // Handler estático C
  static Engine *_self; // Ponteiro estático para instância (permite só uma Engine!)
};

#endif // ENGINE_HH