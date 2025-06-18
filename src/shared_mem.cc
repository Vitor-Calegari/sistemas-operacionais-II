#include "shared_mem.hh"
#include <cstring>

// Implementação do construtor padrão da estrutura Address
SharedMem::Address::Address() {
  std::memset(mac, 0, sizeof(mac));
}

// Implementação do construtor que recebe um array de 6 bytes
SharedMem::Address::Address(const unsigned char m[6]) {
  std::memcpy(mac, m, sizeof(mac));
}

// Implementação do operador de igualdade
bool SharedMem::Address::operator==(const Address &other) const {
  return std::memcmp(mac, other.mac, sizeof(mac)) == 0;
}

// Implementação do operador de desigualdade
bool SharedMem::Address::operator!=(const Address &other) const {
  return !(*this == other);
}

bool SharedMem::Address::operator<(const Address &other) const {
  return std::memcmp(mac, other.mac, sizeof(mac)) < 0;
}

// Implementação da conversão para bool: retorna true se o endereço não for zero
SharedMem::Address::operator bool() const {
  return std::memcmp(mac, SharedMem::ZERO, sizeof(mac)) != 0;
}

// Implementação do construtor de Statistics
SharedMem::Statistics::Statistics()
    : tx_packets(0), tx_bytes(0), rx_packets(0), rx_bytes(0) {
}

const unsigned char SharedMem::BROADCAST_ADDRESS[6] = { 0xFF, 0xFF, 0xFF,
                                                       0xFF, 0xFF, 0xFF };
const unsigned char SharedMem::ZERO[6] = { 0, 0, 0, 0, 0, 0 };

void SharedMem::Frame::clear() {
  prot = 0;
  std::memset(_data, 0, MTU);
}
