#ifndef PROTOCOL_HH
#define PROTOCOL_HH

#include <cstring>
#include "nic.hh"
#include "ethernet.hh"
#include "observer.hh"    // Declaração de Conditional_Data_Observer<>
#include "observed.hh"    // Declaração de Conditionally_Data_Observed<>

// Template Protocol
template <typename NIC>
class Protocol : private typename NIC::Observer {
public:
    // Número de protocolo definido via Traits (ex.: 0x0800)
    static const typename NIC::Protocol_Number PROTO;

    typedef typename NIC::Buffer        Buffer;
    typedef typename NIC::Address       Physical_Address;
    typedef unsigned short              Port;

    // Endereço do protocolo: combinação de endereço físico e porta
    class Address {
    public:
        enum Null { NONE };
        Address();
        Address(const Null &n);
        Address(Physical_Address paddr, Port port);
        operator bool() const;
        bool operator==(const Address &other) const;
        Physical_Address getPAddr() const;
        Port getPort() const;
    private:
        Physical_Address _paddr;
        Port _port;
    };

    // Cabeçalho dos pacotes (pode ser estendido com timestamp, tipo etc.)
    class Header {
    public:
        Header();
        unsigned int length;
    };

    // MTU disponível para payload (tamanho total da buffer menos o header)
    int iphdrlen;
    iphdrlen = ip->ihl*4;
    static const unsigned int MTU = NIC::MTU - iphdrlen;
    typedef unsigned char Data[MTU];

    // Pacote: header seguido do payload
    class Packet : public Header {
    public:
        Packet();
        Header* header();
        template <typename T>
        T* data();
    private:
        Data _data;
    } __attribute__((packed));

    // Construtor/destrutor: associa o protocolo à NIC e registra/desregistra o observador
    Protocol(NIC* nic);
    ~Protocol();

    // Envio e recepção de mensagens
    static int send(const Address &from, const Address &to, const void* data, unsigned int size);
    static int receive(Buffer* buf, const Address &from, void* data, unsigned int size);

    // Gerenciamento de observadores (para recepção assíncrona)
    static void attachObserver(Conditional_Data_Observer<Buffer, Port>* obs, const Address &address);
    static void detachObserver(Conditional_Data_Observer<Buffer, Port>* obs, const Address &address);

private:
    // Método chamado quando um frame é recebido pela NIC
    void update(Buffer* buf);
    // Obtém a instância singleton da NIC (exemplo)
    static NIC* getNICInstance();

    NIC* _nic;
    static Conditionally_Data_Observed<Buffer, Port> _observed;
};

#include "protocol.cc" // Inclusão da implementação para templates
#endif // PROTOCOL_HH
