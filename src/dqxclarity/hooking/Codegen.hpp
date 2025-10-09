#pragma once

#include <cstdint>
#include <cassert>
#include <vector>

namespace dqxclarity {

inline uint32_t ToImm32(uintptr_t p) {
    assert(p <= 0xFFFFFFFFu);
    return static_cast<uint32_t>(p);
}

inline uint32_t Rel32From(uintptr_t from_e9, uintptr_t dest) {
    auto diff = static_cast<int64_t>(dest) - static_cast<int64_t>(from_e9 + 5);
    return static_cast<uint32_t>(diff);
}

class X86CodeBuilder {
public:
    enum class Register {
        EAX, EBX, ECX, EDX, ESI, EDI, EBP, ESP
    };

    void movToMem(Register reg, uint32_t addr);
    void movFromMem(Register reg, uint32_t addr);
    void setByteAtMem(uint32_t addr, uint8_t value);
    void appendBytes(const std::vector<uint8_t>& bytes);
    void jmpRel32(uintptr_t from, uintptr_t dest);
    
    const std::vector<uint8_t>& code() const { return code_; }
    std::vector<uint8_t> finalize() { return std::move(code_); }

private:
    std::vector<uint8_t> code_;
    
    void emitU32(uint32_t value);
};

} // namespace dqxclarity
