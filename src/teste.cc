// test.cc
#include <iostream>
#include <cstring>
#include "ethernet.hh"  // Inclua o header com a definição da classe Ethernet

int main() {
    // Testa o construtor padrão de Ethernet::Address
    Ethernet::Address addrDefault;
    if (!addrDefault)
        std::cout << "addrDefault é nulo (inicializado com zeros)\n";
    else
        std::cout << "addrDefault possui valor diferente de zero\n";

    // Cria um endereço MAC com valores específicos
    unsigned char mac[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01};
    Ethernet::Address addr1(mac);
    if (addr1)
        std::cout << "addr1 foi definido corretamente\n";
    else
        std::cout << "addr1 não foi definido corretamente\n";

    // Testa o operador de igualdade
    Ethernet::Address addr2(mac);
    if (addr1 == addr2)
        std::cout << "addr1 e addr2 são iguais\n";
    else
        std::cout << "addr1 e addr2 são diferentes\n";

    // Cria um frame Ethernet e atribui valores
    Ethernet::Frame frame;
    frame.src = addr1;
    frame.dst = addrDefault;
    frame.prot = 0x0800;

    const char* payload = "Teste de frame Ethernet";
    size_t payloadSize = std::min((size_t)Ethernet::MTU, std::strlen(payload) + 1);
    std::memcpy(frame.data, payload, payloadSize);

    std::cout << "Frame criado:\n";
    std::cout << "  Origem: addr1\n";
    std::cout << "  Destino: addrDefault\n";
    std::cout << "  Protocolo: 0x" << std::hex << frame.prot << std::dec << "\n";
    std::cout << "  Payload: " << frame.data << "\n";

    return 0;
}
