#ifndef PROTOCOL_HH
#define PROTOCOL_HH

#include <cstring>
#include "nic.hh"
#include "ethernet.hh"
#include "observer.hh"    // Declara Conditional_Data_Observer<>
#include "observed.hh"    // Declara Conditionally_Data_Observed<>

// Template Protocol – toda a implementação fica no header
template <typename NIC>
class Protocol : private typename NIC::Observer {
public:
    // Número de protocolo definido via Traits (por exemplo, 0x0800)
    inline static const typename NIC::Protocol_Number PROTO = Traits<Protocol>::ETHERNET_PROTOCOL_NUMBER;

    typedef typename NIC::Buffer        Buffer;
    typedef typename NIC::Address       Physical_Address;
    typedef unsigned short              Port;

    // Endereço do protocolo: combinação de endereço físico e porta
    class Address {
    public:
        enum Null { NONE };
        Address() : _paddr(0), _port(0) { }
        Address(const Null &n) : _paddr(0), _port(0) { }
        Address(Physical_Address paddr, Port port) : _paddr(paddr), _port(port) { }
        operator bool() const { return (_paddr != 0 || _port != 0); }
        bool operator==(const Address &other) const { return (_paddr == other._paddr) && (_port == other._port); }
        Physical_Address getPAddr() const { return _paddr; }
        Port getPort() const { return _port; }
    private:
        Physical_Address _paddr;
        Port _port;
    };

    // Cabeçalho do pacote (pode ser estendido com timestamp, tipo, etc.)
    class Header {
    public:
        Header() : length(0) { }
        unsigned int length;
    };

    // MTU disponível para o payload: espaço total do buffer menos o tamanho do Header
    inline static const unsigned int MTU = NIC::MTU - sizeof(Header);
    typedef unsigned char Data[MTU];

    // Pacote: Header seguido do payload
    class Packet : public Header {
    public:
        Packet() {
            std::memset(_data, 0, sizeof(_data));
        }
        Header* header() { return this; }
        template <typename T>
        T* data() { return reinterpret_cast<T*>(&_data); }
    private:
        Data _data;
    } __attribute__((packed));

    // Construtor: associa o protocolo à NIC e registra-se como observador do protocolo PROTO
    Protocol(NIC* nic) : _nic(nic) {
        _nic->attach(this, PROTO);
    }

    // Destrutor: remove o protocolo da NIC
    ~Protocol() {
        _nic->detach(this, PROTO);
    }

    // Envia uma mensagem: aloca buffer, monta o pacote e delega à NIC
    static int send(const Address &from, const Address &to, const void* data, unsigned int size) {
        NIC* nic = getNICInstance();
        Buffer* buf = nic->alloc(to.getPAddr(), PROTO, sizeof(Header) + size);
        if (!buf)
            return -1;
        Packet* pkt = reinterpret_cast<Packet*>(buf);
        pkt->length = sizeof(Header) + size;
        std::memcpy(pkt->data<char>(), data, size);
        return nic->send(buf);
    }

    // Recebe uma mensagem: extrai o payload do buffer recebido
    static int receive(Buffer* buf, const Address &from, void* data, unsigned int size) {
        NIC* nic = getNICInstance();
        return nic->receive(buf, nullptr, nullptr, data, size);
    }

    // Gerencia observadores para recepção assíncrona
    static void attachObserver(Conditional_Data_Observer<Buffer, Port>* obs, const Address &address) {
        _observed.insert(obs, address);
    }
    static void detachObserver(Conditional_Data_Observer<Buffer, Port>* obs, const Address &address) {
        _observed.remove(obs, address);
    }

private:
    // Método update: chamado pela NIC quando um frame é recebido
    void update(Buffer* buf) {
        if (!_observed.notify(0, buf))
            _nic->free(buf);
    }

    // Para simplificação, assume-se uma instância singleton da NIC
    static NIC* getNICInstance() {
        static NIC instance;
        return &instance;
    }

    NIC* _nic;
    inline static Conditionally_Data_Observed<Buffer, Port> _observed;
};

#endif // PROTOCOL_HH
