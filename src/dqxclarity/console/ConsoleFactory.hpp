#pragma once

#include "IConsoleSink.hpp"
#include <memory>

namespace dqxclarity {

class ConsoleFactory {
public:
    static ConsolePtr Create(bool enable_console);
};

} // namespace dqxclarity