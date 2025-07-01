#ifndef CAR_HH
#define CAR_HH

#include "cam_transducer.hh"
#include "component.hh"
#include "engine.hh"
#include "navigator.hh"
#include "nic.hh"
#include "protocol.hh"
#include "shared_engine.hh"
#include "shared_mem.hh"
#include "smart_unit.hh"

#ifndef INTERFACE_NAME
#define INTERFACE_NAME "lo"
#endif

class Car {
public:
  using SocketNIC = NIC<Engine<Ethernet>>;
  using SharedMemNIC = NIC<SharedEngine<SharedMem>>;
  using ProtocolC = Protocol<SocketNIC, SharedMemNIC, NavigatorDirected>;
  using ComponentC = Component<ProtocolC>;
  using Port = ComponentC::PortC;
  using Coordinate = NavigatorDirected::Coordinate;

public:
  Car(const std::string &label = "",
      const std::vector<Coordinate> &points = { { 0, 0 } },
      Topology topology = Topology({ 1, 1 }, 10), double comm_range = 5,
      double speed = 1)
      : prot(ProtocolC::getInstance(INTERFACE_NAME, getpid(), points, topology,
                                    comm_range, speed)),
        label(label) {
  }

  ComponentC create_component(Port p) {
    return ComponentC(&prot, p);
  }

public:
  ProtocolC &prot;
  const std::string label;
};

class E7Car {
public:
  using SocketNIC = NIC<Engine<Ethernet>>;
  using SharedMemNIC = NIC<SharedEngine<SharedMem>>;
  using ProtocolC = Protocol<SocketNIC, SharedMemNIC, NavigatorDirected>;
  using ComponentC = Component<ProtocolC>;
  using Port = ComponentC::PortC;
  using Coordinate = NavigatorDirected::Coordinate;

  static constexpr SmartUnit cam_unit = CAMTransducer::get_unit();

public:
  E7Car(const std::string dataset_id, const std::string &label = "",
        const std::vector<Coordinate> &points = { { 0, 0 } },
        Topology topology = Topology({ 1, 1 }, 10), double comm_range = 5,
        double speed = 1)
      : prot(ProtocolC::getInstance(INTERFACE_NAME, getpid(), points, topology,
                                    comm_range, speed)),
        label(label), baseComp(&prot, 1),
        CAM_subs(&baseComp._comm,
                 Condition(false, cam_unit.get_int_unit(), 100000), false),

        CAM_trand("tests/dataset/dynamics-vehicle_" + dataset_id + ".csv"),

        CAM_prod(&baseComp._comm, &CAM_trand,
                 Condition(true, cam_unit.get_int_unit(), 100000), false),
        _dataset_id(dataset_id) {
  }

  ComponentC create_component(Port p) {
    return ComponentC(&prot, p);
  }

  void receive(ComponentC::MessageC *msg) {
    CAM_subs.receive(msg);
  }

public:
  ProtocolC &prot;
  const std::string label;
  ComponentC baseComp;
  SmartData<ComponentC::CommunicatorC, Condition> CAM_subs;
  CAMTransducer CAM_trand;
  SmartData<ComponentC::CommunicatorC, Condition, CAMTransducer> CAM_prod;
  std::string _dataset_id;
};

#endif
