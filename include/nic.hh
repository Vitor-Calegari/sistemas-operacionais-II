#ifndef NIC_HH
#define NIC_HH

#include <mutex>
#include <semaphore>

#include "buffer.hh"
#include "conditional_data_observer.hh"
#include "conditionally_data_observed.hh"
#include "ethernet.hh"

#ifdef DEBUG
#include "utils.hh"
#include <iostream>
#endif
// A classe NIC (Network Interface Controller).
// Ela age como a interface de rede, usando a Engine fornecida para E/S,
// e notifica observadores (Protocolos) sobre frames recebidos.
//
// D (Observed_Data): Buffer<Ethernet::Frame>* - Notifica com ponteiros para
// buffers recebidos.
// C (Observing_Condition): Ethernet::Protocol - Filtra observadores pelo
// EtherType.
template <typename Engine>
class NIC : public Ethernet,
            public Conditionally_Data_Observed<Buffer<Ethernet::Frame>,
                                               Ethernet::Protocol>,
            private Engine {
public:
  static const unsigned int SEND_BUFFERS = 1024;
  static const unsigned int RECEIVE_BUFFERS = 1024;

  typedef Ethernet::Header Header;
  typedef Ethernet::Address Address;
  typedef Ethernet::Protocol Protocol_Number;
  typedef Conditional_Data_Observer<Buffer<Ethernet::Frame>, Protocol_Number>
      Observer;
  typedef Conditionally_Data_Observed<Buffer<Ethernet::Frame>, Protocol_Number>
      Observed;
  typedef Buffer<Ethernet::Frame> BufferNIC;

  // Args:
  //   interface_name: Nome da interface de rede (ex: "eth0", "lo").
  NIC(const char *interface_name)
      : Engine(interface_name), last_used_send_buffer(SEND_BUFFERS - 1),
        last_used_recv_buffer(RECEIVE_BUFFERS - 1) {
    // Setup Handler -----------------------------------------------------

    Engine::template bind<NIC<Engine>, &NIC<Engine>::handle_signal>(this);

    // Inicializa pool de buffers ----------------------------------------

    try {
      for (unsigned int i = 0; i < SEND_BUFFERS; ++i) {
        // Cria buffers com a capacidade máxima definida
        _send_buffer_pool[i] = Engine::allocate_frame_memory();
      }
      for (unsigned int i = 0; i < RECEIVE_BUFFERS; ++i) {
        // Cria buffers com a capacidade máxima definida
        _recv_buffer_pool[i] = Engine::allocate_frame_memory();
      }
    } catch (const std::bad_alloc &e) {
      perror("NIC Error: buffer pool alloc");
      exit(EXIT_FAILURE);
    }

    Engine::turnRecvOn();
  }

  // Destrutor
  ~NIC() {
    handle_sem.acquire();
    Engine::stopRecvThread();
    for (unsigned int i = 0; i < SEND_BUFFERS; ++i) {
      Engine::free_frame_memory(_send_buffer_pool[i]);
    }
    for (unsigned int i = 0; i < RECEIVE_BUFFERS; ++i) {
      Engine::free_frame_memory(_recv_buffer_pool[i]);
    }
  }

  // Proibe cópia e atribuição para evitar problemas com ponteiros e estado.
  NIC(const NIC &) = delete;
  NIC &operator=(const NIC &) = delete;

  // Aloca um buffer do pool interno para envio ou recepção.
  // Retorna: Ponteiro para um Buffer livre, ou nullptr se o pool estiver
  // esgotado. NOTA: O chamador NÃO deve deletar o buffer, deve usar free()!
  BufferNIC *alloc(unsigned int size, int send) {
    unsigned int maxSize = Ethernet::HEADER_SIZE + size;

    unsigned int last_used_buffer =
        send ? last_used_send_buffer : last_used_recv_buffer;
    unsigned int buffer_size_l = send ? SEND_BUFFERS : RECEIVE_BUFFERS;
    BufferNIC **buffer_pool = send ? _send_buffer_pool : _recv_buffer_pool;

    for (unsigned int j = 0; j < buffer_size_l; ++j) {
      int i = (last_used_buffer + j) % buffer_size_l;
      if (!buffer_pool[i]->is_in_use()) {
        last_used_buffer = i;
        buffer_pool[i]->mark_in_use();
        // Mínimo de 60 bytes e máximo de Ethernet::MAX_FRAME_SIZE_NO_FCS
        // Tamanho minimo do quadro ethernet e 64 bytes, porem nao incluimos fcs
        // assim resultando em apenas 60 bytes
        buffer_pool[i]->setMaxSize(
            maxSize < Ethernet::MIN_FRAME_SIZE
                ? Ethernet::MIN_FRAME_SIZE
                : (maxSize > Ethernet::MAX_FRAME_SIZE_NO_FCS
                       ? Ethernet::MAX_FRAME_SIZE_NO_FCS
                       : maxSize));
        return buffer_pool[i]; // Retorna ponteiro para o buffer encontrado
      }
    }
// Se nenhum buffer livre foi encontrado
#ifdef DEBUG
    std::cerr << "NIC::alloc: Buffer pool exhausted!" << std::endl;
#endif
    return nullptr;
  }

  // Libera um buffer de volta para o pool.
  // Args:
  //   buf: Ponteiro para o buffer a ser liberado (deve ter sido obtido via
  //   alloc()).
  void free(BufferNIC *buf) {
    if (!buf)
      return;
    buf->data()->clear();
    buf->mark_free(); // Marca como livre e reseta o tamanho
  }

  // --- Funções da API Principal  ---

  // Envia um frame Ethernet contido em um buffer JÁ ALOCADO E PREENCHIDO pelo
  // chamador. O chamador é responsável pela alocação, ao fim do send o buffer
  // é liberado.
  // Args:
  //   buf: Ponteiro para o buffer contendo o frame a ser enviado.
  // Returns:
  //   Número de bytes enviados pela Engine ou -1 em caso de erro.
  int send(BufferNIC *buf) {
    int bytes_sent = Engine::send(buf);

    if (bytes_sent > 0) {
      _statistics.tx_packets++;
      _statistics.tx_bytes += bytes_sent;
#ifdef DEBUG
      std::cout << "NIC::send(buf): Sent " << bytes_sent << " bytes."
                << std::endl;
    } else {
      std::cerr << "NIC::send(buf): Engine failed to send packet." << std::endl;
#endif
    }

    free(buf);

    return bytes_sent;
  }

  // --- Métodos de Gerenciamento e Informação ---

  // Retorna o endereço MAC desta NIC.
  Address address() {
    return Engine::getAddress();
  }

  // Retorna as estatísticas de rede acumuladas.
  const Statistics &statistics() const {
    return _statistics;
  }

  int receive(BufferNIC *buf, void *data, unsigned int size) {
    std::memcpy(data, buf->data()->template data<char>(), size);
    return buf->size() - Ethernet::HEADER_SIZE;
  }

  // Método membro que processa o sinal (chamado pelo handler estático)
  void handle_signal() {
    handle_sem.acquire();
    int bytes_received = 0;
    do {
      // 1. Alocar um buffer para recepção.
      BufferNIC *buf = alloc(Ethernet::MAX_FRAME_SIZE_NO_FCS, 0);

      if (buf == nullptr) {
#ifdef DEBUG
        std::cout << "NIC buffer is full" << std::endl;
#endif
        break;
      }

      // 2. Tentar receber o pacote usando a Engine.
      bytes_received = Engine::receive(buf);
#ifdef DEBUG
      if (bytes_received > 0) {
        printEth(buf);
      }
#endif
      if (bytes_received > 0) {
        // Pacote recebido!
        _statistics.rx_packets++;
        _statistics.rx_bytes += bytes_received;
        bool notified = notify(buf->data()->prot, buf);
#ifdef DEBUG
        std::cout << "NIC::handle_signal: "
                  << (notified ? "Protocol Notificado"
                               : "Protocol Não notificado")
                  << std::endl;
#endif
        // Se NENHUM observador (Protocolo) estava interessado (registrado
        // para este EtherType), a NIC deve liberar o buffer que alocou.
        if (!notified)
          free(buf);

      } else if (bytes_received == 0) {
        // Não há mais pacotes disponíveis no momento (recvfrom retornaria 0
        // ou -1 com EAGAIN/EWOULDBLOCK).
        free(buf);
      } else { // bytes_received < 0
#ifdef DEBUG
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
          perror("NIC::handle_signal recvfrom error");
        }
#endif
        free(buf);
      }
    } while (bytes_received > 0);
    handle_sem.release();
  }

  // --- Membros ---
  Statistics _statistics; // Estatísticas de rede

  // Pool de Buffers
  BufferNIC *_send_buffer_pool[SEND_BUFFERS];
  BufferNIC *_recv_buffer_pool[RECEIVE_BUFFERS];
  unsigned int last_used_send_buffer;
  unsigned int last_used_recv_buffer;
  std::binary_semaphore handle_sem{ 1 };
};

#endif // NIC_HH
