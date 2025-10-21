#pragma once

#include <imgui.h>
#include "../state/UIState.hpp"

namespace ui
{

class WindowAnimator
{
public:
    void reset();
    void update(UIState& s, float dt, bool appended_since_last_frame, bool is_hovered);
    const char* waitSuffix() const;

private:
    // wait animation
    float accum_ = 0.0f;
    int phase_ = 0;
    // scroll animation state
    float last_scroll_max_y_ = 0.0f;
    bool scroll_animating_ = false;
    bool scroll_initialized_ = false;

    static constexpr float SCROLL_SPEED = 800.0f;
};

} // namespace ui
