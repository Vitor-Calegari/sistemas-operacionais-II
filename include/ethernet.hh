#ifndef ETHERNET_HH
#define ETHERNET_HH

#include <cstddef>
#include <cstring>

// Define a classe Ethernet e seus componentes
class Ethernet {
public:
    // Maximum Transmission Unit (MTU) padrão para Ethernet v2.
    // Define o tamanho máximo do payload (dados da camada superior).
    static const int MTU = 1500;
    // Tamanho do cabeçalho Ethernet (Dest MAC + Src MAC + EtherType)
    static const int HEADER_SIZE = 14;
    // Tamanho máximo quadro desconsiderando FCS
    const unsigned int MAX_FRAME_SIZE_NO_FCS = HEADER_SIZE + MTU;
    static const int MIN_FRAME_SIZE = 64;
    static const unsigned char BROADCAST_ADDRESS[6];
    static const unsigned char ZERO[6];
    // Estrutura que representa um endereço MAC (6 bytes)
    struct Address {
        unsigned char mac[6];

        // Construtor padrão: Inicializa com endereço zero (00:00:00:00:00:00).
        Address();
        // Construtor que recebe um array de 6 bytes.
        Address(const unsigned char m[6]);
        // Operador de igualdade: Compara dois endereços MAC.
        bool operator==(const Address &other) const;
        // Operador de desigualdade.
        bool operator!=(const Address &other) const;
        // Operador de conversão para bool: Retorna true se o endereço não for zero.
        explicit operator bool() const;
    };

    // Tipo que representa o protocolo (16 bits)
    typedef unsigned short Protocol;

    // Estrutura que representa um frame Ethernet
    struct Frame {
        Address dst;           // Endereço MAC de destino (6 bytes)
        Address src;           // Endereço MAC de origem (6 bytes)
        Protocol prot;         // EtherType (Protocolo) (2 bytes)
        unsigned char data[MTU];  // Payload (dados da camada superior) - até 1500 bytes
        void clear();
    } __attribute__((packed));

    // Estrutura para estatísticas de transmissão/recepção
    struct Statistics {
        unsigned long long tx_packets;
        unsigned long long tx_bytes;
        unsigned long long rx_packets;
        unsigned long long rx_bytes;
        Statistics();
    };
};

#endif
