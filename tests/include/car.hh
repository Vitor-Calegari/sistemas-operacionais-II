#ifndef CAR_HH
#define CAR_HH


#include "communicator.hh"
#include "engine.hh"
#include "message.hh"
#include "nic.hh"
#include "protocol.hh"
#include "shared_engine.hh"
#include "smart_unit.hh"
#include "transducer.hh"

#include "smart_data.hh"
#include "component.hh"

class Car {
    using Buffer = Buffer<Ethernet::Frame>;
    using SocketNIC = NIC<Engine<Buffer>>;
    using SharedMemNIC = NIC<SharedEngine<Buffer>>;
    using Protocol = Protocol<SocketNIC, SharedMemNIC>;
    using Message = Message<Protocol::Address>;
    using Communicator = Communicator<Protocol, Message>;

    // TODO Aparentemente a únidade de um LiDAR é mais complexa do que esperavamos:
    // https://www.asprs.org/wp-content/uploads/2019/03/LAS_1_4_r14.pdf
    using Lidar = Publisher<Communicator, SmartUnit(SmartUnit::SIUnit::M)>;
public:
    Car() {}
public:
private:

};

#endif