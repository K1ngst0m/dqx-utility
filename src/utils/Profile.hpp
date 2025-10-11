#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

#ifdef DQX_ENABLE_PROFILING
#include <tracy/Tracy.hpp>
#endif

namespace profiling::detail
{
#ifdef DQX_ENABLE_PROFILING
inline constexpr std::uint16_t clampLength(std::size_t length) noexcept
{
    return length > static_cast<std::size_t>(0xFFFF)
        ? static_cast<std::uint16_t>(0xFFFF)
        : static_cast<std::uint16_t>(length);
}

inline std::string_view ToStringView(std::string_view name) noexcept { return name; }

inline std::string_view ToStringView(const std::string& name) noexcept { return std::string_view{name}; }

inline std::string_view ToStringView(const char* name) noexcept
{
    return name ? std::string_view{name} : std::string_view{};
}

inline void SetThreadName(const char* name) noexcept
{
    if (!name)
        return;
    tracy::SetThreadName(name);
}
#else
inline constexpr std::uint16_t clampLength(std::size_t) noexcept { return 0; }

inline std::string_view ToStringView(std::string_view name) noexcept { return name; }

inline std::string_view ToStringView(const std::string& name) noexcept { return std::string_view{name}; }

inline std::string_view ToStringView(const char*) noexcept { return std::string_view{}; }
inline void SetThreadName(const char*) noexcept {}
#endif
} // namespace profiling::detail

#ifdef DQX_ENABLE_PROFILING
#define PROFILE_SCOPE() ZoneScoped
#define PROFILE_SCOPE_FUNCTION() ZoneScopedN(__FUNCTION__)
#define PROFILE_SCOPE_CUSTOM(nameExpr)                                                                                 \
    ZoneScoped;                                                                                                        \
    if (auto __profiling_scope_name = ::profiling::detail::ToStringView(nameExpr); !__profiling_scope_name.empty())    \
    {                                                                                                                  \
        ZoneName(__profiling_scope_name.data(), ::profiling::detail::clampLength(__profiling_scope_name.size()));      \
    }
#define PROFILE_THREAD_NAME(nameExpr) ::profiling::detail::SetThreadName(nameExpr)
#define PROFILE_FRAME_MARK() FrameMark
#else
#define PROFILE_SCOPE() ((void)0)
#define PROFILE_SCOPE_FUNCTION() ((void)0)
#define PROFILE_SCOPE_CUSTOM(nameExpr) ((void)sizeof(nameExpr))
#define PROFILE_THREAD_NAME(nameExpr) ((void)0)
#define PROFILE_FRAME_MARK() ((void)0)
#endif
