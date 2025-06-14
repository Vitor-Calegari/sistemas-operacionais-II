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
  using LitePacket = Base::LitePacket;
  using FullPacket = Base::FullPacket;
  using SysID = Base::SysID;
  using Address = Base::Address;
  using Port = Base::Port;
  using SyncEngineP = Base::SyncEngineP;
  using Buffer = Base::Buffer;
  using Coordinate = Navigator::Coordinate;

public:
  static Protocol &getInstance(const char *interface_name, SysID sysID,
                               const std::vector<Coordinate> &points,
                               Topology topology, double comm_range,
                               double speed = 1) {
    static Protocol instance(interface_name, sysID, points, topology,
                             comm_range, speed);

    return instance;
  }

  Protocol(Protocol const &) = delete;
  void operator=(Protocol const &) = delete;

  ~Protocol() {
  }

protected:
  // Construtor: associa o protocolo à NIC e registra-se como observador do
  // protocolo PROTO
  Protocol(const char *interface_name, SysID sysID,
           const std::vector<Coordinate> &points, Topology topology,
           double comm_range, double speed = 1)
      : Base(interface_name, sysID, false, points, topology, comm_range,
             speed) {
  }

  // Método update: chamado pela NIC quando um frame é recebido.
  // Agora com 3 parâmetros: o Observed, o protocolo e o buffer.
  void update([[maybe_unused]] typename SocketNIC::Observed *obs,
              [[maybe_unused]] typename SocketNIC::Protocol_Number prot,
              Buffer *buf) override {
    auto handlePacket = [this](auto &nic, Buffer *buf, Port port) {
      if (port == Base::BROADCAST) {
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

    uint64_t recv_timestamp = Base::_sync_engine.getTimestamp();

    LitePacket *lite_pkt = buf->data()->template data<LitePacket>();
    SysID originSysId = lite_pkt->header()->origin.getSysID();
    SysID destSysId = lite_pkt->header()->dest.getSysID();
    Port port = lite_pkt->header()->dest.getPort();

    if (originSysId != Base::_sysID) {
      FullPacket *pkt = buf->data()->template data<FullPacket>();
      double coord_x = pkt->header()->coord_x;
      double coord_y = pkt->header()->coord_y;
      Control::Type pkt_type = pkt->header()->ctrl.getType();
  
      if (destSysId != Base::_sysID && destSysId != Base::BROADCAST_SID) {
        Base::_rsnic.free(buf);
        return;
      }

      bool notFromRSU = (pkt_type != Control::Type::DELAY_RESP &&
                          pkt_type != Control::Type::LATE_SYNC &&
                          pkt_type != Control::Type::MAC);

      // Se não é uma mensagem de RSU, verifica MAC
      if (notFromRSU && received_first_keys) {
        int q_id = Base::_nav.get_topology().get_quadrant_id({ coord_x, coord_y });
        MAC::Key key = _key_keeper.getKey(q_id);
  
        MAC::Tag tag = pkt->header()->tag;
#ifdef DEBUG_MAC
        if (pkt_type == Control::Type::COMMON) {
          std::cout << "Message Tag: ";
          for (const auto byte : tag) {
            std::cout << std::hex << static_cast<int>(byte) << " ";
          }
          std::cout << std::endl;
        }
#endif
        pkt->header()->tag = {};
        auto msg = std::bit_cast<std::array<std::byte, sizeof(FullPacket)>>(*pkt);
        std::vector<std::byte> msg_vec(msg.begin(), msg.end());
#ifdef DEBUG_MAC
        if (pkt_type == Control::Type::COMMON) {
          auto exp_tag = MAC::compute(key, msg_vec);
          std::cout << "Expected Tag: ";
          for (const auto byte : exp_tag) {
            std::cout << std::hex << static_cast<int>(byte) << " ";
          }
          std::cout << std::endl;
        }
#endif
  
        if (!MAC::verify(key, msg_vec, tag)) {
#ifdef DEBUG_MAC
        if (pkt_type == Control::Type::COMMON) {
          std::cout << "Fake drop" << std::endl;
        }
#endif
#ifndef DEBUG_MAC
        Base::free(buf);
        return;
#endif
        }
      }

      // Se não é uma mensagem de uma RSU, trata com o range do carro
      if (notFromRSU && !Base::_nav.is_in_range({ coord_x, coord_y })) {
        Base::free(buf);
        return;
      }

      // Se é uma mensagem da RSU, trata com o range da RSU
      if (!notFromRSU) {
        auto quadrant_rsu =
            Base::_nav.get_topology().get_quadrant_id({ coord_x, coord_y });
        auto quadrant =
            Base::_nav.get_topology().get_quadrant_id(Base::_nav.get_location());
        if (quadrant_rsu != quadrant) {
          Base::free(buf);
          return;
        }
      }

      // Se é uma mensagem PTP, trata PTP
      if (pkt_type == Control::Type::DELAY_RESP ||
          pkt_type == Control::Type::LATE_SYNC) {
        Base::_sync_engine.handlePTP(recv_timestamp, pkt->header()->timestamp,
                                      pkt->header()->origin, pkt_type,
                                      *pkt->template data<int64_t>());
      }

      // Se é uma mensagem de chaves MAC, armazena chaves MAC
      if (pkt_type == Control::Type::MAC) {
        std::vector<MacKeyEntry> keys;

        auto data = pkt->template data<std::byte>();
        for (int i = 0; i < 9; ++i) {
          std::array<std::byte, sizeof(MacKeyEntry)> key_bytes{};

          bool is_fully_zeroed = true;
          for (size_t j = 0; j < key_bytes.size(); ++j) {
            key_bytes[j] = data[sizeof(MacKeyEntry) * i + j];
            if (key_bytes[j] != std::byte(0)) {
              is_fully_zeroed = false;
            }
          }

          if (!is_fully_zeroed) {
            keys.push_back(std::bit_cast<MacKeyEntry>(key_bytes));
          }
        }

        _key_keeper.setKeys(keys);
        received_first_keys = true;
      }

      // Se é uma mensagem de controle, libera buffer
      if (pkt_type == Control::Type::ANNOUNCE ||
          !notFromRSU) {
        Base::free(buf);
        return;
      }

      handlePacket(Base::_rsnic, buf, port);
    } else {
      handlePacket(Base::_smnic, buf, port);
    }
  }

protected:
  void fillFullPacket(Buffer *buf, Address &from, Address &to, Control &ctrl,
                  void *data = nullptr, unsigned int size = 0) override {
    // Estrutura do frame ethernet todo:
    // [MAC_D, MAC_S, Proto, Payload = [Addr_S, Addr_D, Data_size, Data_P]]
    buf->data()->src = from.getPAddr();
    buf->data()->dst = SocketNIC::BROADCAST_ADDRESS; // Sempre broadcast
    buf->data()->prot = Base::PROTO;
    // Payload do Ethernet::Frame é o Protocol::Packet
    FullPacket *pkt = buf->data()->template data<FullPacket>();
    pkt->header()->origin = from;
    pkt->header()->dest = to;
    pkt->header()->ctrl = ctrl;

    auto [x, y] = Base::_nav.get_location();
    pkt->header()->coord_x = x;
    pkt->header()->coord_y = y;

    pkt->header()->timestamp = Base::_sync_engine.getTimestamp();
    pkt->header()->payloadSize = size;
    buf->setSize(sizeof(typename Base::NICHeader) +
                 sizeof(typename Base::FullHeader) + size);
    if (data != nullptr) {
      std::memcpy(pkt->template data<char>(), data, size);
    }

    MAC::Key key =
        _key_keeper.getKey(Base::_nav.get_topology().get_quadrant_id({ x, y }));
    auto msg = std::bit_cast<std::array<std::byte, sizeof(FullPacket)>>(*pkt);
    std::vector<std::byte> msg_vec(msg.begin(), msg.end());
    auto tag = MAC::compute(key, msg_vec);
#ifdef DEBUG_MAC
    tag = MAC::Tag{};
#endif
    pkt->header()->tag = tag;
  }

private:
  bool received_first_keys;
  KeyKeeper _key_keeper;
};

#endif // PROTOCOL_HH
