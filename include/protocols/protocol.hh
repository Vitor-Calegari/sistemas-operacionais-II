#ifndef PROTOCOL_HH
#define PROTOCOL_HH

#include "control.hh"
#include "key_keeper.hh"
#include "mac.hh"
#include "mac_structs.hh"
#include "navigator.hh"
#include "protocol_commom.hh"
#include <bit>
#include <cstddef>

template <typename SocketNIC, typename SharedMemNIC, typename Navigator>
class Protocol : public ProtocolCommom<SocketNIC, SharedMemNIC, Navigator> {
public:
  using Base = ProtocolCommom<SocketNIC, SharedMemNIC, Navigator>;
  using Packet = Base::Packet;
  using SysID = Base::SysID;
  using Address = Base::Address;
  using Port = Base::Port;
  using SyncEngineP = Base::SyncEngineP;
  using Buffer = Base::Buffer;
  using Coordinate = Navigator::Coordinate;

public:
  static Protocol &getInstance(const char *interface_name, SysID sysID,
    const std::vector<Coordinate> &points, Topology topology, double comm_range, double speed = 1) {
    static Protocol instance(interface_name, sysID, points, topology, comm_range, speed);

    return instance;
  }

  Protocol(Protocol const &) = delete;
  void operator=(Protocol const &) = delete;

  ~Protocol() {
  }

protected:
  // Construtor: associa o protocolo à NIC e registra-se como observador do
  // protocolo PROTO
  Protocol(const char *interface_name, SysID sysID, const std::vector<Coordinate> &points, Topology topology, double comm_range, double speed = 1)
      : Base(interface_name, sysID, false, points, topology, comm_range, speed) {
  }

  // Método update: chamado pela NIC quando um frame é recebido.
  // Agora com 3 parâmetros: o Observed, o protocolo e o buffer.
  void update([[maybe_unused]] typename SocketNIC::Observed *obs,
              [[maybe_unused]] typename SocketNIC::Protocol_Number prot,
              Buffer *buf) {
    uint64_t recv_timestamp = Base::_sync_engine.getTimestamp();
    Packet *pkt = buf->data()->template data<Packet>();
    double coord_x = pkt->header()->coord_x;
    double coord_y = pkt->header()->coord_y;
    if (pkt->header()->tag != MAC::Tag{}) {
      MAC::Key key = _key_keeper.getKey(
          Base::_nav.get_topology().get_quadrant_id({ coord_x, coord_y }));

      MAC::Tag tag = pkt->header()->tag;
      pkt->header()->tag = {};
      auto msg = std::bit_cast<std::array<std::byte, sizeof(Packet)>>(*pkt);
      std::vector<std::byte> msg_vec(msg.begin(), msg.end());

      if (!MAC::verify(key, msg_vec, tag)) {
        Base::free(buf);
        return;
      }
    }

    Control::Type pkt_type = pkt->header()->ctrl.getType();

    if (pkt_type != Control::Type::MAC &&
        !Base::_nav.is_in_range({ coord_x, coord_y })) {
      Base::free(buf);
      return;
    }

    if (pkt_type == Control::Type::MAC) {
      auto quadrant_rsu =
          Base::_nav.get_topology().get_quadrant_id({ coord_x, coord_y });
      auto quadrant = Base::_nav.get_topology().get_quadrant_id(
          Base::_nav.get_location());

      if (quadrant_rsu != quadrant) {
        Base::free(buf);
        return;
      }
    }

    SysID sysID = pkt->header()->dest.getSysID();
    if (sysID != Base::_sysID && sysID != Base::BROADCAST_SID) {
      Base::_rsnic.free(buf);
      return;
    }

    // Se a mensagem veio da nic de sockets, tratar PTP
    if (pkt->header()->origin.getSysID() != Base::_sysID) {
      if (pkt_type == Control::Type::DELAY_RESP ||
          pkt_type == Control::Type::LATE_SYNC) {
        Base::_sync_engine.handlePTP(recv_timestamp, pkt->header()->timestamp,
                                     pkt->header()->origin, pkt_type,
                                     *pkt->template data<int64_t>());
      }

      if (pkt_type == Control::Type::MAC) {
        std::vector<MacKeyEntry> keys;

        auto data = pkt->template data<std::byte>();
        for (int i = 0; i < 9; ++i) {
          std::array<std::byte, sizeof(MacKeyEntry)> key_bytes{};

          bool is_fully_zeroed = true;
          for (size_t j = 0; j < key_bytes.size(); ++j) {
            key_bytes[j] = data[sizeof(MacKeyEntry) * i + j];
            is_fully_zeroed |= (key_bytes[j] != std::byte(0));
          }

          if (!is_fully_zeroed) {
            keys.push_back(std::bit_cast<MacKeyEntry>(key_bytes));
          }
        }

        _key_keeper.setKeys(keys);
      }

      if (pkt_type == Control::Type::ANNOUNCE ||
          pkt_type == Control::Type::DELAY_RESP ||
          pkt_type == Control::Type::LATE_SYNC ||
          pkt_type == Control::Type::MAC) {
        Base::free(buf);
        return;
      }
    }

    auto handlePacket = [this](auto &nic, Buffer *buf, Packet *pkt) {
      Port port = pkt->header()->dest.getPort();
      if (pkt->header()->dest.getPort() == Base::BROADCAST) {
        std::vector<Port> allComPorts = Base::Observed::getObservsCond();
        Buffer *broadcastBuf;
        for (auto port : allComPorts) {
          broadcastBuf = nic.alloc(buf->size(), 0);
          if (broadcastBuf == nullptr)
            continue;
          std::memcpy(broadcastBuf->data(), buf->data(), buf->size());
          broadcastBuf->setSize(buf->size());
          if (!this->notify(port, broadcastBuf)) {
            nic.free(broadcastBuf);
          }
        }
        nic.free(buf);
      } else if (!this->notify(port, buf)) {
        nic.free(buf);
      }
    };

    if (pkt->header()->origin.getSysID() == Base::_sysID) {
      handlePacket(Base::_smnic, buf, pkt);
    } else {
      handlePacket(Base::_rsnic, buf, pkt);
    }
  }

protected:
  void fillBuffer(Buffer *buf, Address &from, Address &to, Control &ctrl,
                  void *data = nullptr, unsigned int size = 0) {
    // Estrutura do frame ethernet todo:
    // [MAC_D, MAC_S, Proto, Payload = [Addr_S, Addr_D, Data_size, Data_P]]
    buf->data()->src = from.getPAddr();
    buf->data()->dst = SocketNIC::BROADCAST_ADDRESS; // Sempre broadcast
    buf->data()->prot = Base::PROTO;
    // Payload do Ethernet::Frame é o Protocol::Packet
    Packet *pkt = buf->data()->template data<Packet>();
    pkt->header()->origin = from;
    pkt->header()->dest = to;
    pkt->header()->ctrl = ctrl;

    auto [x, y] = Base::_nav.get_location();
    pkt->header()->coord_x = x;
    pkt->header()->coord_y = y;

    pkt->header()->timestamp = Base::_sync_engine.getTimestamp();
    pkt->header()->payloadSize = size;
    buf->setSize(sizeof(Base::NICHeader) + sizeof(Base::Header) + size);
    if (data != nullptr) {
      std::memcpy(pkt->template data<char>(), data, size);
    }

    MAC::Key key = _key_keeper.getKey(
        Base::_nav.get_topology().get_quadrant_id({ x, y }));
    auto msg = std::bit_cast<std::array<std::byte, sizeof(Packet)>>(*pkt);
    std::vector<std::byte> msg_vec(msg.begin(), msg.end());
    auto tag = MAC::compute(key, msg_vec);
    pkt->header()->tag = tag;
  }

private:
  KeyKeeper _key_keeper;
};

#endif // PROTOCOL_HH
