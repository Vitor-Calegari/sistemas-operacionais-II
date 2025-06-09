#ifndef PROTOCOL_COMMOM_HH
#define PROTOCOL_COMMOM_HH

#include "concurrent_observed.hh"
#include "conditional_data_observer.hh"
#include "control.hh"
#include "mac.hh"
#include "navigator.hh"
#include "sync_engine.hh"
#include <cstring>
#include <netinet/in.h>

#ifdef DEBUG
#include "utils.hh"
#endif

template <typename SocketNIC, typename SharedMemNIC, typename Navigator>
class ProtocolCommom
    : public Concurrent_Observed<typename SocketNIC::BufferNIC, unsigned short>,
      private SocketNIC::Observer {
public:
  inline static const typename SocketNIC::Protocol_Number PROTO = htons(0x88B5);

  typedef SyncEngine<ProtocolCommom<SocketNIC, SharedMemNIC, Navigator>>
      SyncEngineP;
  typedef typename SocketNIC::Header NICHeader;
  typedef typename SocketNIC::BufferNIC Buffer;
  typedef typename SocketNIC::Address Physical_Address;
  typedef pid_t SysID;
  typedef unsigned short Port;

  typedef Conditional_Data_Observer<Buffer, Port> Observer;
  typedef Concurrent_Observed<Buffer, Port> Observed;
  using Coordinate = Navigator::Coordinate;

  static const Port BROADCAST = 0xFFFF;
  static const SysID BROADCAST_SID = 0;

  // Endereço do protocolo: combinação de endereço físico e porta
  class Address {
  public:
    enum Null { NONE };
    Address() : _paddr(SocketNIC::ZERO), _sysID(0), _port(0) {
    }
    Address([[maybe_unused]]
            const Null &n)
        : _paddr(SocketNIC::ZERO), _port(0) {
    }
    Address(Physical_Address paddr, SysID sysID, Port port)
        : _paddr(paddr), _sysID(sysID), _port(port) {
    }
    operator bool() const {
      return (_paddr != SocketNIC::ZERO || _port != 0);
    }
    bool operator==(const Address &other) const {
      return (_paddr == other._paddr) && (_port == other._port);
    }
    friend bool operator<(const Address &rhs, const Address &lhs) {
      return (rhs._paddr < lhs._paddr) ||
             (rhs._paddr == lhs._paddr && rhs._port < lhs._port);
    }
    Physical_Address getPAddr() const {
      return _paddr;
    }
    SysID getSysID() const {
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

  // Cabeçalho do pacote do protocolo (pode ser estendido com timestamp, tipo,
  // etc.)
  class Header {
  public:
    Header()
        : origin(Address()), dest(Address()), ctrl(0), coord_x(0), coord_y(0),
          timestamp(0), payloadSize(0), tag{} {
    }
    Address origin;
    Address dest;
    Control ctrl;
    double coord_x;
    double coord_y;
    uint64_t timestamp;
    std::size_t payloadSize;
    MAC::Tag tag;
  } __attribute__((packed));

  // MTU disponível para o payload: espaço total do buffer menos o tamanho do
  // Header
  inline static const unsigned int MTU = SocketNIC::MTU - sizeof(Header);
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

  static ProtocolCommom &getInstance(const char *interface_name, SysID sysID,
                                     bool isRSU,
                                     const std::vector<Coordinate> &points,
                                     Topology topology, double comm_range,
                                     double speed = 1) {
    static ProtocolCommom instance(interface_name, sysID, isRSU, points,
                                   topology, comm_range, speed);
    return instance;
  }

  ProtocolCommom(ProtocolCommom const &) = delete;
  void operator=(ProtocolCommom const &) = delete;

protected:
  // Construtor: associa o protocolo à NIC e registra-se como observador do
  // protocolo PROTO
  ProtocolCommom(const char *interface_name, SysID sysID, bool isRSU,
                 const std::vector<Coordinate> &points, Topology topology,
                 double comm_range, double speed = 1)
      : _rsnic(interface_name), _smnic(interface_name), _sysID(sysID),
        _sync_engine(this, isRSU), _nav(points, topology, comm_range, speed) {
    _rsnic.attach(this, PROTO);
    _smnic.attach(this, PROTO);
    std::cout << get_timestamp() << " Protocol " << _sysID << " ended"
              << std::endl;
  }

public:
  // Destrutor: remove o protocolo da NIC
  ~ProtocolCommom() {
    _rsnic.detach(this, PROTO);
    _smnic.detach(this, PROTO);
  }

  Address getAddr() {
    return Address(getNICPAddr(), getSysID(), 0);
  }

  Address getBroadcastAddr() {
    return Address(getNICPAddr(), BROADCAST_SID, 0);
  }

  Physical_Address getNICPAddr() {
    return _rsnic.address();
  }
  SysID getSysID() {
    return _sysID;
  }

  Address peekOrigin(Buffer *buf) {
    Packet *pkt = buf->data()->template data<Packet>();
    return pkt->header()->origin;
  }

  unsigned char *peekData(Buffer *buf) {
    Packet *pkt = buf->data()->template data<Packet>();
    return pkt->template data<unsigned char>();
  }

  bool peekOriginSysID(Buffer *buf) {
    Packet *pkt = buf->data()->template data<Packet>();
    return pkt->header()->origin.getSysID();
  }

  void free(Buffer *buf) {
    SysID originSysID = peekOriginSysID(buf);
    if (originSysID == _sysID) {
      _smnic.free(buf);
    } else {
      _rsnic.free(buf);
    }
  }

  // Envia uma mensagem:
  // Aloca um buffer (que é um Ethernet::Frame), interpreta o payload (após o
  // cabeçalho Ethernet) como um Packet, monta o pacote e delega o envio à NIC.
  int send(Address &from, Address &to, Control &ctrl, void *data = nullptr,
           unsigned int size = 0, uint64_t recv_timestamp = 0) {
    auto sendWithNIC = [&](auto &nic) -> int {
      Buffer *buf = nic.alloc(sizeof(Header) + size, 1);
      if (buf == nullptr)
        return -1;
      ctrl.setSynchronized(_sync_engine.getSynced());
      ctrl.setNeedSync(_sync_engine.getNeedSync());
      fillBuffer(buf, from, to, ctrl, data, size);
      if (recv_timestamp) {
        buf->data()->template data<Packet>()->header()->timestamp =
            recv_timestamp;
      }
      return nic.send(buf);
    };

    // Broadcast: send through both NICs
    if (to.getSysID() == BROADCAST_SID) {
      int ret_smnic = -1;
      int ret_rsnic = -1;
      _sync_engine.setBroadcastAlreadySent(true);
      if (ctrl.getType() == Control::Type::ANNOUNCE ||
          ctrl.getType() == Control::Type::DELAY_RESP ||
          ctrl.getType() == Control::Type::LATE_SYNC ||
          ctrl.getType() == Control::Type::MAC) {
        ret_rsnic = sendWithNIC(_rsnic);
      } else {
        ret_rsnic = sendWithNIC(_rsnic);
        ret_smnic = sendWithNIC(_smnic);
      }
      if (ret_smnic == -1 && ret_rsnic == -1)
        return -1;
      return ret_smnic >= ret_rsnic ? ret_smnic : ret_rsnic;
    }

    // Regular send: choose appropriate NIC
    return (to.getSysID() == _sysID) ? sendWithNIC(_smnic)
                                     : sendWithNIC(_rsnic);
  }

  // Recebe uma mensagem:
  // Aqui, também interpretamos o payload do Ethernet::Frame como um Packet.
  int receive(Buffer *buf, Address *from, Address *to, Control *ctrl,
              double *coord_x, double *_coord_y, uint64_t *timestamp,
              MAC::Tag *tag, void *data, unsigned int size) {
    SysID originSysID = peekOriginSysID(buf);
    auto receiveFrom = [this, from, to, ctrl, coord_x, _coord_y, timestamp, tag,
                        data, size](auto &nic, Buffer *buf) {
      Packet pkt = Packet();
      nic.receive(buf, &pkt, MTU + sizeof(Header));
      int bytes = fillRecv(pkt, from, to, ctrl, coord_x, _coord_y, timestamp,
                           tag, data, size);
      nic.free(buf);
      return bytes;
    };

    int actual_received_bytes = (originSysID == _sysID)
                                    ? receiveFrom(_smnic, buf)
                                    : receiveFrom(_rsnic, buf);
    return actual_received_bytes;
  }

  void *unmarshal(Buffer *buf) {
    return buf->data()->template data<Packet>();
  }

  bool amILeader() const {
    return _sync_engine.amILeader();
  }

  virtual void update([[maybe_unused]] typename SocketNIC::Observed *obs,
                      [[maybe_unused]] typename SocketNIC::Protocol_Number prot,
                      [[maybe_unused]] Buffer *buf) {};

protected:
  virtual void fillBuffer(Buffer *buf, Address &from, Address &to,
                          Control &ctrl, void *data = nullptr,
                          unsigned int size = 0) {
    // Estrutura do frame ethernet todo:
    // [MAC_D, MAC_S, Proto, Payload = [Addr_S, Addr_D, Data_size, Data_P]]
    buf->data()->src = from.getPAddr();
    buf->data()->dst = SocketNIC::BROADCAST_ADDRESS; // Sempre broadcast
    buf->data()->prot = PROTO;
    // Payload do Ethernet::Frame é o Protocol::Packet
    Packet *pkt = buf->data()->template data<Packet>();
    pkt->header()->origin = from;
    pkt->header()->dest = to;
    pkt->header()->ctrl = ctrl;

    auto [x, y] = _nav.get_location();
    pkt->header()->coord_x = x;
    pkt->header()->coord_y = y;

    pkt->header()->timestamp = _sync_engine.getTimestamp();
    pkt->header()->payloadSize = size;
    buf->setSize(sizeof(NICHeader) + sizeof(Header) + size);
    if (data != nullptr) {
      std::memcpy(pkt->template data<char>(), data, size);
    }
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
  }

  int fillRecv(Packet &pkt, Address *from, Address *to, Control *ctrl,
               double *coord_x, double *coord_y, uint64_t *timestamp,
               MAC::Tag *tag, void *data, unsigned int size) {
    *from = pkt.header()->origin;
    *to = pkt.header()->dest;
    *ctrl = pkt.header()->ctrl;
    *coord_x = pkt.header()->coord_x;
    *coord_y = pkt.header()->coord_y;
    *timestamp = pkt.header()->timestamp;
    unsigned int message_data_size = pkt.header()->payloadSize;
    std::memcpy(tag, &pkt.header()->tag, tag->size());
    unsigned int actual_received_bytes =
        size > message_data_size ? message_data_size : size;
    std::memcpy(data, pkt.template data<char>(), actual_received_bytes);
    return actual_received_bytes;
  }

  SocketNIC _rsnic;
  SharedMemNIC _smnic;
  SysID _sysID;
  SyncEngineP _sync_engine;
  Navigator _nav;
};

#endif // PROTOCOL_COMMOM_HH
