#ifndef ENGINE_HH
#define ENGINE_HH

#include <cstring>
#include <iostream>

#include <arpa/inet.h>
#include <linux/if_packet.h>
#include <netinet/if_ether.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>

#include <fcntl.h>
#include <functional>
#include <signal.h>

#include "ethernet.hh"
#include "buffer.hh"

class Engine {

public:
  // Construtor: Cria e configura o socket raw.
  Engine(const char *interface_name);

  // Destrutor: Fecha o socket.
  ~Engine();

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
    std::memcpy(sadr_ll.sll_addr, buf->data()->dst.mac, ETH_ALEN);

    int send_len = sendto(_self->_socket_raw, buf->data(), buf->size(), 0,
                          (const sockaddr *)&sadr_ll, sizeof(sadr_ll));
    if (send_len < 0) {
      printf("error in sending....sendlen=%d....errno=%d\n", send_len, errno);
      return -1;
    }
    return send_len;
  }

  // Obtém informações da interface (MAC, índice) usando ioctl.
  bool get_interface_info();

  // *************TODO**************
  // Ainda está errado os parâmetros
  // A NIC tem 2 receive e 2 send, qual deles é da engine?
  // Porque o receive da engine teria parametros?
  // Será que sempre vai ser na mesma interface de rede?
  // Mensagens transferidas em broadcast são recebidas pela mesma engine que as
  // enviou, como lidar?
  // *******************************
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
    int buflen = recvfrom(_self->_socket_raw, buf->data(), buf->maxSize(), 0,
                          (struct sockaddr *)&sender_addr,
                          (socklen_t *)&sender_addr_len);
    std::cout << buf << std::endl;
    if (buflen < 0) {
      // Erro real ou apenas indicação de não bloqueio?
      if (errno == EWOULDBLOCK || errno == EAGAIN) {
        buf->setSize(0); // Nenhum dado recebido agora
        return 0;       // Não é um erro fatal em modo não bloqueante
      } else {
        perror("Engine::receive recvfrom error");
        buf->setSize(0); // Indica erro zerando o tamanho
        return -1;      // Erro real
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
  void setupSignalHandler(std::function<void(int)> func);

// protected:
  // Retorna o descritor do socket raw.
  // Necessário para operações como ioctl na classe NIC.
  int getSocketFd() const {
    return _socket_raw;
  }

public:
  const Ethernet::Address & getAddress() { return _address; }

private:
  // Configura a recepção de sinais SIGIO para o socket.
  void confSignalReception();

  // Socket é um inteiro pois seu valor representa um file descriptor
  int _socket_raw;
  int _interface_index;
  const char * _interface_name;
  Ethernet::Address _address;

  static void signalHandler(int signum);

  std::function<void(int)> signalHandlerFunction;
  
  static Engine *_self;
};

#endif
