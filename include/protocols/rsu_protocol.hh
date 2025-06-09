#ifndef TOWER_PROTOCOL_HH
#define TOWER_PROTOCOL_HH

#include "navigator.hh"
#include "protocol_commom.hh"
#include "rsu_engine.hh"

template <typename SocketNIC, typename SharedMemNIC, typename Navigator>
class RSUProtocol : public ProtocolCommom<SocketNIC, SharedMemNIC, Navigator> {
public:
  using Base = ProtocolCommom<SocketNIC, SharedMemNIC, Navigator>;
  using Packet = Base::Packet;
  using SysID = Base::SysID;
  using Address = Base::Address;
  using Port = Base::Port;
  using SyncEngineP = Base::SyncEngineP;
  using Buffer = Base::Buffer;
  using RSUEngineP = RSUEngine<RSUProtocol<SocketNIC, SharedMemNIC, Navigator>>;
  using Coord = RSUEngineP::Coord;
  using Coordinate = Navigator::Coordinate;

public:
  static RSUProtocol &getInstance(const char *interface_name, SysID sysID,
                                  SharedData *shared_data, Coord coord, int id,
                                  const std::vector<Coordinate> &points, Topology topology, double comm_range, double speed = 1) {
    static RSUProtocol instance(interface_name, sysID, shared_data, coord, id,
      points, topology, comm_range, speed);
    return instance;
  }

  RSUProtocol(RSUProtocol const &) = delete;
  void operator=(RSUProtocol const &) = delete;

  ~RSUProtocol() {
    std::cout << get_timestamp() << " RSU Protocol " << Base::_sysID << " ended"<< std::endl;
  }

protected:
  // Construtor: associa o protocolo à NIC e registra-se como observador do
  // protocolo PROTO
  RSUProtocol(const char *interface_name, SysID sysID, SharedData *shared_data,
              Coord coord, int id, const std::vector<Coordinate> &points, Topology topology, double comm_range, double speed = 1)
      : Base(interface_name, sysID, true,points,  topology, comm_range, speed),
        _crypto_engine(this, shared_data, coord, id) {
  }

  // Método update: chamado pela NIC quando um frame é recebido.
  // Agora com 3 parâmetros: o Observed, o protocolo e o buffer.
  void update([[maybe_unused]] typename SocketNIC::Observed *obs,
              [[maybe_unused]] typename SocketNIC::Protocol_Number prot,
              Buffer *buf) {
    uint64_t recv_timestamp = Base::_sync_engine.getTimestamp();
    Packet *pkt = buf->data()->template data<Packet>();
    SysID sysID = pkt->header()->dest.getSysID();

#ifdef DEBUG_TIMESTAMP_2
    std::cout << get_timestamp() << " I’m RSU " << getpid() << " I received a";
    if (pkt->header()->ctrl.getType() == Control::Type::ANNOUNCE) {
      std::cout << " ANNOUNCE message " << std::endl;
    } else {
      std::cout << " OUTRO message " << std::endl;
    }
#endif

    if (sysID != Base::_sysID && sysID != Base::BROADCAST_SID) {
      Base::_rsnic.free(buf);
      return;
    }

    if (pkt->header()->ctrl.getType() == Control::Type::DELAY_RESP ||
        pkt->header()->ctrl.getType() == Control::Type::LATE_SYNC ||
        pkt->header()->ctrl.getType() == Control::Type::MAC) {
      Base::free(buf);
      return;
    }

    // Se a mensagem veio da nic de sockets, tratar PTP
    if (pkt->header()->origin.getSysID() != Base::_sysID) {
      // Se não está sincronizado, enviar mensagens para carro sincronizar
      if (pkt->header()->ctrl.needSync()) {
        Address myaddr = Base::getAddr();
        Control ctrl(Control::Type::DELAY_RESP);
        int64_t timestamp_relate_to = pkt->header()->timestamp;
        // Delay Resp
        Base::send(myaddr, pkt->header()->origin, ctrl, &timestamp_relate_to, 8,
                   recv_timestamp);
        // Late Sync
        ctrl.setType(Control::Type::LATE_SYNC);
        Base::send(myaddr, pkt->header()->origin, ctrl, &timestamp_relate_to,
                   8);
      }
#ifdef DEBUG_TIMESTAMP
      else {
        std::cout << get_timestamp() << " I’m RSU " << getpid()
                  << " car already synced " << std::endl;
      }
#endif
      if (pkt->header()->ctrl.getType() == Control::Type::ANNOUNCE) {
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
  RSUEngineP _crypto_engine;
};

#endif // RSU_PROTOCOL_HH
