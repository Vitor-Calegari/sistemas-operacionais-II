#include <iostream>
#include <fstream>
#include <cstring>
#include <algorithm>
#include "ethernet.hh"

int main() {
    std::ofstream outfile("output.txt");
    if (!outfile) {
        std::cerr << "Erro ao abrir output.txt\n";
        return 1;
    }
    
    Ethernet::Address addrDefault;
    outfile << (!addrDefault ? "addrDefault é nulo\n" : "addrDefault não é nulo\n");

    unsigned char mac[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01};
    Ethernet::Address addr1(mac);
    outfile << (addr1 ? "addr1 definido corretamente\n" : "addr1 não definido\n");

    Ethernet::Address addr2(mac);
    outfile << (addr1 == addr2 ? "addr1 e addr2 são iguais\n" : "addr1 e addr2 são diferentes\n");

    Ethernet::Frame frame;
    frame.src = addr1;
    frame.dst = addrDefault;
    frame.prot = 0x0800;
    
    const char* payload = "Teste de frame Ethernet";
    size_t payloadSize = std::min((size_t)Ethernet::MTU, std::strlen(payload) + 1);
    std::memcpy(frame.data, payload, payloadSize);

    outfile << "Frame criado:\n";
    outfile << "  Origem: addr1\n";
    outfile << "  Destino: addrDefault\n";
    outfile << "  Protocolo: 0x" << std::hex << frame.prot << std::dec << "\n";
    outfile << "  Payload: " << frame.data << "\n";

    outfile.close();
    return 0;
}


// Compile g++ -std=c++11 -Wall -I../include -o teste ethernet.cc test.cc