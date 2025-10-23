#pragma once

#include <chrono>
#include <sstream>
#include <string_view>

// DQX_PROFILING_LEVEL is set via CMake:
//   0 = Disabled (no profiling)
//   1 = Timer only (std::chrono + dqxclarity::Logger)
//   2 = Tracy + Timer (full profiling)

#ifndef DQX_PROFILING_LEVEL
#define DQX_PROFILING_LEVEL 0
#endif

#if DQX_PROFILING_LEVEL >= 2
#include <tracy/Tracy.hpp>
#endif

#if DQX_PROFILING_LEVEL >= 1
#include "../api/dqxclarity.hpp" // For Logger struct definition
#endif

namespace dqxclarity::profiling
{

#if DQX_PROFILING_LEVEL >= 1
/// Global logger pointer for profiling output (set by Engine::initialize)
inline Logger* g_profiling_logger = nullptr;

/**
 * @brief Set the logger for profiling output
 *
 * Should be called during dqxclarity::Engine::initialize() to route
 * profiling logs through the application's logging system.
 *
 * @param logger Pointer to Logger struct with callback functions
 */
inline void SetProfilingLogger(Logger* logger) noexcept { g_profiling_logger = logger; }

/**
 * @brief RAII scope timer for measuring and logging execution time
 *
 * Captures start time on construction and logs elapsed time on destruction
 * using dqxclarity's Logger callbacks. Thread-safe and exception-safe.
 */
class ScopeTimer
{
public:
    explicit ScopeTimer(std::string_view name) noexcept
        : name_(name)
        , start_(std::chrono::high_resolution_clock::now())
    {
    }

    ~ScopeTimer() noexcept
    {
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start_);

        // Only log if logger is set and has an info callback
        if (g_profiling_logger && g_profiling_logger->info)
        {
            std::ostringstream oss;
            oss << "[PROFILE] " << name_ << " took " << duration.count() << " μs";
            g_profiling_logger->info(oss.str());
        }
    }

    // Non-copyable, non-movable
    ScopeTimer(const ScopeTimer&) = delete;
    ScopeTimer& operator=(const ScopeTimer&) = delete;
    ScopeTimer(ScopeTimer&&) = delete;
    ScopeTimer& operator=(ScopeTimer&&) = delete;

private:
    std::string_view name_;
    std::chrono::time_point<std::chrono::high_resolution_clock> start_;
};
#endif

#if DQX_PROFILING_LEVEL >= 2
// Helper functions for Tracy integration
inline constexpr std::uint16_t clampLength(std::size_t length) noexcept
{
    return length > static_cast<std::size_t>(0xFFFF) ? static_cast<std::uint16_t>(0xFFFF) :
                                                       static_cast<std::uint16_t>(length);
}

inline std::string_view ToStringView(std::string_view name) noexcept { return name; }

inline std::string_view ToStringView(const std::string& name) noexcept { return std::string_view{ name }; }

inline std::string_view ToStringView(const char* name) noexcept
{
    return name ? std::string_view{ name } : std::string_view{};
}
#endif

} // namespace dqxclarity::profiling

// Profiling macros for different levels

#if DQX_PROFILING_LEVEL == 0
// Level 0: Disabled - No profiling overhead
#define PROFILE_SCOPE() ((void)0)
#define PROFILE_SCOPE_FUNCTION() ((void)0)
#define PROFILE_SCOPE_CUSTOM(nameExpr) ((void)sizeof(nameExpr))
#define PROFILE_THREAD_NAME(nameExpr) ((void)0)
#define PROFILE_FRAME_MARK() ((void)0)

#elif DQX_PROFILING_LEVEL == 1
// Level 1: Timer only - Lightweight profiling with dqxclarity::Logger callbacks
#define PROFILE_SCOPE() ::dqxclarity::profiling::ScopeTimer __profiling_timer(__FUNCTION__)
#define PROFILE_SCOPE_FUNCTION() ::dqxclarity::profiling::ScopeTimer __profiling_timer(__FUNCTION__)
#define PROFILE_SCOPE_CUSTOM(nameExpr) ::dqxclarity::profiling::ScopeTimer __profiling_timer(nameExpr)
#define PROFILE_THREAD_NAME(nameExpr) ((void)0)
#define PROFILE_FRAME_MARK() ((void)0)

#elif DQX_PROFILING_LEVEL >= 2
// Level 2: Tracy + Timer - Full profiling with real-time visualization and logging
#define PROFILE_SCOPE() \
    ZoneScoped;         \
    ::dqxclarity::profiling::ScopeTimer __profiling_timer(__FUNCTION__)

#define PROFILE_SCOPE_FUNCTION() \
    ZoneScopedN(__FUNCTION__);   \
    ::dqxclarity::profiling::ScopeTimer __profiling_timer(__FUNCTION__)

#define PROFILE_SCOPE_CUSTOM(nameExpr)                                                                                \
    ZoneScoped;                                                                                                       \
    ::dqxclarity::profiling::ScopeTimer __profiling_timer(nameExpr);                                                  \
    if (auto __profiling_scope_name = ::dqxclarity::profiling::ToStringView(nameExpr);                                \
        !__profiling_scope_name.empty())                                                                              \
    {                                                                                                                 \
        ZoneName(__profiling_scope_name.data(), ::dqxclarity::profiling::clampLength(__profiling_scope_name.size())); \
    }

#define PROFILE_THREAD_NAME(nameExpr) tracy::SetThreadName(nameExpr)
#define PROFILE_FRAME_MARK() FrameMark

#endif
