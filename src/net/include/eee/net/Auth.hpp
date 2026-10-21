#pragma once

#include "eee/core/Types.hpp"

#include <string_view>

namespace eee::net {

// FNV-1a 64-bit. Used to avoid sending the password verbatim; this is not a
// password hash for storage and does not provide encryption or replay
// protection. TLS/VPN is still required for hostile networks.
inline core::u64 hashSecret(std::string_view secret) {
    core::u64 hash = 1469598103934665603ull;
    for (const char c : secret) {
        hash ^= static_cast<core::u8>(c);
        hash *= 1099511628211ull;
    }
    return hash;
}

inline bool constantTimeEqual(core::u64 a, core::u64 b) {
    core::u64 diff = a ^ b;
    core::u64 result = 0;
    for (int i = 0; i < 64; ++i) {
        result |= (diff >> i) & 1ull;
    }
    return result == 0;
}

} // namespace eee::net
