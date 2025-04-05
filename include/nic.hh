#ifndef NIC_HH
#define NIC_HH

#include <arpa/inet.h> // Para htons, ntohs
#include <functional>
#include <iostream> // Para debug output (opcional)
#include <mutex>
#include <net/if.h> // Para struct ifreq
#include <string>
#include <sys/ioctl.h> // Para ioctl, SIOCGIFHWADDR, SIOCGIFINDEX

#include "buffer.hh"
#include "concurrent_observed.hh"
#include "concurrent_observer.hh"
#include "engine.hh"
#include "ethernet.hh"

// TODO Remover depois:
#include "utils.hh"

// A classe NIC (Network Interface Controller).
// Ela age como a interface de rede, usando a Engine fornecida para E/S,
// e notifica observadores (Protocolos) sobre frames recebidos.
//
// D (Observed_Data): Buffer<Ethernet::Frame>* - Notifica com ponteiros para
// buffers recebidos. C (Observing_Condition): Ethernet::Protocol - Filtra
// observadores pelo EtherType.
template<typename Engine>
class NIC
    : public Ethernet,
      // ******************************************************************************
      // TODO: O OBSERVADOR AQUI ESTÁ ERRADO, DEVERIA SER O CONDITIONAL. Porém
      // ainda não está implementado, concertar depois no resto da NIC.
      public Concurrent_Observed<Buffer<Ethernet::Frame>, Ethernet::Protocol>,
      // ******************************************************************************
      private Engine {
public:
  // TODO Valores temporareos
  static const unsigned int SEND_BUFFERS = 100; // Traits<NIC>::SEND_BUFFERS;
  static const unsigned int RECEIVE_BUFFERS =
      100; // Traits<NIC>::RECEIVE_BUFFERS;

  static const unsigned int BUFFER_SIZE =
      SEND_BUFFERS * sizeof(Buffer<Ethernet::Frame>) +
      RECEIVE_BUFFERS * sizeof(Buffer<Ethernet::Frame>);
  typedef Ethernet::Address Address; // Re-expõe Address de Ethernet
  typedef Ethernet::Protocol Protocol_Number;
  typedef Concurrent_Observer<Buffer<Ethernet::Frame>, Protocol_Number>
      Observer;
  typedef Concurrent_Observed<Buffer<Ethernet::Frame>, Protocol_Number>
      Observed;
  typedef Buffer<Ethernet::Frame> BufferNIC;

  // Construtor: Recebe um ponteiro para a Engine (cuja vida útil é gerenciada
  // externamente) e o nome da interface de rede. Args:
  //   engine: Ponteiro para a instância da Engine a ser usada.
  //   interface_name: Nome da interface de rede (ex: "eth0", "lo").
  NIC(const std::string &interface_name)
      : _interface_index(-1),
        _interface_name(interface_name)
  {
    // Tenta obter as informações da interface (MAC, índice)
    if (!get_interface_info(_interface_name)) {
      // Tratamento de erro mais robusto pode ser necessário
      perror("NIC Error: interface info");
      exit(EXIT_FAILURE);
    }

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

    // Print Debug -------------------------------------------------------

    std::cout << "NIC initialized for interface " << _interface_name
              << " with MAC ";
    // Imprime o MAC Address (formatação manual)
    for (int i = 0; i < 6; ++i)
      std::cout << std::hex << (int)_address.mac[i] << (i < 5 ? ":" : "");
    std::cout << std::dec << " and index " << _interface_index << std::endl;
  }

  // Destrutor
  ~NIC() {
    std::cout << "NIC for interface " << _interface_name << " destroyed."
              << std::endl;
  }

  // Proibe cópia e atribuição para evitar problemas com ponteiros e estado.
  NIC(const NIC &) = delete;
  NIC &operator=(const NIC &) = delete;

  // Aloca um buffer do pool interno para envio ou recepção.
  // Retorna: Ponteiro para um Buffer livre, ou nullptr se o pool estiver
  // esgotado. NOTA: O chamador NÃO deve deletar o buffer, deve usar free()!
  BufferNIC *alloc(Address dst, Protocol_Number prot, unsigned int size) {
    std::lock_guard<std::mutex> lock(_pool_mutex); // Protege o acesso ao pool
    for (BufferNIC &buf : _buffer_pool) {
      if (!buf.is_in_use()) {
        buf.mark_in_use(); // Marca como usado ANTES de retornar
        buf.data()->src = _address;
        buf.data()->dst = dst;
        buf.data()->prot = prot;
        // Mínimo de 64 bytes e máximo de Ethernet::MAX_FRAME_SIZE_NO_FCS
        buf.setMaxSize(size < Ethernet::MIN_FRAME_SIZE
                           ? Ethernet::MIN_FRAME_SIZE
                           : (size > Ethernet::MAX_FRAME_SIZE_NO_FCS
                                  ? Ethernet::MAX_FRAME_SIZE_NO_FCS
                                  : size));
        buf.setSize(Ethernet::HEADER_SIZE);
        return &buf; // Retorna ponteiro para o buffer encontrado
      }
    }
    // Se nenhum buffer livre foi encontrado
    std::cerr << "NIC::alloc: Buffer pool exhausted!" << std::endl;
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
    if (!buf) {
      std::cerr << "NIC::send error: Null buffer provided." << std::endl;
      return -1;
    }

    // Configura o endereço de destino para sendto
    struct sockaddr_ll sadr_ll;
    std::memset(&sadr_ll, 0, sizeof(sadr_ll));
    sadr_ll.sll_family = AF_PACKET;
    sadr_ll.sll_ifindex = _interface_index;
    sadr_ll.sll_halen = ETH_ALEN;
    std::memcpy(sadr_ll.sll_addr, buf->data()->dst.mac, ETH_ALEN);

    // Chama o send da Engine com a cópia
    int bytes_sent = Engine::send(buf, (sockaddr *)&sadr_ll);

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
      std::cerr << "NIC::send(buf): Engine failed to send packet." << std::endl;
    }

    free(buf);

    return bytes_sent;
  }

  // TODO Acho que esse método pode não existir
  // Método de envio de alto nível.
  // Este método aloca temporariamente um buffer, preenche-o e o envia.
  // Args:
  //   dst: Endereço MAC de destino (NOTA: será IGNORADO, sempre envia
  //   broadcast). prot: O número do protocolo (EtherType) a ser colocado no
  //   cabeçalho (em ordem de host). data: Ponteiro para os dados a serem
  //   enviados no payload. size: Tamanho dos dados em bytes.
  // Returns:
  //   Número de bytes enviados no total ou -1 em caso de erro.
  int send(Address dst, Protocol_Number prot, void *data, unsigned int size) {
    // 1. Alocar um buffer temporário com 'new'
    BufferNIC *buf = nullptr;
    try {
      buf = alloc(dst, prot, size);
    } catch (const std::bad_alloc &e) {
      std::cerr << "NIC::send: Failed to allocate buffer - " << e.what()
                << std::endl;
      return -1;
    }

    // 2. Preencher o buffer
    // Define o endereço de destino como broadcast (ignora o parâmetro dst)
    buf->data()->dst = dst;
    // Define o endereço de origem (será sobrescrito pelo send(Buffer*) ou
    // engine, mas bom ter)
    buf->data()->src = _address;
    // Define o protocolo (EtherType). Recebe em ordem de host, converte
    // para
    // rede.
    buf->data()->prot = htons(prot);

    // Calcula o tamanho do payload a ser copiado (limitado pelo MTU)
    unsigned int payload_size = std::min(size, (unsigned int)Ethernet::MTU);
    // Copia os dados do usuário para o payload do frame Ethernet
    if (data && payload_size > 0) {
      std::memcpy(buf->data()->data, data, payload_size);
    } else {
      payload_size = 0; // Garante que payload_size seja 0 se data for null
    }

    // Define o tamanho total do buffer (Cabeçalho Ethernet + Payload)
    unsigned int total_size = Ethernet::HEADER_SIZE + payload_size;
    // Ethernet frames têm tamanho mínimo (64 bytes total, 60 sem FCS).
    // Padding
    // pode ser necessário. Raw sockets geralmente não precisam de padding
    // manual se o total_size >= 60. if (total_size < 60) total_size = 60;
    //
    // Padding manual se necessário (raro com raw sockets)
    buf->setSize(total_size);

    // 3. Chamar o outro método send para fazer o envio real.
    //    Este método send(const Buffer*) fará a cópia e chamará a engine.
    int result = send(buf); // Passa o ponteiro do buffer alocado

    return result;
  }

  // --- Métodos de Gerenciamento e Informação ---

  // Retorna o endereço MAC desta NIC.
  const Address &address() const {
    return _address;
  }

  // Retorna as estatísticas de rede acumuladas.
  const Statistics &statistics() const {
    std::lock_guard<std::mutex> lock(_stats_mutex);
    return _statistics; // Retorna referência direta (cuidado com concorrência)
  }

private:
  // Método membro que processa o sinal (chamado pelo handler estático)
  //  R: Acho que deveriamos testar se essa função funciona sem o While,
  //     acho que a Engine da uma interrupção por pacote novo
  void handle_signal(int signum) {
    if (signum == SIGIO) {
      // TODO Print temporário:
      std::cout << "New packet received" << std::endl;
      // 1. Alocar um buffer para recepção.
      //    De acordo com o código original (main.cc, engine signal handler
      //    example), parece que a alocação é feita com 'new'. O tamanho deve
      //    ser suficiente para o maior frame Ethernet.
      BufferNIC *buf = nullptr;
      try {
        // Usa a capacidade máxima do frame Ethernet
        buf = alloc(Address(), 0, 1514);
      } catch (const std::bad_alloc &e) {
        std::cerr
            << "NIC::handle_signal: Failed to allocate buffer for reception - "
            << e.what() << std::endl;
      }

      // 2. Tentar receber o pacote usando a Engine.
      //    A Engine::receive fornecida espera (Buffer<Data> * buf, sockaddr
      //    saddr) e retorna int. Precisamos de uma sockaddr para receber o
      //    remetente.

      struct sockaddr_ll sender_addr; // A engine original usa sockaddr genérico
      socklen_t sender_addr_len;
      int bytes_received =
          Engine::receive(buf, sender_addr, sender_addr_len); // Chamada à API original
      if (bytes_received >= 0) {
        printEth(buf);
      }

      if (bytes_received > 0) {
        // Pacote recebido!

        // Atualiza estatísticas (protegido por mutex)
        {
          std::lock_guard<std::mutex> lock(_stats_mutex);
          _statistics.rx_packets++;
          _statistics.rx_bytes += bytes_received;
        }

        // Filtrar pacotes enviados por nós mesmos (best-effort sem sender MAC
        // da engine original) A única forma é comparar o MAC de origem DENTRO
        // do buffer com o nosso.
        //  R: Talvez tenhamos que modificar isso quando a utilizemos a NIC para
        //  comunicar entre
        //     Threads e não apenas processos.
        if (buf->data()->src != _address) {
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
            // std::cout << "NIC: Packet received (proto=0x" << std::hex <<
            // ntohs(proto_net_order) << std::dec << "), but no observer found.
            // Deleting buffer." << std::endl;
            free(buf); // Libera o buffer alocado com 'new'
          } else {
            // std::cout << "NIC: Packet received (proto=0x" << std::hex <<
            // ntohs(proto_net_order) << std::dec << ") and notified observer.
            // Buffer passed." << std::endl; O buffer foi passado para o
            // Observer (Protocol) através da chamada a
            // Concurrent_Observer::update dentro do this->notify(). O
            // Protocol/Communicator agora é responsável por eventualmente
            // deletar o buffer recebido via Concurrent_Observer::updated().
          }
        } else {
          // Pacote com nosso MAC de origem, provavelmente loopback do nosso
          // envio. Ignorar.
          // std::cout << "NIC: Ignored own packet." << std::endl;
          free(buf); // Libera o buffer alocado
        }

      } else if (bytes_received == 0) {
        // Não há mais pacotes disponíveis no momento (recvfrom retornaria 0 ou
        // -1 com EAGAIN/EWOULDBLOCK). A engine original retorna -1 em erro, não
        // 0. Então só chegamos aqui se for < 0.
        free(buf); // Libera o buffer que alocamos e não usamos.
      } else {     // bytes_received < 0
                   // Erro na leitura do socket (ou EAGAIN/EWOULDBLOCK se fosse
                   // não-bloqueante). A engine original não parece configurar
               // não-bloqueante explicitamente no signal handler setup. Se for
               // erro real, logar. Se for EAGAIN, apenas sair do loop. if
               // (errno != EAGAIN && errno != EWOULDBLOCK) { // Se pudéssemos
               // checar errno
               // perror("NIC::handle_signal recvfrom error");
               //}
        free(buf); // Libera o buffer que alocamos.
      }
    }
  }

  // Obtém informações da interface (MAC, índice) usando ioctl.
  bool get_interface_info(const std::string &interface_name) {
    struct ifreq ifr;
    std::memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, interface_name.c_str(), IFNAMSIZ - 1);
    ifr.ifr_name[IFNAMSIZ - 1] = '\0'; // Garante terminação nula

    // Obter o índice da interface
    if (ioctl(Engine::getSocketFd(), SIOCGIFINDEX, &ifr) == -1) {
      perror(("NIC Error: ioctl SIOCGIFINDEX failed for " + interface_name)
                 .c_str());
      return false;
    }
    _interface_index = ifr.ifr_ifindex;

    // Obter o endereço MAC (Hardware Address)
    if (ioctl(Engine::getSocketFd(), SIOCGIFHWADDR, &ifr) == -1) {
      perror(("NIC Error: ioctl SIOCGIFHWADDR failed for " + interface_name)
                 .c_str());
      return false;
    }

    // Copia o endereço MAC da estrutura ifreq para o membro _address
    // Usa o construtor de Address que recebe unsigned char[6]
    _address = Address(
        reinterpret_cast<const unsigned char *>(ifr.ifr_hwaddr.sa_data));

    // Caso a interface utilizada seja loopback, geralmente o endereço dela é
    // 00:00:00:00:00:00 Verifica se o MAC obtido não é zero
    if (interface_name != "lo" &&
        !_address) { // Usa o operator bool() da Address
      std::cerr << "NIC Error: Obtained MAC address is zero for "
                << interface_name << std::endl;
      return false;
    }

    return true;
  }

  // --- Membros ---
  Address _address;            // Endereço MAC desta NIC (obtido via ioctl)
  int _interface_index;        // Índice da interface de rede (obtido via ioctl)
  std::string _interface_name; // Guarda o nome da interface

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
