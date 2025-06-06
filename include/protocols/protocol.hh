#ifndef PROTOCOL_HH
#define PROTOCOL_HH

#include "key_keeper.hh"
#include "mac_structs.hh"
#include "protocol_commom.hh"
#include <bit>
#include <cstddef>

template <typename SocketNIC, typename SharedMemNIC>
class Protocol : public ProtocolCommom<SocketNIC, SharedMemNIC> {
public:
  using Base = ProtocolCommom<SocketNIC, SharedMemNIC>;
  using Packet = Base::Packet;
  using SysID = Base::SysID;
  using Address = Base::Address;
  using Port = Base::Port;
  using SyncEngineP = Base::SyncEngineP;
  using Buffer = Base::Buffer;

public:
  static Protocol &getInstance(const char *interface_name, SysID sysID) {
    static Protocol instance(interface_name, sysID);

    return instance;
  }

  Protocol(Protocol const &) = delete;
  void operator=(Protocol const &) = delete;

  ~Protocol() {
  }

protected:
  // Construtor: associa o protocolo à NIC e registra-se como observador do
  // protocolo PROTO
  Protocol(const char *interface_name, SysID sysID)
      : Base(interface_name, sysID, false) {
  }

  // Método update: chamado pela NIC quando um frame é recebido.
  // Agora com 3 parâmetros: o Observed, o protocolo e o buffer.
  void update([[maybe_unused]] typename SocketNIC::Observed *obs,
              [[maybe_unused]] typename SocketNIC::Protocol_Number prot,
              Buffer *buf) {
    uint64_t recv_timestamp = Base::_sync_engine.getTimestamp();
    Packet *pkt = buf->data()->template data<Packet>();
    SysID sysID = pkt->header()->dest.getSysID();
    if (sysID != Base::_sysID && sysID != Base::BROADCAST_SID) {
      Base::_rsnic.free(buf);
      return;
    }

    // Se a mensagem veio da nic de sockets, tratar PTP
    if (pkt->header()->origin.getSysID() != Base::_sysID) {
      if (pkt->header()->ctrl.getType() == Control::Type::DELAY_RESP ||
          pkt->header()->ctrl.getType() == Control::Type::LATE_SYNC) {
        Base::_sync_engine.handlePTP(
            recv_timestamp, pkt->header()->timestamp, pkt->header()->origin,
            pkt->header()->ctrl.getType(), *pkt->template data<int64_t>());
      }

      if (pkt->header()->ctrl.getType() == Control::Type::MAC) {
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

      if (pkt->header()->ctrl.getType() == Control::Type::ANNOUNCE ||
          pkt->header()->ctrl.getType() == Control::Type::DELAY_RESP ||
          pkt->header()->ctrl.getType() == Control::Type::LATE_SYNC ||
          pkt->header()->ctrl.getType() == Control::Type::MAC) {
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

private:
  KeyKeeper _key_keeper;
};

#endif // PROTOCOL_HH
