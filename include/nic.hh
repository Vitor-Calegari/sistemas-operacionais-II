#ifndef NIC_HH
#define NIC_HH

#include <mutex>
#include <semaphore>

#include "buffer.hh"
#include "conditional_data_observer.hh"
#include "conditionally_data_observed.hh"

#ifdef DEBUG
#include "utils.hh"
#include <iostream>
#endif
// A classe NIC (Network Interface Controller).
// Ela age como a interface de rede, usando a Engine fornecida para E/S,
// e notifica observadores (Protocolos) sobre frames recebidos.
//
// D (Observed_Data): Buffer<Engine::FrameClass::Frame>* - Notifica com ponteiros para
// buffers recebidos.
// C (Observing_Condition): Engine::FrameClass::Protocol - Filtra observadores pelo
// EtherType.
template <typename Engine>
class NIC : public Engine::FrameClass,
            public Conditionally_Data_Observed<Buffer,
            typename Engine::FrameClass::Protocol>,
            private Engine {
public:
  static const unsigned int SEND_BUFFERS = 1024;
  static const unsigned int RECEIVE_BUFFERS = 1024;

  typedef typename Engine::FrameClass::Statistics Statistics;

  typedef Engine::FrameClass NICFrameClass;
  typedef Engine::FrameClass::Header Header;
  typedef Engine::FrameClass::Address Address;
  typedef Engine::FrameClass::Protocol Protocol_Number;
  typedef Conditional_Data_Observer<Buffer, Protocol_Number>
      Observer;
  typedef Conditionally_Data_Observed<Buffer, Protocol_Number>
      Observed;

  // Args:
  //   interface_name: Nome da interface de rede (ex: "eth0").
  NIC(const char *interface_name)
      : Engine(interface_name), last_used_send_buffer(SEND_BUFFERS - 1),
        last_used_recv_buffer(RECEIVE_BUFFERS - 1) {
    Buffer::BufferType buf_type{};
    if (typeid(NICFrameClass) == typeid(Ethernet)) {
      buf_type = Buffer::BufferType::EthernetFrame;
    } else {
      buf_type = Buffer::BufferType::SharedMemFrame;
    }
    // Inicializa pool de buffers ----------------------------------------
    // Cria buffers com a capacidade máxima definida
    for (unsigned int i = 0; i < SEND_BUFFERS; ++i) {
      _send_buffer_pool[i] = Buffer(buf_type);
    }
    for (unsigned int i = 0; i < RECEIVE_BUFFERS; ++i) {
      _recv_buffer_pool[i] = Buffer(buf_type);
    }

    // Setup Handler -----------------------------------------------------
    Engine::template bind<NIC<Engine>, &NIC<Engine>::handle_signal>(this);
  }

  // Destrutor
  ~NIC() {}

  // Proibe cópia e atribuição para evitar problemas com ponteiros e estado.
  NIC(const NIC &) = delete;
  NIC &operator=(const NIC &) = delete;

  // Aloca um buffer do pool interno para envio ou recepção.
  // Retorna: Ponteiro para um Buffer livre, ou nullptr se o pool estiver
  // esgotado. NOTA: O chamador NÃO deve deletar o buffer, deve usar free()!
  Buffer *alloc(unsigned int size, int send) {
    std::lock_guard<std::mutex> lock(alloc_mtx);
    unsigned int maxSize = Engine::FrameClass::HEADER_SIZE + size;

    unsigned int last_used_buffer =
        send ? last_used_send_buffer : last_used_recv_buffer;
    unsigned int buffer_size_l = send ? SEND_BUFFERS : RECEIVE_BUFFERS;
    Buffer *buffer_pool = send ? _send_buffer_pool : _recv_buffer_pool;

    for (unsigned int j = 0; j < buffer_size_l; ++j) {
      int i = (last_used_buffer + j) % buffer_size_l;
      if (!buffer_pool[i].is_in_use()) {
        last_used_buffer = i;
        buffer_pool[i].mark_in_use();
        // Mínimo de 60 bytes e máximo de Engine::FrameClass::MAX_FRAME_SIZE_NO_FCS
        // Tamanho minimo do quadro Engine::FrameClass e 64 bytes, porem nao incluimos fcs
        // assim resultando em apenas 60 bytes
        buffer_pool[i].setMaxSize(
            maxSize < Engine::FrameClass::MIN_FRAME_SIZE
                ? Engine::FrameClass::MIN_FRAME_SIZE
                : (maxSize > Engine::FrameClass::MAX_FRAME_SIZE_NO_FCS
                       ? Engine::FrameClass::MAX_FRAME_SIZE_NO_FCS
                       : maxSize));
        return &buffer_pool[i]; // Retorna ponteiro para o buffer encontrado
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
  void free(Buffer *buf) {
    if (!buf)
      return;
    buf->template data<typename NICFrameClass::Frame>()->clear();
    buf->mark_free(); // Marca como livre e reseta o tamanho
  }

  // --- Funções da API Principal  ---

  // Envia um frame Engine::FrameClass contido em um buffer JÁ ALOCADO E PREENCHIDO pelo
  // chamador. O chamador é responsável pela alocação, ao fim do send o buffer
  // é liberado.
  // Args:
  //   buf: Ponteiro para o buffer contendo o frame a ser enviado.
  // Returns:
  //   Número de bytes enviados pela Engine ou -1 em caso de erro.
  int send(Buffer *buf) {
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

  // Método membro que processa o sinal (chamado pelo handler estático)
  void handle_signal() {
    int bytes_received = 0;
    do {
      // 1. Alocar um buffer para recepção.
      Buffer *buf = alloc(Engine::FrameClass::MAX_FRAME_SIZE_NO_FCS, 0);

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
        bool notified = this->notify(buf->template data<typename NICFrameClass::Frame>()->prot, buf);
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
  }
  std::mutex alloc_mtx{};
  // --- Membros ---
  Statistics _statistics; // Estatísticas de rede

  // Pool de Buffers
  Buffer _send_buffer_pool[SEND_BUFFERS];
  Buffer _recv_buffer_pool[RECEIVE_BUFFERS];
  unsigned int last_used_send_buffer;
  unsigned int last_used_recv_buffer;
};

#endif // NIC_HH
