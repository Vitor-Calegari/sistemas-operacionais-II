#ifndef CAR_HH
#define CAR_HH

#include "engine.hh"
#include "shared_engine.hh"
#include "nic.hh"
#include "protocol.hh"
#include "component.hh"

#ifndef INTERFACE_NAME
#define INTERFACE_NAME "lo"
#endif

class Car {
    using Buffer = Buffer<Ethernet::Frame>;
    using SocketNIC = NIC<Engine<Buffer>>;
    using SharedMemNIC = NIC<SharedEngine<Buffer>>;
    using Protocol = Protocol<SocketNIC, SharedMemNIC>;
    using Component = Component<Protocol>;
    using Port = Component::Port;

public:
    Car() : rsnic(INTERFACE_NAME), smnic(INTERFACE_NAME), prot(Protocol::getInstance(&rsnic, &smnic, getpid())) {
    }
    Component create_component(Port p) {
        return Component(&prot, p);
    }
public:
    SocketNIC rsnic;
    SharedMemNIC smnic;
    Protocol &prot;
};

#endif