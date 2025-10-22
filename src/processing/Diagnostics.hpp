#pragma once

#include <atomic>
#include <cstddef>
#include <string>
#include <string_view>

namespace processing
{

class Diagnostics
{
public:
    static constexpr int kLogInstance = 1;

    static void SetVerbose(bool enabled) noexcept;
    [[nodiscard]] static bool IsVerbose() noexcept;

    static void SetMaxPreview(std::size_t bytes) noexcept;
    [[nodiscard]] static std::size_t MaxPreview() noexcept;

    [[nodiscard]] static std::string Preview(std::string_view text);

private:
    static void sanitize(std::string& text);
    static std::atomic<bool> verbose_;
    static std::atomic<std::size_t> max_preview_;
};

} // namespace processing
