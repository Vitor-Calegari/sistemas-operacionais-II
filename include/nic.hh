#ifndef NIC_HH
#define NIC_HH

#include <mutex>

#include "buffer.hh"
#include "conditional_data_observer.hh"
#include "conditionally_data_observed.hh"
#include "ethernet.hh"
#include "sync_engine.hh"

#ifdef DEBUG
#include "utils.hh"
#include <iostream>
#endif

#ifdef DEBUG_DELAY
#include "clocks.hh"
#include "debug_timestamp.hh"
#endif

template <Buffer::BufferType T, size_t... I>
static constexpr std::array<Buffer, sizeof...(I)>
make_pool(std::index_sequence<I...>) {
  return { (static_cast<void>(I), Buffer{T})... };
}

// A classe NIC (Network Interface Controller).
// Ela age como a interface de rede, usando a Engine fornecida para E/S,
// e notifica observadores (Protocolos) sobre frames recebidos.
//
// D (Observed_Data): Buffer<Engine::FrameClass::Frame>* - Notifica com
// ponteiros para buffers recebidos. C (Observing_Condition):
// Engine::FrameClass::Protocol - Filtra observadores pelo EtherType.
template <typename Engine>
class NIC
    : public Engine::FrameClass,
      public Conditionally_Data_Observed<Buffer,
                                         typename Engine::FrameClass::Protocol>,
      private Engine {
public:

  typedef typename Engine::FrameClass::Statistics Statistics;
  typedef typename Engine::FrameClass NICFrameClass;
  typedef typename Engine::FrameClass::Header Header;
  typedef typename Engine::FrameClass::Address Address;
  typedef typename Engine::FrameClass::Protocol Protocol_Number;
  typedef Conditional_Data_Observer<Buffer, Protocol_Number> Observer;
  typedef Conditionally_Data_Observed<Buffer, Protocol_Number> Observed;

  static constexpr unsigned int SEND_BUFFERS    = 1024;
  static constexpr unsigned int RECEIVE_BUFFERS = 1024;

  // Determina em tempo de compilação qual BufferType usar
  static constexpr Buffer::BufferType pool_type =
    std::is_same_v<typename Engine::FrameClass, Ethernet>
      ? Buffer::EthernetFrame
      : Buffer::SharedMemFrame;

  // Pools pré‑inicializados em compile-time, mas mutáveis em runtime
  inline static std::array<Buffer, SEND_BUFFERS> _send_buffer_pool =
    make_pool<pool_type>(std::make_index_sequence<SEND_BUFFERS>{});
  inline static std::array<Buffer, RECEIVE_BUFFERS> _recv_buffer_pool =
    make_pool<pool_type>(std::make_index_sequence<RECEIVE_BUFFERS>{});

  // Args:
  //   interface_name: Nome da interface de rede (ex: "eth0").
  NIC(const char *interface_name, SimulatedClock *clock)
      : Engine(interface_name), last_used_send_buffer(SEND_BUFFERS - 1),
        last_used_recv_buffer(RECEIVE_BUFFERS - 1), _clock(clock) {
    // Setup Handler -----------------------------------------------------
    Engine::template bind<NIC<Engine>, &NIC<Engine>::handle_signal>(this);
  }

  // Destrutor
  ~NIC() {
  }

  // Proibe cópia e atribuição para evitar problemas com ponteiros e estado.
  NIC(const NIC &) = delete;
  NIC &operator=(const NIC &) = delete;

  // Aloca um buffer do pool interno para envio ou recepção.
  // Retorna: Ponteiro para um Buffer livre, ou nullptr se o pool estiver
  // esgotado. NOTA: O chamador NÃO deve deletar o buffer, deve usar free()!
  Buffer *alloc(int send) {
    std::lock_guard<std::mutex> lock(alloc_mtx);

    unsigned int last_used_buffer =
        send ? last_used_send_buffer : last_used_recv_buffer;
    unsigned int buffer_size_l = send ? SEND_BUFFERS : RECEIVE_BUFFERS;
    auto& buffer_pool = send ? _send_buffer_pool : _recv_buffer_pool;

    for (unsigned int j = 0; j < buffer_size_l; ++j) {
      int i = (last_used_buffer + j) % buffer_size_l;
      if (!buffer_pool[i].is_in_use()) {
        last_used_buffer = i;
        buffer_pool[i].mark_in_use();
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

  // Envia um frame Engine::FrameClass contido em um buffer JÁ ALOCADO E
  // PREENCHIDO pelo chamador. O chamador é responsável pela alocação, ao fim do
  // send o buffer é liberado. Args:
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
      Buffer *buf = nullptr;
      if constexpr (std::is_same_v<NICFrameClass, Ethernet>) {
        // 1. Alocar um buffer para recepção.
        buf = alloc(0);

        if (buf == nullptr) {
#ifdef DEBUG
          std::cout << "NIC buffer is full" << std::endl;
#endif
          break;
        }
#ifdef DEBUG_DELAY
      auto now = std::chrono::high_resolution_clock::now();
      buf->_temp_bottom_delay = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
#endif
      }

      // 2. Tentar receber o pacote usando a Engine.
      bytes_received = Engine::receive(buf);
#ifdef DEBUG
      if (bytes_received > 0) {
        printEth(buf);
      }
#endif
      if (bytes_received > 0) {
        buf->set_receive_time(_clock->getTimestamp());
        // Pacote recebido!
        _statistics.rx_packets++;
        _statistics.rx_bytes += bytes_received;
        bool notified = this->notify(
            buf->template data<typename NICFrameClass::Frame>()->prot, buf);
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
  Buffer::BufferType buf_type{};
  std::mutex alloc_mtx{};
  // --- Membros ---
  Statistics _statistics; // Estatísticas de rede

  // Pool de Buffers
  unsigned int last_used_send_buffer;
  unsigned int last_used_recv_buffer;

  SimulatedClock *_clock;
};

#endif // NIC_HH
