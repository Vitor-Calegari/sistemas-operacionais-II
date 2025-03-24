#ifndef ETHERNET_HH
#define ETHERNET_HH

#include <cstring>
#include <cstddef>

// A classe Ethernet define os elementos básicos da camada de enlace
class Ethernet {
public:
    // Define o tamanho máximo de dados (payload) que um frame pode transportar
    static const unsigned int MTU = 1500;

    // Estrutura para representar um endereço MAC (6 bytes)
    struct Address {
        unsigned char mac[6];

        // Construtor padrão: inicializa o endereço com zeros
        Address() {
            std::memset(mac, 0, sizeof(mac));
        }

        // Construtor que inicializa o endereço a partir de um array de 6 bytes
        Address(const unsigned char m[6]) {
            std::memcpy(mac, m, sizeof(mac));
        }

        // Operador de igualdade: compara os 6 bytes do endereço MAC
        bool operator==(const Address &other) const {
            return (std::memcmp(mac, other.mac, sizeof(mac)) == 0);
        }

        // Operador de desigualdade: inverte o resultado da comparação
        bool operator!=(const Address &other) const {
            return !(*this == other);
        }

        // Conversão para bool: retorna true se o endereço não for nulo (todos zeros)
        explicit operator bool() const {
            static const unsigned char zero[6] = {0, 0, 0, 0, 0, 0};
            return (std::memcmp(mac, zero, sizeof(mac)) != 0);
        }
    };

    // Tipo que representa o EtherType (identifica o protocolo do frame) – 16 bits
    typedef unsigned short Protocol;

    // Estrutura que representa um frame Ethernet simplificado
    struct Frame {
        Address      dst;     // Endereço MAC de destino
        Address      src;     // Endereço MAC de origem
        Protocol     prot;    // Campo que indica qual protocolo está sendo utilizado (ex.: IPv4 = 0x0800)
        unsigned char data[MTU]; // Array de bytes contendo o payload (dados)
    } __attribute__((packed)); // Evita padding entre os campos da estrutura

    // Estrutura para armazenar estatísticas de transmissão e recepção de frames
    struct Statistics {
        unsigned int tx_packets; // Número de pacotes transmitidos
        unsigned int rx_packets; // Número de pacotes recebidos

        // Construtor que inicializa os contadores com zero
        Statistics() : tx_packets(0), rx_packets(0) {}
    };
};

#endif
