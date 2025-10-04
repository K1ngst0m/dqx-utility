#pragma once

#include <cstdint>
#include <cassert>

namespace dqxclarity {

inline uint32_t ToImm32(uintptr_t p) {
    assert(p <= 0xFFFFFFFFu);
    return static_cast<uint32_t>(p);
}

inline uint32_t Rel32From(uintptr_t from_e9, uintptr_t dest) {
    auto diff = static_cast<int64_t>(dest) - static_cast<int64_t>(from_e9 + 5);
    return static_cast<uint32_t>(diff);
}

} // namespace dqxclarity