#include "mac.hh"
#include "utils.hh"

#include <array>
#include <cstddef>
#include <cstring>
#include <openssl/core_names.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <stdexcept>

namespace MAC {

MAC::Tag compute(const MAC::Key &key, const std::vector<std::byte> &message) {
  if (key.size() != MAC::KEY_SIZE) {
    throw std::invalid_argument("Poly1305 key must be exactly 32 bytes.");
  }

  EVP_MAC *mac = EVP_MAC_fetch(nullptr, "POLY1305", nullptr);
  if (!mac) {
    throw std::runtime_error("Failed to fetch POLY1305 MAC.");
  }

  EVP_MAC_CTX *ctx = EVP_MAC_CTX_new(mac);
  if (!ctx) {
    EVP_MAC_free(mac);
    throw std::runtime_error("Failed to create EVP_MAC_CTX.");
  }

  if (EVP_MAC_init(ctx, reinterpret_cast<const unsigned char *>(key.data()),
                   key.size(), nullptr) != 1) {
    EVP_MAC_CTX_free(ctx);
    EVP_MAC_free(mac);
    throw std::runtime_error("EVP_MAC_init failed.");
  }

  if (EVP_MAC_update(ctx,
                     reinterpret_cast<const unsigned char *>(message.data()),
                     message.size()) != 1) {
    EVP_MAC_CTX_free(ctx);
    EVP_MAC_free(mac);
    throw std::runtime_error("EVP_MAC_update failed.");
  }

  Tag tag{};
  size_t tag_len = 0;
  if (EVP_MAC_final(ctx, reinterpret_cast<unsigned char *>(tag.data()),
                    &tag_len, tag.size()) != 1) {
    EVP_MAC_CTX_free(ctx);
    EVP_MAC_free(mac);
    throw std::runtime_error("EVP_MAC_final failed.");
  }

  EVP_MAC_CTX_free(ctx);
  EVP_MAC_free(mac);

  return tag;
}

bool verify(const MAC::Key &key, const std::vector<std::byte> &message,
            const MAC::Tag &expected_tag) {
  if (key.size() != MAC::KEY_SIZE) {
    throw std::invalid_argument("Poly1305 key must be exactly 32 bytes.");
  }
  if (expected_tag.size() != MAC::TAG_SIZE) {
    throw std::invalid_argument("Poly1305 tag must be exactly 16 bytes.");
  }

  auto computed_tag = MAC::compute(key, message);
  return CRYPTO_memcmp(computed_tag.data(), expected_tag.data(),
                       MAC::TAG_SIZE) == 0;
}

MAC::Key generate_random_key() {
  MAC::Key key{};

  for (int i = 0; i < MAC::KEY_SIZE; ++i) {
    key[i] = std::byte(randint(0, 255));
  }

  return key;
}

} // namespace MAC
