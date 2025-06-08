#include "mac.hh"
#include <cassert>
#include <vector>
#include <random>
#include <iostream>

int main() {
    // 1) Gera uma chave aleatória
    MAC::Key key = MAC::generate_random_key();

    // 2) Cria uma “mensagem” de exemplo (payload aleatório)
    std::vector<std::byte> message;
    message.reserve(64);
    std::mt19937_64 rng{std::random_device{}()};
    std::uniform_int_distribution<int> dist(0, 255);
    for (int i = 0; i < 64; ++i) {
        message.push_back(static_cast<std::byte>(dist(rng)));
    }

    // 3) Calcula o tag para a mensagem original
    MAC::Tag tag = MAC::compute(key, message);

    // 4) Verifica que o tag válido passa
    assert(MAC::verify(key, message, tag) && "MAC deveria ser válido para mensagem intacta");

    // 5) Tenta verificar uma mensagem adulterada (payload corrompido)
    auto tampered_msg = message;
    tampered_msg[0] ^= std::byte{0xFF};  // inverte um byte
    assert(!MAC::verify(key, tampered_msg, tag) &&
           "MAC verify deve falhar se o payload for alterado");

    // 6) Tenta verificar com o tag adulterado (mesmo payload)
    auto tampered_tag = tag;
    tampered_tag[0] ^= std::byte{0xFF};
    assert(!MAC::verify(key, message, tampered_tag) &&
           "MAC verify deve falhar se o tag for alterado");

    std::cout << "[OK] Teste de validação de MAC passou\n";
    return 0;
}

// g++ -std=c++20 tests/src/e5/e5_validate_mac_test.cc -Iinclude/data_frames -Iinclude -o mac_test src/mac.cc src/utils.cc src/ethernet.cc -lcrypto