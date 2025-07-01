#ifndef TOWER_PROTOCOL_HH
#define TOWER_PROTOCOL_HH

#include "navigator.hh"
#include "protocol_commom.hh"
#include "rsu_engine.hh"

template <typename SocketNIC, typename SharedMemNIC, typename Navigator>
class RSUProtocol : public ProtocolCommom<SocketNIC, SharedMemNIC, Navigator> {
public:
  using Base = ProtocolCommom<SocketNIC, SharedMemNIC, Navigator>;
  using LitePacket = Base::LitePacket;
  using FullPacket = Base::FullPacket;
  using SysID = Base::SysID;
  using Address = Base::Address;
  using Port = Base::Port;
  using SyncEngineP = Base::SyncEngineP;
  using RSUEngineP = RSUEngine<RSUProtocol<SocketNIC, SharedMemNIC, Navigator>>;
  using Coord = RSUEngineP::Coord;
  using Coordinate = Navigator::Coordinate;

public:
  static RSUProtocol &getInstance(const char *interface_name, SysID sysID,
                                  SharedData *shared_data, Coord coord, int id,
                                  const std::vector<Coordinate> &points,
                                  Topology topology, double comm_range,
                                  double speed = 1) {
    static RSUProtocol instance(interface_name, sysID, shared_data, coord, id,
                                points, topology, comm_range, speed);
    return instance;
  }

  RSUProtocol(RSUProtocol const &) = delete;
  void operator=(RSUProtocol const &) = delete;

  ~RSUProtocol() {
#ifdef DEBUG_RSU_PROTOCOL
    std::cout << get_timestamp() << " RSU Protocol " << Base::_sysID << " ended"
              << std::endl;
#endif
  }

protected:
  // Construtor: associa o protocolo à NIC e registra-se como observador do
  // protocolo PROTO
  RSUProtocol(const char *interface_name, SysID sysID, SharedData *shared_data,
              Coord coord, int id, const std::vector<Coordinate> &points,
              Topology topology, double comm_range, double speed = 1)
      : Base(interface_name, sysID, true, points, topology, comm_range, speed),
        _crypto_engine(this, shared_data, coord, id) {
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
          if (buf->type() == Buffer::EthernetFrame) {
            std::memcpy(broadcastBuf->template data<char>(), buf->template data<FullPacket>(), buf->size());
          } else {
            std::memcpy(broadcastBuf->template data<char>(), buf->template data<LitePacket>(), buf->size());
          }
          broadcastBuf->set_receive_time(buf->get_receive_time());
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

#ifdef DEBUG_TIMESTAMP_2
    std::cout << get_timestamp() << " I’m RSU " << getpid() << " I received a";
    if (pkt->header()->ctrl.getType() == Control::Type::ANNOUNCE) {
      std::cout << " ANNOUNCE message " << std::endl;
    } else {
      std::cout << " OUTRO message " << std::endl;
    }
#endif

    if (buf->type() == Buffer::EthernetFrame) {
      FullPacket *pkt = buf->template data<typename Base::SocketFrame>()->template data<FullPacket>();
      SysID destSysId = pkt->header()->dest.getSysID();
      Port port = pkt->header()->dest.getPort();
      if (destSysId != Base::_sysID && destSysId != Base::UNIVERSAL_BROADCAST && destSysId != Base::EXT_BROADCAST) {
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
                     buf->get_receive_time());
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
      handlePacket(Base::_rsnic, buf, port);
    } else {
      LitePacket *lite_pkt = buf->template data<typename Base::SharedMFrame>()->template data<LitePacket>();
      Port port = lite_pkt->header()->dest;
      handlePacket(Base::_smnic, buf, port);
    }
  }

private:
  RSUEngineP _crypto_engine;
};

#endif // RSU_PROTOCOL_HH
