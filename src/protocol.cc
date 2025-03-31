#include "protocol.hh"

// Definição do membro estático PROTO
template <typename NIC>
const typename NIC::Protocol_Number Protocol<NIC>::PROTO = Traits<Protocol>::ETHERNET_PROTOCOL_NUMBER;

// Implementação da classe Address
template <typename NIC>
Protocol<NIC>::Address::Address() : _paddr(0), _port(0) { }

template <typename NIC>
Protocol<NIC>::Address::Address(const Null &n) : _paddr(0), _port(0) { }

template <typename NIC>
Protocol<NIC>::Address::Address(Physical_Address paddr, Port port)
    : _paddr(paddr), _port(port) { }

template <typename NIC>
Protocol<NIC>::Address::operator bool() const {
    return (_paddr != 0 || _port != 0);
}

template <typename NIC>
bool Protocol<NIC>::Address::operator==(const Address &other) const {
    return (_paddr == other._paddr) && (_port == other._port);
}

template <typename NIC>
typename Protocol<NIC>::Physical_Address Protocol<NIC>::Address::getPAddr() const {
    return _paddr;
}

template <typename NIC>
typename Protocol<NIC>::Port Protocol<NIC>::Address::getPort() const {
    return _port;
}

// Implementação da classe Header
template <typename NIC>
Protocol<NIC>::Header::Header() : length(0) { }

// Implementação da classe Packet
template <typename NIC>
Protocol<NIC>::Packet::Packet() {
    std::memset(_data, 0, sizeof(_data));
}

template <typename NIC>
typename Protocol<NIC>::Header* Protocol<NIC>::Packet::header() {
    return this;
}

template <typename NIC>
template <typename T>
T* Protocol<NIC>::Packet::data() {
    return reinterpret_cast<T*>(&_data);
}

// Construtor e destrutor do Protocol
template <typename NIC>
Protocol<NIC>::Protocol(NIC* nic) : _nic(nic) {
    _nic->attach(this, PROTO);
}

template <typename NIC>
Protocol<NIC>::~Protocol() {
    _nic->detach(this, PROTO);
}

// Métodos estáticos: send e receive
template <typename NIC>
int Protocol<NIC>::send(const Address &from, const Address &to, const void* data, unsigned int size) {
    NIC* nic = getNICInstance();
    Buffer* buf = nic->alloc(to.getPAddr(), PROTO, sizeof(Header) + size);
    if (!buf)
        return -1;
    Packet* pkt = reinterpret_cast<Packet*>(buf);
    pkt->length = sizeof(Header) + size;
    std::memcpy(pkt->data<char>(), data, size);
    return nic->send(buf);
}

template <typename NIC>
int Protocol<NIC>::receive(Buffer* buf, const Address &from, void* data, unsigned int size) {
    NIC* nic = getNICInstance();
    return nic->receive(buf, nullptr, nullptr, data, size);
}

// Gerenciamento de Observadores
template <typename NIC>
void Protocol<NIC>::attachObserver(Conditional_Data_Observer<Buffer, Port>* obs, const Address &address) {
    _observed.insert(obs, address);
}

template <typename NIC>
void Protocol<NIC>::detachObserver(Conditional_Data_Observer<Buffer, Port>* obs, const Address &address) {
    _observed.remove(obs, address);
}

// Método update (chamado quando um frame é recebido)
template <typename NIC>
void Protocol<NIC>::update(Buffer* buf) {
    if (!_observed.notify(0, buf))
        _nic->free(buf);
}

// Método para obter a instância da NIC (singleton dummy)
template <typename NIC>
NIC* Protocol<NIC>::getNICInstance() {
    static NIC instance;
    return &instance;
}

// Definição do membro estático _observed
template <typename NIC>
Conditionally_Data_Observed<typename Protocol<NIC>::Buffer, typename Protocol<NIC>::Port> Protocol<NIC>::_observed;
