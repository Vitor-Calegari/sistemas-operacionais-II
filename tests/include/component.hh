#ifndef COMPONENT_HH
#define COMPONENT_HH

#include "communicator.hh"
#include "message.hh"
#include "smart_data.hh"

template <typename Protocol>
class Component {
public:
  using MessageC = Message<typename Protocol::Address>;
  using CommunicatorC = Communicator<Protocol, MessageC>;
  using PortC = CommunicatorC::Port;

public:
  Component(Protocol *prot, PortC p) : _comm(prot, p) {
  }

public:
  bool send(MessageC *message) {
    return _comm.send(message);
  }
  bool receive(MessageC *message) {
    return _comm.receive(message);
  }
  template <typename Condition>
  SmartData<CommunicatorC, Condition> subscribe(Condition cond) {
    return SmartData<CommunicatorC, Condition>(&_comm, cond);
  }
  template <typename Condition, typename Transducer>
  SmartData<CommunicatorC, Condition, Transducer>
  register_publisher(Transducer *transd, Condition cond) {
    return SmartData<CommunicatorC, Condition, Transducer>(&_comm, transd,
                                                           cond);
  }
  CommunicatorC::Address addr() { return _comm.addr(); }

private:
  CommunicatorC _comm;
};

#endif
