#ifndef COMPONENT_HH
#define COMPONENT_HH

#include "message.hh"
#include "communicator.hh"
#include "smart_unit.hh"
#include "transducer.hh"
#include "smart_data.hh"

template<typename Protocol>
class Component {
public:
    using Message = Message<Protocol::Address>;
    using Communicator = Communicator<Protocol, Message>;
    using Port = Communicator::Port;
public:
    Component(Protocol * prot, Port p) : _comm(prot, p) {
    }
public:
    bool send(Message *message) {
        return _comm.send(message);
    }
    bool receive(Message *message) {
        return _comm.receive(message);
    }
    template<typename Condition>
    SmartData<Communicator, Condition> subscribe(Condition cond) {
        return SmartData<Communicator, Condition>(&_comm, cond);
    }
    template<typename Condition, typename Transducer>
    SmartData<Communicator, Condition, Transducer> register_publisher(Transducer * transd, Condition cond) {
        return SmartData<Communicator, Condition, Transducer>(&_comm, transd, cond);
    }
private:
    Communicator _comm;
};

#endif