#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>

// DQX_PROFILING_LEVEL is set via CMake:
//   0 = Disabled (no profiling)
//   1 = Timer only (std::chrono + plog)
//   2 = Tracy + Timer (full profiling)

#ifndef DQX_PROFILING_LEVEL
#define DQX_PROFILING_LEVEL 0
#endif

#if DQX_PROFILING_LEVEL >= 2
#include <tracy/Tracy.hpp>
#endif

#if DQX_PROFILING_LEVEL >= 1
#include <mutex>
#include <plog/Log.h>
#include <plog/Appenders/RollingFileAppender.h>
#include <plog/Formatters/TxtFormatter.h>
#include <plog/Init.h>
#endif

namespace profiling
{

#if DQX_PROFILING_LEVEL >= 1
constexpr int kProfilingLogInstance = 2;
#endif

namespace detail
{

#if DQX_PROFILING_LEVEL >= 1
/**
 * @brief RAII scope timer for measuring and logging execution time
 *
 * Captures start time on construction and logs elapsed time on destruction.
 * Used for lightweight profiling with plog integration.
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
        PLOG_DEBUG_(profiling::kProfilingLogInstance) << "[PROFILE] " << name_ << " took " << duration.count() << " μs";
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

/**
 * @brief Accumulates frame timing statistics and logs periodically
 *
 * Tracks min/max/avg frame times across N frames and logs summary statistics
 * to avoid spamming the log with per-frame profiling data.
 * Only active at profiling level 1 (Tracy handles this at level 2).
 */
class FrameStatsAccumulator
{
public:
    explicit FrameStatsAccumulator(std::size_t log_interval = 60) noexcept
        : log_interval_(log_interval)
        , frame_count_(0)
        , min_frame_time_(std::numeric_limits<double>::max())
        , max_frame_time_(0.0)
        , total_frame_time_(0.0)
        , start_(std::chrono::high_resolution_clock::now())
    {
    }

    void recordFrame() noexcept
    {
        auto now = std::chrono::high_resolution_clock::now();
        auto frame_duration = std::chrono::duration_cast<std::chrono::microseconds>(now - start_);
        double frame_time_ms = frame_duration.count() / 1000.0;

        // Update statistics
        min_frame_time_ = std::min(min_frame_time_, frame_time_ms);
        max_frame_time_ = std::max(max_frame_time_, frame_time_ms);
        total_frame_time_ += frame_time_ms;
        ++frame_count_;

        // Log statistics periodically
        if (frame_count_ >= log_interval_)
        {
            double avg_frame_time = total_frame_time_ / frame_count_;
            double fps = frame_count_ > 0 ? 1000.0 / avg_frame_time : 0.0;

            PLOG_DEBUG_(profiling::kProfilingLogInstance)
                << "[PROFILE] Frame stats (" << frame_count_ << " frames): "
                << "avg=" << static_cast<int>(avg_frame_time * 1000) << "μs, "
                << "min=" << static_cast<int>(min_frame_time_ * 1000) << "μs, "
                << "max=" << static_cast<int>(max_frame_time_ * 1000) << "μs, "
                << "fps=" << static_cast<int>(fps + 0.5);

            // Reset for next interval
            frame_count_ = 0;
            min_frame_time_ = std::numeric_limits<double>::max();
            max_frame_time_ = 0.0;
            total_frame_time_ = 0.0;
        }

        start_ = now;
    }

    // Non-copyable, non-movable
    FrameStatsAccumulator(const FrameStatsAccumulator&) = delete;
    FrameStatsAccumulator& operator=(const FrameStatsAccumulator&) = delete;
    FrameStatsAccumulator(FrameStatsAccumulator&&) = delete;
    FrameStatsAccumulator& operator=(FrameStatsAccumulator&&) = delete;

private:
    std::size_t log_interval_;
    std::size_t frame_count_;
    double min_frame_time_;
    double max_frame_time_;
    double total_frame_time_;
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

inline void SetThreadName(const char* name) noexcept
{
    if (!name)
        return;
    tracy::SetThreadName(name);
}
#endif

} // namespace detail

} // namespace profiling

// Profiling macros for different levels

#if DQX_PROFILING_LEVEL == 0
// Level 0: Disabled - No profiling overhead
#define PROFILE_SCOPE() ((void)0)
#define PROFILE_SCOPE_FUNCTION() ((void)0)
#define PROFILE_SCOPE_CUSTOM(nameExpr) ((void)sizeof(nameExpr))
#define PROFILE_SCOPE_FRAME() ((void)0)
#define PROFILE_THREAD_NAME(nameExpr) ((void)0)
#define PROFILE_FRAME_MARK() ((void)0)
#define PROFILE_FRAME_STATS(accumulator) ((void)0)

#elif DQX_PROFILING_LEVEL == 1
// Level 1: Timer only - Lightweight profiling with plog
#define PROFILE_SCOPE() ::profiling::detail::ScopeTimer __profiling_timer(__FUNCTION__)
#define PROFILE_SCOPE_FUNCTION() ::profiling::detail::ScopeTimer __profiling_timer(__FUNCTION__)
#define PROFILE_SCOPE_CUSTOM(nameExpr) ::profiling::detail::ScopeTimer __profiling_timer(nameExpr)
#define PROFILE_SCOPE_FRAME() ((void)0)
#define PROFILE_THREAD_NAME(nameExpr) ((void)0)
#define PROFILE_FRAME_MARK() ((void)0)
#define PROFILE_FRAME_STATS(accumulator) (accumulator).recordFrame()

#elif DQX_PROFILING_LEVEL >= 2
// Level 2: Tracy + Timer - Full profiling with real-time visualization and logging
#define PROFILE_SCOPE() \
    ZoneScoped;         \
    ::profiling::detail::ScopeTimer __profiling_timer(__FUNCTION__)

#define PROFILE_SCOPE_FUNCTION() \
    ZoneScopedN(__FUNCTION__);   \
    ::profiling::detail::ScopeTimer __profiling_timer(__FUNCTION__)

#define PROFILE_SCOPE_CUSTOM(nameExpr)                                                                              \
    ZoneScoped;                                                                                                     \
    ::profiling::detail::ScopeTimer __profiling_timer(nameExpr);                                                    \
    if (auto __profiling_scope_name = ::profiling::detail::ToStringView(nameExpr); !__profiling_scope_name.empty()) \
    {                                                                                                               \
        ZoneName(__profiling_scope_name.data(), ::profiling::detail::clampLength(__profiling_scope_name.size()));   \
    }

#define PROFILE_SCOPE_FRAME() ZoneScopedN(__FUNCTION__)

#define PROFILE_THREAD_NAME(nameExpr) ::profiling::detail::SetThreadName(nameExpr)
#define PROFILE_FRAME_MARK() FrameMark
#define PROFILE_FRAME_STATS(accumulator) ((void)0)

#endif
