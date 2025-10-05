#include "Pattern.hpp"
#include <sstream>

namespace dqxclarity {

Pattern Pattern::FromString(const std::string& pattern_str) {
    Pattern pattern;
    std::istringstream iss(pattern_str);
    std::string token;

    while (iss >> token) {
        if (token == "??" || token == "." || token == "..") {
            pattern.bytes.push_back(0x00);
            pattern.mask.push_back(false);
        } else {
            try {
                uint8_t byte = static_cast<uint8_t>(std::stoi(token, nullptr, 16));
                pattern.bytes.push_back(byte);
                pattern.mask.push_back(true);
            } catch (...) {
                return Pattern();
            }
        }
    }

    return pattern;
}

Pattern Pattern::FromBytes(const uint8_t* data, size_t size) {
    Pattern pattern;
    pattern.bytes.assign(data, data + size);
    pattern.mask.assign(size, true);
    return pattern;
}

} // namespace dqxclarity
