#ifndef MAC_HH
#define MAC_HH

#include <array>
#include <cstddef>
#include <vector>

namespace MAC {
constexpr int KEY_SIZE = 32;
using Key = std::array<std::byte, KEY_SIZE>;

constexpr int TAG_SIZE = 16;
using Tag = std::array<std::byte, TAG_SIZE>;

Tag compute(const Key &key, const std::vector<std::byte> &message);

bool verify(const Key &key, const std::vector<std::byte> &message,
            const Tag &expected_tag);

Key generate_random_key();
} // namespace MAC

#endif
