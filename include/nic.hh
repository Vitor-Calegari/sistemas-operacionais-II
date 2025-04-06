#ifndef NIC_HH
#define NIC_HH

#include <vector>
#include <mutex>
#include <memory> // Para std::unique_ptr (se Engine fosse gerenciada aqui)
#include <stdexcept>
#include <iostream>

#include "ethernet.hh"
#include "buffer.hh"
#include "engine.hh" // Precisa incluir Engine
#include "conditionally_data_observed.hh"
#include "conditional_data_observer.hh"

#ifdef DEBUG
#include "utils.hh"
#endif

// Usa o tipo de Buffer padrão (Buffer<Ethernet::Frame>)
typedef Buffer<> NicBuffer;

template<typename EngineType = Engine> // Torna NIC template em relação à Engine
class NIC
    : public Ethernet, // Para acesso aos tipos Address, Protocol, etc.
      public Conditionally_Data_Observed<NicBuffer, typename Ethernet::Protocol>,
      private EngineType // Herda da Engine para acesso fácil aos métodos (como no original)
                         // Alternativa: Composição com std::unique_ptr<EngineType> _engine;
{
public:
  // Constantes e Typedefs (mantidos como no original)
  static const unsigned int SEND_BUFFERS = 1;
  static const unsigned int RECEIVE_BUFFERS = 1;
  static const unsigned int BUFFER_SIZE = SEND_BUFFERS + RECEIVE_BUFFERS; // Simplificado
  typedef Ethernet::Address Address;
  typedef Ethernet::Protocol Protocol_Number;
  typedef Conditional_Data_Observer<NicBuffer, Protocol_Number> Observer;
  typedef Conditionally_Data_Observed<NicBuffer, Protocol_Number> Observed;
  typedef NicBuffer BufferNIC; // Alias local

  // Construtor: Passa o nome da interface para o construtor da Engine base.
  // Inicializa o pool de buffers usando a alocação da Engine.
  NIC(const char *interface_name) :
      EngineType(interface_name) // Chama construtor da Engine base
      // A capacidade de cada buffer individual é determinada aqui
     // _buffer_capacity(Ethernet::MAX_FRAME_SIZE_NO_FCS) // (se fosse membro)
  {
    // Setup do Handler (mantido como no original)
    std::function<void(int)> callback =
        std::bind(&NIC::handle_signal, this, std::placeholders::_1);
    // Chama setupSignalHandler da Engine base
    EngineType::setupSignalHandler(callback);

    // Inicializa pool de buffers AGORA usando a Engine para alocar memória
    _buffer_pool.reserve(BUFFER_SIZE); // Reserva espaço no vector
    for (unsigned int i = 0; i < BUFFER_SIZE; ++i) {
        Ethernet::Frame* frame_mem = nullptr;
        try {
            // 1. Pede para a Engine alocar a memória bruta
            frame_mem = EngineType::allocate_frame_memory();

            // 2. Cria o objeto Buffer passando o ponteiro da memória alocada
            //    e a capacidade máxima dessa memória.
            _buffer_pool.emplace_back(frame_mem, Ethernet::MAX_FRAME_SIZE_NO_FCS);

        } catch (const std::exception& e) {
            // Se a alocação da Engine falhar ou emplace_back falhar
            std::cerr << "NIC Error: Failed to initialize buffer pool slot " << i << " - " << e.what() << std::endl;
            // Limpeza parcial: libera memória já alocada pela Engine nesta iteração
            if (frame_mem) {
                EngineType::free_frame_memory(frame_mem);
            }
            // Limpeza completa: libera todos os buffers criados até agora antes de sair
            cleanup_buffer_pool();
            perror("NIC Error: buffer pool init failed"); // Usa perror para consistência
            exit(EXIT_FAILURE); // Ou lança exceção
        }
    }
     #ifdef DEBUG
     std::cout << "NIC buffer pool initialized with " << BUFFER_SIZE << " buffers." << std::endl;
     #endif
  }

    // Destrutor: Libera a memória alocada pela Engine para cada buffer no pool.
    virtual ~NIC() { // Virtual se houver herança de NIC
        cleanup_buffer_pool();
        #ifdef DEBUG
        std::cout << "NIC destroyed, buffer pool cleaned up." << std::endl;
        #endif
    }


  // Proíbe cópia e atribuição
  NIC(const NIC &) = delete;
  NIC &operator=(const NIC &) = delete;

    // --- API Principal (Adaptada) ---

    // Aloca um *objeto Buffer* do pool da NIC. A memória *já está* alocada pela Engine.
    // Preenche cabeçalhos Ethernet conforme assinatura original.
    BufferNIC *alloc(Address dst, Protocol_Number prot_net_order, unsigned int payload_size) {
        std::lock_guard<std::mutex> lock(_pool_mutex); // Protege o pool

        // Calcula o tamanho total necessário (para validação, não para alocação aqui)
        unsigned int total_required_size = Ethernet::HEADER_SIZE + payload_size;
        // Valida se o payload cabe no MTU
        if (payload_size > Ethernet::MTU) {
             #ifdef DEBUG
             std::cerr << "NIC::alloc Warning: Requested payload size (" << payload_size
                       << ") exceeds MTU (" << Ethernet::MTU << "). Allocation might proceed but check usage." << std::endl;
             #endif
             // Continuamos, mas o chamador precisa estar ciente. O setSize limitará.
             payload_size = Ethernet::MTU; // Garante que não exceda MTU para cálculo de setSize
             total_required_size = Ethernet::HEADER_SIZE + payload_size;
        }

        for (BufferNIC &buf : _buffer_pool) {
            if (!buf.is_in_use()) {
                buf.mark_in_use(); // Marca o OBJETO Buffer como usado

                // Limpa o conteúdo anterior (importante!)
                buf.data()->clear(); // Chama clear() do Ethernet::Frame

                // Preenche cabeçalhos Ethernet no frame apontado por buf.data()
                buf.data()->dst = dst;
                buf.data()->src = EngineType::getAddress(); // Obtém MAC da Engine base
                buf.data()->prot = prot_net_order; // Protocolo já em ordem de rede

                // Define o tamanho TOTAL inicial (Header + Payload Planejado)
                // Garante que não excede a capacidade e respeita tamanho mínimo Ethernet.
                unsigned int final_size = total_required_size;
                if (final_size < Ethernet::MIN_FRAME_SIZE) {
                    final_size = Ethernet::MIN_FRAME_SIZE; // Aplica padding implícito pelo tamanho
                }
                // Define o tamanho MÁXIMO que este buffer pode conter (já definido em Buffer)
                // buf.setMaxSize(buf.capacity()); // Desnecessário se capacity() for usado
                // Define o tamanho ATUAL de dados válidos
                buf.setSize(final_size); // Importante: define o tamanho do frame a ser enviado

                #ifdef DEBUG
                // std::cout << "NIC::alloc: Buffer object allocated. Size set to " << final_size << std::endl;
                #endif
                return &buf; // Retorna ponteiro para o OBJETO Buffer
            }
        }
        // Se nenhum objeto Buffer livre foi encontrado
        #ifdef DEBUG
        std::cerr << "NIC::alloc: Buffer object pool exhausted!" << std::endl;
        #endif
        return nullptr;
    }


    // Libera um *objeto Buffer* de volta ao pool da NIC.
    // NÃO libera a memória bruta (isso é feito no destrutor da NIC).
    void free(BufferNIC *buf) {
        if (!buf) return; // Ignora ponteiro nulo

        std::lock_guard<std::mutex> lock(_pool_mutex); // Protege o pool

        // Verifica se o buffer realmente pertence ao pool (opcional, mas seguro)
        bool found = false;
        for (auto& pool_buf : _buffer_pool) {
            if (&pool_buf == buf) {
                found = true;
                break;
            }
        }
        if (!found) {
            #ifdef DEBUG
            std::cerr << "NIC::free Warning: Attempt to free buffer not in pool!" << std::endl;
            #endif
            return; // Não libera buffer desconhecido
        }

        // Limpa o conteúdo do frame Ethernet para evitar vazamento de dados
        if(buf->data()) { // Verifica se ponteiro interno não é nulo
            buf->data()->clear();
        }

        buf->mark_free(); // Marca o OBJETO Buffer como livre e reseta seu tamanho interno
        #ifdef DEBUG
        // std::cout << "NIC::free: Buffer object freed." << std::endl;
        #endif
    }

  // Envia um buffer previamente alocado via alloc().
  // Delega para Engine::send e libera o *objeto* Buffer com free().
  int send(BufferNIC *buf) {
        if (!buf || !buf->is_in_use()) { // Checa se o objeto Buffer é válido e está em uso
             #ifdef DEBUG
             std::cerr << "NIC::send error: Invalid or not-in-use buffer object provided." << std::endl;
             #endif
            return -1;
        }

        // Chama o send da Engine base, passando o objeto Buffer.
        // A Engine acessará buf->data() e buf->size().
        int bytes_sent = EngineType::send(buf);

        // Atualiza estatísticas (mantido)
        if (bytes_sent > 0) {
          std::lock_guard<std::mutex> lock(_stats_mutex);
          _statistics.tx_packets++;
          _statistics.tx_bytes += bytes_sent;
        } else {
          #ifdef DEBUG
          std::cerr << "NIC::send(buf): Engine failed to send packet (returned " << bytes_sent << ")." << std::endl;
          #endif
        }

        // Libera o OBJETO Buffer de volta ao pool da NIC
        free(buf);

        return bytes_sent;
  }


  // --- Métodos de Gerenciamento e Informação (mantidos) ---
  const Address &address() const { return EngineType::getAddress(); }
  const Statistics &statistics() const {
        std::lock_guard<std::mutex> lock(_stats_mutex);
        return _statistics;
  }

  // --- Método Receive (Placeholder/API PDF) ---
  // O receive real acontece em handle_signal. Esta função parece ser
  // para a camada superior (Protocol) usar *depois* de ser notificada.
  // Ela extrai dados de um buffer JÁ recebido e preenchido.
  // TODO: Revisar lógica e parâmetros conforme uso pelo Protocol.
  int receive(BufferNIC * buf, Address * src_addr, Address * dst_addr, void * data, unsigned int size) {
       if (!buf || !buf->data() || !data || !src_addr || !dst_addr) return -1; // Validação

       // Extrai endereços do cabeçalho do buffer
       *src_addr = buf->data()->src;
       *dst_addr = buf->data()->dst;

       // Calcula tamanho do payload disponível no buffer
       int payload_available = buf->size() - Ethernet::HEADER_SIZE;
       if (payload_available < 0) payload_available = 0; // Caso tamanho < header

       // Calcula quantos bytes copiar (mínimo entre o pedido e o disponível)
       unsigned int bytes_to_copy = std::min(size, (unsigned int)payload_available);

       // Copia o payload do buffer para a área de dados do usuário
       if (bytes_to_copy > 0) {
           std::memcpy(data, buf->data()->data, bytes_to_copy);
       }

       // Retorna o número de bytes copiados para o usuário
       return bytes_to_copy;
  }


private:
  // Método de tratamento do sinal SIGIO (lógica principal mantida)
  void handle_signal(int signum) {
    if (signum == SIGIO) {
       #ifdef DEBUG
       // std::cout << "NIC::handle_signal triggered." << std::endl;
       #endif
      // Loop para drenar socket (necessário com SIGIO level-triggered)
      while (true) {
          // 1. Aloca um OBJETO Buffer do pool da NIC
          BufferNIC *buf = this->alloc(Address(), 0, Ethernet::MTU); // Aloca com capacidade máxima
          if (!buf) {
              #ifdef DEBUG
              std::cerr << "NIC::handle_signal: Buffer pool exhausted during reception!" << std::endl;
              #endif
              break; // Sai se pool esgotado
          }

          // 2. Chama Engine::receive para preencher a MEMÓRIA apontada por buf->data()
          struct sockaddr_ll sender_addr;
          socklen_t sender_addr_len = sizeof(sender_addr);
          // Chama receive da Engine base
          int bytes_received = EngineType::receive(buf, sender_addr, sender_addr_len);

          #ifdef DEBUG
          // if (bytes_received >= 0) { printEth(buf); } // Debug print
          #endif

          if (bytes_received > 0) { // Pacote recebido
              // Atualiza stats (mantido)
              { std::lock_guard<std::mutex> lock(_stats_mutex);
                _statistics.rx_packets++;
                _statistics.rx_bytes += bytes_received; }

              // Filtra pacotes próprios (mantido)
              if (buf->data()->src != EngineType::getAddress()) {
                  Protocol_Number proto_net_order = buf->data()->prot;
                  // Notifica observers (usando notify da classe base Observed)
                  // NOTA: O notify correto a ser chamado é o da classe base
                  // Conditionally_Data_Observed, que pode ter assinatura diferente.
                  // Assumindo que exista `this->notify(proto_net_order, buf)` por enquanto.
                   bool notified = this->notify(proto_net_order, buf); // TODO: Verificar assinatura correta

                  if (!notified) {
                      this->free(buf); // Libera OBJETO Buffer se não notificado
                  } // else: Observer é responsável por chamar this->free(buf)
              } else {
                  this->free(buf); // Libera OBJETO Buffer (pacote próprio)
              }
          } else if (bytes_received == 0) { // EAGAIN/EWOULDBLOCK
              this->free(buf); // Libera OBJETO Buffer não usado
              break; // Fila vazia
          } else { // Erro real
              if (errno != EAGAIN && errno != EWOULDBLOCK) {
                   perror("NIC::handle_signal Engine::receive error");
              }
              this->free(buf); // Libera OBJETO Buffer não usado
              break; // Sai em caso de erro
          }
      } // Fim do while
    }
  }

  // Função auxiliar para limpar o pool no destrutor
  void cleanup_buffer_pool() {
       for (BufferNIC& buf : _buffer_pool) {
           // Pede para a Engine liberar a memória bruta associada a este buffer
           if (buf.data()) { // Verifica se o ponteiro é válido
               EngineType::free_frame_memory(buf.data());
               // Não precisa setar buf.data() para null aqui, pois o objeto Buffer será destruído
           }
       }
       _buffer_pool.clear(); // Limpa o vetor de objetos Buffer
  }


  // --- Membros ---
  Statistics _statistics;
  mutable std::mutex _stats_mutex;

  // Pool de Objetos Buffer (NIC gerencia os objetos, Engine gerencia a memória)
  std::vector<BufferNIC> _buffer_pool;
  std::mutex _pool_mutex; // Protege _buffer_pool e flags _in_use dos Buffers

  // const unsigned int _buffer_capacity; // Movido para construtor do Buffer (se necessário)
};

#endif // NIC_HH