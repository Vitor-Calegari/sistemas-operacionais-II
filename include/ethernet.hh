#ifndef ETHERNET_HH
#define ETHERNET_HH

#include <cstddef>
#include <cstring>

// Define a classe Ethernet e seus componentes
class Ethernet {
public:
    // Tamanho máximo do payload
    static const unsigned int MTU = 1500;

    // Estrutura que representa um endereço MAC (6 bytes)
    struct Address {
        unsigned char mac[6];

        // Declaração dos construtores e operadores
        Address();
        Address(const unsigned char m[6]);
        bool operator==(const Address &other) const;
        bool operator!=(const Address &other) const;
        explicit operator bool() const;
    };

    // Tipo que representa o protocolo (16 bits)
    typedef unsigned short Protocol;

    // Estrutura que representa um frame Ethernet
    struct Frame {
        Address dst;           // Endereço de destino
        Address src;           // Endereço de origem
        Protocol prot;         // Protocolo (por exemplo, 0x0800)
        unsigned char data[MTU];  // Payload
    } __attribute__((packed));

    // Estrutura para estatísticas de transmissão/recepção
    struct Statistics {
        unsigned int tx_packets;
        unsigned int rx_packets;
        Statistics();
    };
};

#endif
