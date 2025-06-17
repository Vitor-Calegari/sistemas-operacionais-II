#ifndef CAR_HH
#define CAR_HH

#include "component.hh"
#include "engine.hh"
#include "nic.hh"
#include "protocol.hh"
#include "shared_engine.hh"
#include "navigator.hh"

#ifndef INTERFACE_NAME
#define INTERFACE_NAME "lo"
#endif

class Car {
public:
  using SocketNIC = NIC<Engine<Ethernet>>;
  using SharedMemNIC = NIC<SharedEngine<Ethernet>>;
  using ProtocolC = Protocol<SocketNIC, SharedMemNIC, NavigatorDirected>;
  using ComponentC = Component<ProtocolC>;
  using Port = ComponentC::PortC;
  using Coordinate = NavigatorDirected::Coordinate;

public:
  Car(const std::string &label = "", const std::vector<Coordinate> &points = {{0, 0}}, Topology topology = Topology({1, 1}, 10),
    double comm_range = 5, double speed = 1)
      : prot(ProtocolC::getInstance(INTERFACE_NAME, getpid(), points, topology,
      comm_range, speed)), label(label) {
  }

  ComponentC create_component(Port p) {
    return ComponentC(&prot, p);
  }

public:
  ProtocolC &prot;
  const std::string label;
};

#endif
