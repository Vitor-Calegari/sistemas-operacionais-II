#ifndef NIC_HH
#define NIC_HH

#include <vector>
#include <mutex>
#include <memory>
#include <stdexcept>
#include <iostream>

#include "ethernet.hh"
#include "buffer.hh" // Precisa vir antes de NIC se NIC usa Buffer<> diretamente
#include "engine.hh"
#include "conditionally_data_observed.hh"
#include "conditional_data_observer.hh"

#include <sys/ioctl.h> // Para ioctl
#include <net/if.h>    // Para struct ifreq
#include <unistd.h>
#include <cstring>
#include <arpa/inet.h> // Para htons, ntohs

#ifdef DEBUG
#include "utils.hh"
#endif

// REMOVIDO: typedef Buffer<> NicBuffer; - Não é necessário globalmente

template<typename EngineType = Engine>
class NIC
    : public Ethernet, // Para acesso aos tipos Address, Protocol, etc.
      public Conditionally_Data_Observed<Buffer<Ethernet::Frame>, typename Ethernet::Protocol>,
      private EngineType // Herda da Engine (conforme último exemplo)
{
public:
  // Constantes para definir o tamanho do pool (como no exemplo anterior)
  // TODO: Valores temporários, idealmente viriam de Traits<NIC>
  static const unsigned int SEND_BUFFERS = 1;
  static const unsigned int RECEIVE_BUFFERS = 1;
  // BUFFER_SIZE agora é o NÚMERO total de buffers no pool
  static const unsigned int BUFFER_SIZE = SEND_BUFFERS + RECEIVE_BUFFERS;

  // Typedefs públicos (mantidos)
  typedef Ethernet::Address Address;
  typedef Ethernet::Protocol Protocol_Number;
  typedef Conditional_Data_Observer<Buffer<Ethernet::Frame>, Protocol_Number> Observer;
  typedef Conditionally_Data_Observed<Buffer<Ethernet::Frame>, Protocol_Number> Observed;
  // Alias interno para Buffer<Ethernet::Frame>
  typedef Buffer<Ethernet::Frame> BufferNIC;

  // Construtor: Não recebe mais num_buffers, usa a constante BUFFER_SIZE.
  NIC(const char *interface_name) :
      EngineType(interface_name) // Chama construtor da Engine base
  {
    // Setup do Handler (mantido)
    std::function<void(int)> callback =
        std::bind(&NIC::handle_signal, this, std::placeholders::_1);
    EngineType::setupSignalHandler(callback);

    // Inicializa pool de buffers usando a constante BUFFER_SIZE
    _buffer_pool.reserve(BUFFER_SIZE);
    for (unsigned int i = 0; i < BUFFER_SIZE; ++i) {
        Ethernet::Frame* frame_mem = nullptr;
        try {
            frame_mem = EngineType::allocate_frame_memory();
            // Cria o objeto BufferNIC passando o ponteiro da memória alocada
            _buffer_pool.emplace_back(frame_mem, Ethernet::MAX_FRAME_SIZE_NO_FCS);
        } catch (const std::exception& e) {
            std::cerr << "NIC Error: Failed to initialize buffer pool slot " << i << " - " << e.what() << std::endl;
            if (frame_mem) { EngineType::free_frame_memory(frame_mem); }
            cleanup_buffer_pool(); // Limpa o que foi alocado até agora
            perror("NIC Error: buffer pool init failed");
            exit(EXIT_FAILURE);
        }
    }
     #ifdef DEBUG
     std::cout << "NIC buffer pool initialized with " << BUFFER_SIZE << " buffers." << std::endl;
     #endif
  }

  // Destrutor (mantido)
  virtual ~NIC() {
      cleanup_buffer_pool();
      #ifdef DEBUG
      std::cout << "NIC destroyed, buffer pool cleaned up." << std::endl;
      #endif
  }

  // Proíbe cópia e atribuição (mantido)
  NIC(const NIC &) = delete;
  NIC &operator=(const NIC &) = delete;

  // --- API Principal (alloc, free, send - sem mudanças lógicas aqui) ---

  BufferNIC *alloc(Address dst, Protocol_Number prot_net_order, unsigned int payload_size) {
      std::lock_guard<std::mutex> lock(_pool_mutex);
      unsigned int total_required_size = Ethernet::HEADER_SIZE + payload_size;
      if (payload_size > Ethernet::MTU) {
           #ifdef DEBUG
           std::cerr << "NIC::alloc Warning: Requested payload size (" << payload_size
                     << ") exceeds MTU (" << Ethernet::MTU << "). Check usage." << std::endl;
           #endif
           payload_size = Ethernet::MTU;
           total_required_size = Ethernet::HEADER_SIZE + payload_size;
      }

      for (BufferNIC &buf : _buffer_pool) {
          if (!buf.is_in_use()) {
              buf.mark_in_use();
              buf.data()->clear(); // Limpa frame anterior
              buf.data()->dst = dst;
              buf.data()->src = EngineType::getAddress();
              buf.data()->prot = prot_net_order;
              unsigned int final_size = total_required_size;
              if (final_size < Ethernet::MIN_FRAME_SIZE) {
                  final_size = Ethernet::MIN_FRAME_SIZE;
              }
              buf.setSize(final_size);
              #ifdef DEBUG
              // std::cout << "NIC::alloc: Buffer object allocated. Size set to " << final_size << std::endl;
              #endif
              return &buf;
          }
      }
      #ifdef DEBUG
      std::cerr << "NIC::alloc: Buffer object pool exhausted!" << std::endl;
      #endif
      return nullptr;
  }

  void free(BufferNIC *buf) {
      if (!buf) return;
      std::lock_guard<std::mutex> lock(_pool_mutex);
      // Validação se pertence ao pool (opcional)
      // ...
      if(buf->data()) { buf->data()->clear(); }
      buf->mark_free();
      #ifdef DEBUG
      // std::cout << "NIC::free: Buffer object freed." << std::endl;
      #endif
  }

  int send(BufferNIC *buf) {
      if (!buf || !buf->is_in_use()) {
          #ifdef DEBUG
          std::cerr << "NIC::send error: Invalid or not-in-use buffer object provided." << std::endl;
          #endif
          return -1;
      }
      int bytes_sent = EngineType::send(buf); // Delega para Engine
      // Stats e free (libera o OBJETO buffer)
      if (bytes_sent > 0) {
          std::lock_guard<std::mutex> lock(_stats_mutex);
          _statistics.tx_packets++;
          _statistics.tx_bytes += bytes_sent;
      } else {
          #ifdef DEBUG
          std::cerr << "NIC::send(buf): Engine failed to send packet (returned " << bytes_sent << ")." << std::endl;
          #endif
      }
      free(buf); // Libera o objeto Buffer de volta ao pool
      return bytes_sent;
  }

  // --- Métodos de Gerenciamento e Informação (mantidos) ---
  const Address &address() const { return EngineType::getAddress(); }
  const Statistics &statistics() const {
      std::lock_guard<std::mutex> lock(_stats_mutex);
      return _statistics;
  }
  int receive(BufferNIC * buf, Address * src_addr, Address * dst_addr, void * data, unsigned int size) {
      // Implementação da cópia de dados (mantida)
       if (!buf || !buf->data() || !data || !src_addr || !dst_addr) return -1;
       *src_addr = buf->data()->src;
       *dst_addr = buf->data()->dst;
       int payload_available = buf->size() - Ethernet::HEADER_SIZE;
       if (payload_available < 0) payload_available = 0;
       unsigned int bytes_to_copy = std::min(size, (unsigned int)payload_available);
       if (bytes_to_copy > 0) {
           std::memcpy(data, buf->data()->data, bytes_to_copy);
       }
       return bytes_to_copy;
  }

private:
  // handle_signal (mantido - lógica interna precisa de revisão futura para Observer)
  void handle_signal(int signum) {
     if (signum == SIGIO) {
       #ifdef DEBUG
       // std::cout << "NIC::handle_signal triggered." << std::endl;
       #endif
      while (true) {
          BufferNIC *buf = this->alloc(Address(), 0, Ethernet::MTU);
          if (!buf) {
              #ifdef DEBUG
              std::cerr << "NIC::handle_signal: Buffer pool exhausted during reception!" << std::endl;
              #endif
              break;
          }
          struct sockaddr_ll sender_addr;
          socklen_t sender_addr_len = sizeof(sender_addr);
          int bytes_received = EngineType::receive(buf, sender_addr, sender_addr_len);

          #ifdef DEBUG
          // if (bytes_received >= 0) { printEth(buf); }
          #endif

          if (bytes_received > 0) {
              { std::lock_guard<std::mutex> lock(_stats_mutex);
                _statistics.rx_packets++;
                _statistics.rx_bytes += bytes_received; }

              if (buf->data()->src != EngineType::getAddress()) {
                  Protocol_Number proto_net_order = buf->data()->prot;
                  // TODO: Chamar o notify correto da classe base Observed
                   bool notified = this->notify(proto_net_order, buf); // ASSUMINDO que esta assinatura funcione
                  if (!notified) { this->free(buf); }
              } else { this->free(buf); } // Pacote próprio

          } else if (bytes_received == 0) { // EAGAIN
              this->free(buf); break;
          } else { // Erro
              if (errno != EAGAIN && errno != EWOULDBLOCK) { perror("NIC::handle_signal Engine::receive error"); }
              this->free(buf); break;
          }
      } // Fim while
    }
  }

  // cleanup_buffer_pool (mantido)
  void cleanup_buffer_pool() {
       for (BufferNIC& buf : _buffer_pool) {
           if (buf.data()) { EngineType::free_frame_memory(buf.data()); }
       }
       _buffer_pool.clear();
  }

  // --- Membros (mantidos) ---
  Statistics _statistics;
  mutable std::mutex _stats_mutex;
  std::vector<BufferNIC> _buffer_pool;
  std::mutex _pool_mutex;
};

#endif // NIC_HH