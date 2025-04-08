#include "ethernet.hh"
#include <cstring>

// Implementação do construtor padrão da estrutura Address
Ethernet::Address::Address() {
  std::memset(mac, 0, sizeof(mac));
}

// Implementação do construtor que recebe um array de 6 bytes
Ethernet::Address::Address(const unsigned char m[6]) {
  std::memcpy(mac, m, sizeof(mac));
}

// Implementação do operador de igualdade
bool Ethernet::Address::operator==(const Address &other) const {
  return std::memcmp(mac, other.mac, sizeof(mac)) == 0;
}

// Implementação do operador de desigualdade
bool Ethernet::Address::operator!=(const Address &other) const {
  return !(*this == other);
}

// Implementação da conversão para bool: retorna true se o endereço não for zero
Ethernet::Address::operator bool() const {
  return std::memcmp(mac, Ethernet::ZERO, sizeof(mac)) != 0;
}

// Implementação do construtor de Statistics
Ethernet::Statistics::Statistics()
    : tx_packets(0), tx_bytes(0), rx_packets(0), rx_bytes(0) {
}

const unsigned char Ethernet::BROADCAST_ADDRESS[6] = { 0xFF, 0xFF, 0xFF,
                                                       0xFF, 0xFF, 0xFF };
const unsigned char Ethernet::ZERO[6] = { 0, 0, 0, 0, 0, 0 };

void Ethernet::Frame::clear() {
  src = Address();
  dst = Address();
  prot = 0;
  std::memset(data, 0, MTU);
}
