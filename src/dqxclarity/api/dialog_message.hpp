#pragma once

#include <string>
#include <cstdint>

namespace dqxclarity {

struct DialogMessage {
  std::uint64_t seq = 0;
  std::string text;
  std::string lang;    // optional, may be empty
  std::string speaker; // optional, may be empty
};

} // namespace dqxclarity