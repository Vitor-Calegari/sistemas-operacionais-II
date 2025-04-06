#ifndef NIC_HH
#define NIC_HH

#include <arpa/inet.h> // Para htons, ntohs
#include <functional>
#include <iostream> // Para debug output (opcional)
#include <mutex>

#include "buffer.hh"
#include "engine.hh"
#include "ethernet.hh"
#include "conditionally_data_observed.hh"
#include "conditional_data_observer.hh"

#ifdef DEBUG
#include "utils.hh"
#endif
// A classe NIC (Network Interface Controller).
// Ela age como a interface de rede, usando a Engine fornecida para E/S,
// e notifica observadores (Protocolos) sobre frames recebidos.
//
// D (Observed_Data): Buffer<Ethernet::Frame>* - Notifica com ponteiros para
// buffers recebidos.
// C (Observing_Condition): Ethernet::Protocol - Filtra observadores pelo EtherType.
template<typename Engine>
class NIC
    : public Ethernet,
      public Conditionally_Data_Observed<Buffer<Ethernet::Frame>, Ethernet::Protocol>,
      private Engine
{
public:
  // TODO Valores temporareos
  static const unsigned int SEND_BUFFERS = 1;//Traits<NIC<Engine>>::SEND_BUFFERS;
  static const unsigned int RECEIVE_BUFFERS = 1;//Traits<NIC<Engine>>::RECEIVE_BUFFERS;

  static const unsigned int BUFFER_SIZE =
      SEND_BUFFERS * sizeof(Buffer<Ethernet::Frame>) +
      RECEIVE_BUFFERS * sizeof(Buffer<Ethernet::Frame>);
  typedef Ethernet::Address Address; // Re-expõe Address de Ethernet
  typedef Ethernet::Protocol Protocol_Number;
  typedef Conditional_Data_Observer<Buffer<Ethernet::Frame>, Protocol_Number>
      Observer;
  typedef Conditionally_Data_Observed<Buffer<Ethernet::Frame>, Protocol_Number>
      Observed;
  typedef Buffer<Ethernet::Frame> BufferNIC;

  // Construtor: Recebe um ponteiro para a Engine (cuja vida útil é gerenciada
  // externamente) e o nome da interface de rede.
  // Args:
  //   engine: Ponteiro para a instância da Engine a ser usada.
  //   interface_name: Nome da interface de rede (ex: "eth0", "lo").
  NIC(const char *interface_name) : Engine(interface_name)  //TODO Passar o parametro interface_name aqui é uma boa escolha?
  {
    // Setup Handler -----------------------------------------------------

    std::function<void(int)> callback =
        std::bind(&NIC::handle_signal, this, std::placeholders::_1);

    Engine::setupSignalHandler(callback);

    // Inicializa pool de buffers ----------------------------------------

    try {
      _buffer_pool.reserve(
          BUFFER_SIZE); // Reserva espaço para evitar realocações
      for (unsigned int i = 0; i < BUFFER_SIZE; ++i) {
        // Cria buffers com a capacidade máxima definida
        _buffer_pool.emplace_back(Ethernet::MAX_FRAME_SIZE_NO_FCS);
      }
    } catch (const std::bad_alloc &e) {
      perror("NIC Error: buffer pool alloc");
      exit(EXIT_FAILURE);
    }
  }

  // Destrutor
  ~NIC() {}

  // Proibe cópia e atribuição para evitar problemas com ponteiros e estado.
  NIC(const NIC &) = delete;
  NIC &operator=(const NIC &) = delete;

  // Aloca um buffer do pool interno para envio ou recepção.
  // Retorna: Ponteiro para um Buffer livre, ou nullptr se o pool estiver
  // esgotado. NOTA: O chamador NÃO deve deletar o buffer, deve usar free()!
  BufferNIC *alloc(Address dst, Protocol_Number prot, unsigned int size) {
    std::lock_guard<std::mutex> lock(_pool_mutex); // Protege o acesso ao pool
    int maxSize = Ethernet::HEADER_SIZE + size;
    for (BufferNIC &buf : _buffer_pool) {
      if (!buf.is_in_use()) {
        buf.mark_in_use(); // Marca como usado ANTES de retornar
        buf.data()->src = Engine::getAddress();
        buf.data()->dst = dst;
        buf.data()->prot = prot;
        // Mínimo de 64 bytes e máximo de Ethernet::MAX_FRAME_SIZE_NO_FCS
        buf.setMaxSize(maxSize < Ethernet::MIN_FRAME_SIZE
                           ? Ethernet::MIN_FRAME_SIZE
                           : (maxSize > Ethernet::MAX_FRAME_SIZE_NO_FCS
                                  ? Ethernet::MAX_FRAME_SIZE_NO_FCS
                                  : maxSize));
        buf.setSize(Ethernet::HEADER_SIZE);
        return &buf; // Retorna ponteiro para o buffer encontrado
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
    std::lock_guard<std::mutex> lock(_pool_mutex);
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
      // Atualiza estatísticas
      {
        std::lock_guard<std::mutex> lock(_stats_mutex);
        _statistics.tx_packets++;
        _statistics.tx_bytes +=
            bytes_sent; // Idealmente, bytes_sent == buf->size()
      }
      // std::cout << "NIC::send(buf): Sent " << bytes_sent << " bytes." <<
      // std::endl;
    } else {
      #ifdef DEBUG
      std::cerr << "NIC::send(buf): Engine failed to send packet." << std::endl;
      #endif
    }

    free(buf);

    return bytes_sent;
  }

  // --- Métodos de Gerenciamento e Informação ---

  // Retorna o endereço MAC desta NIC.
  const Address &address() const {
    return Engine::getAddress();
  }

  // Retorna as estatísticas de rede acumuladas.
  const Statistics &statistics() const {
    std::lock_guard<std::mutex> lock(_stats_mutex);
    return _statistics; // Retorna referência direta (cuidado com concorrência)
  }

  int receive(BufferNIC * buf, Address * from, Address * to, void * data, unsigned int size) {
    return buf->size();
  }

private:
  // Método membro que processa o sinal (chamado pelo handler estático)
  void handle_signal(int signum) {
    if (signum == SIGIO) {
      // TODO Print temporário:
      #ifdef DEBUG
      std::cout << "New packet received" << std::endl;
      #endif
      // 1. Alocar um buffer para recepção.
      BufferNIC *buf = nullptr;
      // Usa a capacidade máxima do frame Ethernet
      buf = alloc(Address(), 0, Ethernet::MAX_FRAME_SIZE_NO_FCS);

      // 2. Tentar receber o pacote usando a Engine.
      struct sockaddr_ll sender_addr; // A engine original usa sockaddr genérico
      socklen_t sender_addr_len;
      int bytes_received =
          Engine::receive(buf, sender_addr, sender_addr_len); // Chamada à API original
      #ifdef DEBUG
      if (bytes_received >= 0) {
        printEth(buf);
      }
      #endif

      if (bytes_received > 0) {
        // Pacote recebido!

        // Atualiza estatísticas (protegido por mutex)
        {
          std::lock_guard<std::mutex> lock(_stats_mutex);
          _statistics.rx_packets++;
          _statistics.rx_bytes += bytes_received;
        }

        // Filtrar pacotes enviados por nós mesmos
        if (buf->data()->src != Engine::getAddress()) {
          // ************************************************************
          // TODO Foi comentado pois os observadores ainda não funcionam
          // Notifica os observadores registrados para este protocolo.
          // Passa o ponteiro do buffer alocado.
          // bool notified = notify(proto_net_order, buf);
          // ************************************************************
          bool notified = false;
          // Se NENHUM observador (Protocolo) estava interessado (registrado
          // para este EtherType), a NIC deve liberar o buffer que alocou.
          if (!notified) {
            free(buf);
          } else {
            // O buffer foi passado para o Observer (Protocol) através da chamada a
            // Concurrent_Observer::update dentro do this->notify(). O
            // Protocol/Communicator agora é responsável por eventualmente
            // liberar o buffer recebido via Concurrent_Observer::updated().
          }
        } else {
          free(buf);
        }

      } else if (bytes_received == 0) {
        // Não há mais pacotes disponíveis no momento (recvfrom retornaria 0 ou
        // -1 com EAGAIN/EWOULDBLOCK). A engine original retorna -1 em erro, não
        // 0. Então só chegamos aqui se for < 0.
        free(buf);
      } else {     // bytes_received < 0
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                perror("NIC::handle_signal recvfrom error");
            }
        free(buf);
      }
    }
  }

  

  // --- Membros ---
  Statistics _statistics; // Estatísticas de rede
  mutable std::mutex
      _stats_mutex; // Mutex para proteger o acesso às estatísticas

  // Pool de Buffers
  std::vector<BufferNIC>
      _buffer_pool;       // Vetor que contém os buffers pré-alocados
  std::mutex _pool_mutex; // Mutex para proteger o acesso ao pool (_buffer_pool
                          // e flags _in_use)
};

#endif // NIC_HH
