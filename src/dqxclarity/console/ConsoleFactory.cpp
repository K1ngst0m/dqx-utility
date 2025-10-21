#include "ConsoleFactory.hpp"
#ifdef _WIN32
#include "win/Console.hpp"
#else
#include "linux/Console.hpp"
#endif
#include "ConsoleNull.hpp"

namespace dqxclarity
{

ConsolePtr ConsoleFactory::Create(bool enable_console)
{
    if (!enable_console)
        return std::make_shared<ConsoleNull>();
#ifdef _WIN32
    return std::make_shared<Console>();
#else
    return std::make_shared<Console>();
#endif
}

} // namespace dqxclarity
