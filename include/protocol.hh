#ifndef PROTOCOL_HH
#define PROTOCOL_HH

#include <cstring>
#include "nic.hh"
#include "ethernet.hh"
#include "conditional_data_observer.hh"
#include "conditionally_data_observed.hh"

template <typename NIC>
class Protocol : private NIC::Observer {
public:
    inline static const typename NIC::Protocol_Number PROTO = htons(ETH_P_802_EX1); //Traits<Protocol>::ETHERNET_PROTOCOL_NUMBER;

    typedef typename NIC::BufferNIC        Buffer;
    typedef typename NIC::Address       Physical_Address;
    typedef unsigned short              Port;

    typedef Conditional_Data_Observer<Buffer, Port> Observer;
    typedef Conditionally_Data_Observed<Buffer, Port> Observed;

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

    // Cabeçalho do pacote do protocolo (pode ser estendido com timestamp, tipo, etc.)
    class Header {
    public:
        Header() : length(0), origin(Address()){}
        Address origin;
        unsigned int length;
    };

    // MTU disponível para o payload: espaço total do buffer menos o tamanho do Header
    inline static const unsigned int MTU = NIC::MTU - sizeof(Header);
    typedef unsigned char Data[MTU];

    // Pacote do protocolo: composto pelo Header e o Payload
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

    static Protocol* getInstance(NIC* nic) {
        if (!_instance) {
            _instance = new Protocol(nic);
        }
        return _instance;
    }

    Protocol(Protocol const&)               = delete;
    void operator=(Protocol const&)  = delete;

protected:
    // Construtor: associa o protocolo à NIC e registra-se como observador do protocolo PROTO
    Protocol(NIC* nic) : _nic(nic) {
        _nic->attach(this, PROTO);
    }
public:
    // Destrutor: remove o protocolo da NIC
    ~Protocol() {
        _nic->detach(this, PROTO);
    }

    // Envia uma mensagem:
    // Aloca um buffer (que é um Ethernet::Frame), interpreta o payload (após o cabeçalho Ethernet)
    // como um Packet, monta o pacote e delega o envio à NIC.
    int send(const Address &from, const Address &to, const void* data, unsigned int size) {
        Buffer* buf = _nic->alloc(to.getPAddr(), PROTO, sizeof(Header) + size);
        if (!buf)
            return -1;
        // Em vez de interpretar o frame inteiro como um Packet, acessa o payload.
        // Presume-se que o Ethernet::Frame possui um cabeçalho fixo de tamanho Ethernet::HEADER_SIZE.
        unsigned char* payloadPtr = reinterpret_cast<unsigned char*>(buf) + Ethernet::HEADER_SIZE;
        //buf->data()->data;
        Packet* pkt = reinterpret_cast<Packet*>(payloadPtr);
        pkt->header()->origin = from;
        buf->setSize(buf->size() + sizeof(Header) + size);
        std::memcpy(pkt->template data<char>(), data, size);
        return _nic->send(buf);
    }

    // Recebe uma mensagem:
    // Aqui, também interpretamos o payload do Ethernet::Frame como um Packet.
    int receive(Buffer* buf, Address &from, void* data, unsigned int size) {
        Address to = Address();
        unsigned int s= getNIC()->receive(buf, &from.paddr, &to.paddr, data, size);
        getNIC()->free(buf);
        return s;
    }

private:
    // Método update: chamado pela NIC quando um frame é recebido.
    // Agora com 3 parâmetros: o Observed, o protocolo e o buffer.
    void update(typename NIC::Observed* obs, typename NIC::Protocol_Number prot, Buffer* buf) {
        if (!_nic->notify(prot, buf))
            _nic->free(buf);
    }

    // Retorna a NIC associada (diferente do singleton, retorna o _nic da instância)
    NIC* getNIC() {
        return _nic;
    }

    static Protocol* _instance;
    NIC* _nic;
};
template <typename NIC>
Protocol<NIC>* Protocol<NIC>::_instance = nullptr;
#endif // PROTOCOL_HH
