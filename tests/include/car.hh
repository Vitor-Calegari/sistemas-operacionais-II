#ifndef CAR_HH
#define CAR_HH

#include "component.hh"
#include "engine.hh"
#include "nic.hh"
#include "protocol.hh"
#include "shared_engine.hh"

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
  Car()
      : rsnic(INTERFACE_NAME), smnic(INTERFACE_NAME),
        prot(ProtocolC::getInstance(&rsnic, &smnic, getpid())) {
  }
  ComponentC create_component(Port p) {
    return ComponentC(&prot, p);
  }

public:
  SocketNIC rsnic;
  SharedMemNIC smnic;
  ProtocolC &prot;
};

#endif
