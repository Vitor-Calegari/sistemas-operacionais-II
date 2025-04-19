#ifndef PROTOCOL_HH
#define PROTOCOL_HH

#include "concurrent_observed.hh"
#include "conditional_data_observer.hh"
#include "conditionally_data_observed.hh"
#include <cstring>

#ifdef DEBUG
#include <iostream>
#endif

template <typename NIC>
class Protocol
    : public Concurrent_Observed<typename NIC::BufferNIC, unsigned short>,
      private NIC::Observer {
public:
  inline static const typename NIC::Protocol_Number PROTO =
      htons(0x88B5);

  typedef typename NIC::Header NICHeader;
  typedef typename NIC::BufferNIC Buffer;
  typedef typename NIC::Address Physical_Address;
  typedef unsigned short SysID;
  typedef unsigned short Port;

  typedef Conditional_Data_Observer<Buffer, Port> Observer;
  typedef Conditionally_Data_Observed<Buffer, Port> Observed;

  // Endereço do protocolo: combinação de endereço físico e porta
  class Address {
  public:
    enum Null { NONE };
    Address() : _paddr(NIC::ZERO), _sysID(0), _port(0) {
    }
    Address([[maybe_unused]]
            const Null &n)
        : _paddr(NIC::ZERO), _port(0) {
    }
    Address(Physical_Address paddr, SysID sysID, Port port) : _paddr(paddr), _sysID(sysID), _port(port) {
    }
    operator bool() const {
      return (_paddr != NIC::ZERO || _port != 0);
    }
    bool operator==(const Address &other) const {
      return (_paddr == other._paddr) && (_port == other._port);
    }
    Physical_Address getPAddr() const {
      return _paddr;
    }
    Port getSysID() const {
      return _sysID;
    }
    Port getPort() const {
      return _port;
    }

  private:
    Physical_Address _paddr;
    SysID _sysID;
    Port _port;
  } __attribute__((packed));

  static const Address BROADCAST;

  // Cabeçalho do pacote do protocolo (pode ser estendido com timestamp, tipo,
  // etc.)
  class Header {
  public:
    Header() : origin(Address()), dest(Address()), payloadSize(0) {}
    Address origin;
    Address dest;
    unsigned short payloadSize;
  } __attribute__((packed));

  // MTU disponível para o payload: espaço total do buffer menos o tamanho do
  // Header
  inline static const unsigned int MTU = NIC::MTU - sizeof(Header);
  typedef unsigned char Data[MTU];

  // Pacote do protocolo: composto pelo Header e o Payload
  class Packet : public Header {
  public:
    Packet() {
      std::memset(_data, 0, sizeof(_data));
    }
    Header *header() {
      return this;
    }
    template <typename T>
    T *data() {
      return reinterpret_cast<T *>(&_data);
    }

  private:
    Data _data;
  } __attribute__((packed));

  static Protocol &getInstance(NIC *nic, SysID sysID) {
    static Protocol instance(nic, sysID);

    return instance;
  }

  Protocol(Protocol const &) = delete;
  void operator=(Protocol const &) = delete;

protected:
  // Construtor: associa o protocolo à NIC e registra-se como observador do
  // protocolo PROTO
  Protocol(NIC *nic, SysID sysID) : _nic(nic), _sysID(sysID) {
    _nic->attach(this, PROTO);
  }

public:
  // Destrutor: remove o protocolo da NIC
  ~Protocol() {
    _nic->detach(this, PROTO);
  }

  Physical_Address getNICPAddr() { return _nic->address(); }
  SysID getSysID() { return _sysID; }

  // Envia uma mensagem:
  // Aloca um buffer (que é um Ethernet::Frame), interpreta o payload (após o
  // cabeçalho Ethernet) como um Packet, monta o pacote e delega o envio à NIC.
  int send(Address &from, Address &to, void *data,
           unsigned int size) {
    Buffer *buf = _nic->alloc(sizeof(Header) + size, 1);
    if (buf == nullptr)
      return -1;
    // Estrutura do frame ethernet todo:
    // [MAC_D, MAC_S, Proto, Payload = [Addr_S, Addr_D, Data_size, Data_P]]
    buf->data()->src = from.getPAddr();
    buf->data()->dst = BROADCAST.getPAddr();  // Sempre broadcast
    buf->data()->prot = PROTO;
    // Payload do Ethernet::Frame é o Protocol::Packet
    Packet *pkt = buf->data()->template data<Packet>();
    pkt->header()->origin = from;
    pkt->header()->dest = to;
    pkt->header()->payloadSize = size;
    buf->setSize(sizeof(NICHeader) + sizeof(Header) + size);
    std::memcpy(pkt->template data<char>(), data, size);
#ifdef DEBUG
    std::cout
        << "*************************Protocol Packet*************************"
        << std::endl;
    std::cout << std::dec << std::endl;
    std::cout << "Packet content: " << std::endl;
    std::cout << "Origin Address: ";
    for (int i = 0; i < 6; ++i) {
      std::cout << std::hex << std::uppercase
          << (int)pkt->header()->origin.getPAddr().mac[i];
      if (i < 5)
      std::cout << ":";
    }
    std::cout << ", SysID: " << std::dec << pkt->header()->origin.getSysID()
          << ", Port: " << pkt->header()->origin.getPort() << std::endl;

    std::cout << "Destination Address: ";
    for (int i = 0; i < 6; ++i) {
      std::cout << std::hex << std::uppercase
          << (int)pkt->header()->dest.getPAddr().mac[i];
      if (i < 5)
      std::cout << ":";
    }
    std::cout << ", SysID: " << std::dec << pkt->header()->dest.getSysID()
          << ", Port: " << pkt->header()->dest.getPort() << std::endl;
    std::cout << "Payload Size: " << pkt->header()->payloadSize << std::endl;
    char hex_chars[] = "0123456789ABCDEF";
    char buffer[3]; // Two hex digits and a null terminator
    buffer[2] = '\0';

    std::cout << "Payload Data: ";
    for (unsigned int i = 0; i < size; ++i) {
      unsigned char byte = pkt->template data<char>()[i];
      buffer[0] = hex_chars[(byte >> 4) & 0xF];
      buffer[1] = hex_chars[byte & 0xF];
      std::cout << buffer << " ";
    }
    std::cout << std::endl;

    std::cout << "Full Packet Data: ";
    unsigned char *packetBytes = reinterpret_cast<unsigned char *>(pkt);
    for (unsigned int i = 0; i < sizeof(Header) + size; ++i) {
      unsigned char byte = packetBytes[i];
      buffer[0] = hex_chars[(byte >> 4) & 0xF];
      buffer[1] = hex_chars[byte & 0xF];
      std::cout << buffer << " ";
    }
    std::cout << std::endl;
#endif
    if (to.getSysID() == _sysID) {
      return -1; // TODO Implementar nova engine
    } else {
      return _nic->send(buf);
    }

  }

  // Recebe uma mensagem:
  // Aqui, também interpretamos o payload do Ethernet::Frame como um Packet.
  int receive(Buffer *buf, Address *from, Address *to, void *data, unsigned int size) {
    Packet pkt = Packet();
    _nic->receive(buf, &pkt, MTU + sizeof(Header));
    *from = pkt.header()->origin;
    *to = pkt.header()->dest;
    unsigned int message_data_size = pkt.header()->payloadSize;
    unsigned int actual_received_bytes = size > message_data_size ? message_data_size : size;
    std::memcpy(data, pkt.template data<char>(), actual_received_bytes);
    _nic->free(buf);
    return actual_received_bytes;
  }

private:
  // Método update: chamado pela NIC quando um frame é recebido.
  // Agora com 3 parâmetros: o Observed, o protocolo e o buffer.
  void update([[maybe_unused]] typename NIC::Observed *obs,
              [[maybe_unused]] typename NIC::Protocol_Number prot,
              Buffer *buf) {
    Packet *pkt = buf->data()->template data<Packet>();
    Port port = pkt->header()->dest.getPort();
    if (pkt->header()->dest.getSysID() != _sysID) {
      _nic->free(buf);
    } else if (!this->notify(port, buf)) {
      _nic->free(buf);
#ifdef DEBUG
      std::cerr << "Protocol::update: Communicator não notificado" << std::endl;
    }
    else {
      std::cerr << "Protocol::update: Communicator notificado" << std::endl;
#endif
    }
  }

  NIC *_nic;
  SysID _sysID;
};

template <typename NIC>
const typename Protocol<NIC>::Address Protocol<NIC>::BROADCAST =
    typename Protocol<NIC>::Address(NIC::BROADCAST_ADDRESS, 0, 10);
#endif // PROTOCOL_HH
