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
  using BufferC = Buffer<Ethernet::Frame>;
  using SocketNIC = NIC<Engine<BufferC>>;
  using SharedMemNIC = NIC<SharedEngine<BufferC>>;
  using ProtocolC = Protocol<SocketNIC, SharedMemNIC>;
  using ComponentC = Component<ProtocolC>;
  using Port = ComponentC::PortC;

public:
  Car(const std::string &label = "", NavigatorCommon *nav)
      : prot(ProtocolC::getInstance(INTERFACE_NAME, getpid(), nav)), label(label) {
  }

  ComponentC create_component(Port p) {
    return ComponentC(&prot, p);
  }

public:
  ProtocolC &prot;
  const std::string label;
};

#endif
