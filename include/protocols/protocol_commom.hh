#ifndef PROTOCOL_COMMOM_HH
#define PROTOCOL_COMMOM_HH

#include "buffer.hh"
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
class ProtocolCommom : public Concurrent_Observed<Buffer, unsigned short>,
                       private SocketNIC::Observer {
public:
  inline static const typename SocketNIC::Protocol_Number PROTO = htons(0x88B5);

  typedef SyncEngine<ProtocolCommom<SocketNIC, SharedMemNIC, Navigator>>
      SyncEngineP;
  typedef typename SocketNIC::Header SocketNICHeader;
  typedef typename SharedMemNIC::Header SharedMemNICHeader;
  typedef typename SocketNIC::Address Physical_Address;
  typedef pid_t SysID;
  typedef unsigned short Port;

  typedef typename SocketNIC::NICFrameClass::Frame SocketFrame;
  typedef typename SharedMemNIC::NICFrameClass::Frame SharedMFrame;

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
    void setPort(Port p) {
      _port = p;
    }

  private:
    Physical_Address _paddr;
    SysID _sysID;
    Port _port;
  } __attribute__((packed));

  class LiteHeader {
  public:
    LiteHeader() : origin(0), dest(0), ctrl(0), payloadSize(0) {
    }
    Port origin;
    Port dest;
    Control ctrl;
    std::size_t payloadSize;
  } __attribute__((packed));

  class FullHeader {
  public:
    FullHeader()
        : origin(Address()), dest(Address()), ctrl(0), payloadSize(0),
          coord_x(0), coord_y(0), timestamp(0), tag{} {
    }
    Address origin;
    Address dest;
    Control ctrl;
    std::size_t payloadSize;
    double coord_x;
    double coord_y;
    int64_t timestamp;
    MAC::Tag tag;
  } __attribute__((packed));

  // MTU disponível para o payload: espaço total do buffer menos o tamanho do
  // Header
  inline static const unsigned int MTU = SocketNIC::MTU - sizeof(FullHeader);
  typedef std::byte Data[MTU];

  // Pacote do protocolo: composto pelo Header e o Payload
  template <typename Header>
  class Packet : public Header {
  public:
    inline static const unsigned int MTU = SocketNIC::MTU - sizeof(Header);
    typedef unsigned char Data[MTU];

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

  using LitePacket = Packet<LiteHeader>;
  using FullPacket = Packet<FullHeader>;

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
      : _sync_engine(this, isRSU),
        _rsnic(interface_name, _sync_engine.getClock()),
        _smnic(interface_name, _sync_engine.getClock()), _sysID(sysID),
        _nav(points, topology, comm_range, speed) {
    _rsnic.attach(this, PROTO);
    _smnic.attach(this, PROTO);
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

  Navigator::Coordinate getLocation() {
    return _nav.get_location();
  }

  Address peekOrigin(Buffer *buf) {
    if (buf->type() == Buffer::EthernetFrame) {
      return buf->template data<SocketFrame>()
          ->template data<FullPacket>()
          ->header()
          ->origin;
    } else {
      Address addr = getAddr();
      addr.setPort(buf->template data<SocketFrame>()
                       ->template data<LitePacket>()
                       ->header()
                       ->origin);
      return addr;
    }
  }

  SysID peekOriginSysID(Buffer *buf) {
    if (buf->type() == Buffer::EthernetFrame) {
      return buf->template data<SocketFrame>()
          ->template data<FullPacket>()
          ->header()
          ->origin.getSysID();
    } else {
      return getSysID();
    }
  }

  void free(Buffer *buf) {
    if (buf->type() == Buffer::EthernetFrame) {
      _rsnic.free(buf);
    } else {
      _smnic.free(buf);
    }
  }

  // Envia uma mensagem:
  // Aloca um buffer (que é um SocketFrame), interpreta o payload (após o
  // cabeçalho Ethernet) como um Packet, monta o pacote e delega o envio à NIC.
  int send(Address &from, Address &to, Control &ctrl, void *data = nullptr,
           unsigned int size = 0, int64_t recv_timestamp = 0) {
    Port from_port = from.getPort();
    Port to_port = to.getPort();
    // Broadcast: send through both NICs
    if (to.getSysID() == BROADCAST_SID) {
      int ret_smnic = -1;
      int ret_rsnic = -1;
      _sync_engine.setBroadcastAlreadySent(true);
      if (ctrl.getType() == Control::Type::ANNOUNCE ||
          ctrl.getType() == Control::Type::DELAY_RESP ||
          ctrl.getType() == Control::Type::LATE_SYNC ||
          ctrl.getType() == Control::Type::MAC) {
        ret_rsnic = sendSocket(from, to, ctrl, data, size, recv_timestamp);
      } else {
        ret_rsnic = sendSocket(from, to, ctrl, data, size, recv_timestamp);
        ret_smnic = sendSharedMem(from_port, to_port, ctrl, data, size);
      }
      if (ret_smnic == -1 && ret_rsnic == -1)
        return -1;
      return ret_smnic >= ret_rsnic ? ret_smnic : ret_rsnic;
    }

    // Regular send: choose appropriate NIC
    return (to.getSysID() == _sysID)
               ? sendSharedMem(from_port, to_port, ctrl, data, size)
               : sendSocket(from, to, ctrl, data, size, recv_timestamp);
  }

  // Recebe uma mensagem:
  // Aqui, também interpretamos o payload do SocketFrame como um Packet.
  int receive(Buffer *buf, Address *from, Address *to, Control *ctrl,
              double *coord_x, double *_coord_y, int64_t *timestamp,
              void *data, unsigned int size) {
    int received_bytes = 0;

    if (buf->type() == Buffer::EthernetFrame) {
      FullPacket *pkt =
          buf->template data<SocketFrame>()->template data<FullPacket>();
      received_bytes = fillRecvFullMsg(pkt, from, to, ctrl, coord_x, _coord_y,
                                       timestamp, data, size);
      _rsnic.free(buf);
    } else {
      LitePacket *pkt =
          buf->template data<SharedMFrame>()->template data<LitePacket>();
      received_bytes = fillRecvLiteMsg(pkt, from, to, ctrl, data, size);
      *timestamp = buf->get_receive_time();
      _smnic.free(buf);
    }
    return received_bytes;
  }

  void *unmarshal(Buffer *buf) {
    if (buf->type() == Buffer::EthernetFrame) {
      return buf->template data<SocketFrame>()->template data<FullPacket>();
    } else {
      return buf->template data<SharedMFrame>()->template data<LitePacket>();
    }
  }

  Control::Type getPType(Buffer *buf) {
    if (buf->type() == Buffer::EthernetFrame) {
      return buf->template data<SocketFrame>()
          ->template data<FullPacket>()
          ->ctrl.getType();
    } else {
      return buf->template data<SharedMFrame>()
          ->template data<LitePacket>()
          ->ctrl.getType();
    }
  }

  char *peekPacketData(Buffer *buf) {
    if (buf->type() == Buffer::EthernetFrame) {
      return buf->template data<SocketFrame>()
          ->template data<FullPacket>()
          ->template data<char>();
    } else {
      return buf->template data<SharedMFrame>()
          ->template data<LitePacket>()
          ->template data<char>();
    }
  }

  bool amILeader() const {
    return _sync_engine.amILeader();
  }

  virtual void update([[maybe_unused]] typename SocketNIC::Observed *obs,
                      [[maybe_unused]] typename SocketNIC::Protocol_Number prot,
                      [[maybe_unused]] Buffer *buf) {};

private:
  int sendSocket(Address &from, Address &to, Control &ctrl,
                 void *data = nullptr, unsigned int size = 0,
                 int64_t recv_timestamp = 0) {
    Buffer *buf = _rsnic.alloc(sizeof(FullHeader) + size, 1);
    if (buf == nullptr)
      return -1;
    ctrl.setSynchronized(_sync_engine.getSynced());
    ctrl.setNeedSync(_sync_engine.getNeedSync());
    fillFullPacket(buf, from, to, ctrl, data, size);
    if (recv_timestamp) {
      if (buf->type() == Buffer::EthernetFrame) {
        buf->template data<SocketFrame>()
            ->template data<FullPacket>()
            ->header()
            ->timestamp = recv_timestamp;
      }
    }
    int ret = _rsnic.send(buf);
    _rsnic.free(buf);
    return ret;
  }

  int sendSharedMem(Port &from, Port &to, Control &ctrl, void *data = nullptr,
                    unsigned int size = 0) {
    Buffer *buf = _smnic.alloc(sizeof(LiteHeader) + size, 1);
    if (buf == nullptr)
      return -1;
    fillLitePacket(buf, from, to, ctrl, data, size);
    return _smnic.send(buf);
  }

protected:
  virtual void fillLitePacket(Buffer *buf, Port &from, Port &to, Control &ctrl,
                              void *data = nullptr, unsigned int size = 0) {
    // Frame de sharedmem não tem MAC
    buf->template data<SharedMFrame>()->prot = PROTO;
    // Payload do SocketFrame é o Protocol::Packet
    LitePacket *pkt =
        buf->template data<SharedMFrame>()->template data<LitePacket>();
    pkt->header()->origin = from;
    pkt->header()->dest = to;
    pkt->header()->ctrl = ctrl;
    pkt->header()->payloadSize = size;
    buf->setSize(sizeof(SharedMemNICHeader) + sizeof(LiteHeader) + size);
    if (data != nullptr) {
      std::memcpy(pkt->template data<char>(), data, size);
    }
  }

  virtual void fillFullPacket(Buffer *buf, Address &from, Address &to,
                              Control &ctrl, void *data = nullptr,
                              unsigned int size = 0) {
    buf->template data<SocketFrame>()->src = from.getPAddr();
    buf->template data<SocketFrame>()->dst =
        SocketNIC::BROADCAST_ADDRESS; // Sempre broadcast
    buf->template data<SocketFrame>()->prot = PROTO;
    FullPacket *pkt =
        buf->template data<SocketFrame>()->template data<FullPacket>();
    pkt->header()->origin = from;
    pkt->header()->dest = to;
    pkt->header()->ctrl = ctrl;
    pkt->header()->payloadSize = size;
    buf->setSize(sizeof(SocketNICHeader) + sizeof(FullHeader) + size);
    auto [x, y] = _nav.get_location();
    pkt->header()->coord_x = x;
    pkt->header()->coord_y = y;
    pkt->header()->timestamp = _sync_engine.getTimestamp();
    if (data != nullptr) {
      std::memcpy(pkt->template data<char>(), data, size);
    }
  }

  int fillRecvLiteMsg(LitePacket *pkt, Address *from, Address *to,
                      Control *ctrl, void *data, unsigned int size) {
    *from = getAddr();
    from->setPort(pkt->header()->origin);
    *to = getAddr();
    to->setPort(pkt->header()->dest);
    *ctrl = pkt->header()->ctrl;
    // Header pequeno não tem coords e timestamp
    unsigned int message_data_size = pkt->header()->payloadSize;
    unsigned int actual_received_bytes =
        size > message_data_size ? message_data_size : size;
    std::memcpy(data, pkt->template data<char>(), actual_received_bytes);
    return actual_received_bytes;
  }

  int fillRecvFullMsg(FullPacket *pkt, Address *from, Address *to,
                      Control *ctrl, double *coord_x, double *coord_y,
                      int64_t *timestamp, void *data, unsigned int size) {
    *from = pkt->header()->origin;
    *to = pkt->header()->dest;
    *ctrl = pkt->header()->ctrl;
    *coord_x = pkt->header()->coord_x;
    *coord_y = pkt->header()->coord_y;
    *timestamp = pkt->header()->timestamp;
    unsigned int message_data_size = pkt->header()->payloadSize;
    unsigned int actual_received_bytes =
        size > message_data_size ? message_data_size : size;
    std::memcpy(data, pkt->template data<char>(), actual_received_bytes);
    return actual_received_bytes;
  }

  SyncEngineP _sync_engine;
  SocketNIC _rsnic;
  SharedMemNIC _smnic;
  SysID _sysID;
  Navigator _nav;
};

#endif // PROTOCOL_COMMOM_HH
