#ifndef MAC_HH
#define MAC_HH

#include <array>
#include <cstddef>
#include <vector>

namespace MAC {
constexpr int KEY_SIZE = 32;
using Key = std::array<std::byte, KEY_SIZE>;

std::vector<unsigned char> compute(const Key &key,
                                   const std::vector<unsigned char> &message);

bool verify(const Key &key, const std::vector<unsigned char> &message,
            const std::vector<unsigned char> &expected_tag);

Key generate_random_key();
} // namespace MAC

#endif
