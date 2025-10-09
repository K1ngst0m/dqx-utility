#pragma once

#include <imgui.h>

class DialogAnimator {
public:
    void reset();
    void advance(float dt);
    const char* suffix() const;
    
    static void updateFadeEffect(
        float& last_activity_time,
        float& current_alpha_multiplier,
        float fade_timeout,
        bool appended_since_last_frame,
        bool is_hovered
    );

    static void updateScrollAnimation(
        float& last_scroll_max_y,
        bool& scroll_animating,
        bool& scroll_initialized,
        float delta_time,
        bool auto_scroll_enabled
    );

private:
    float accum_ = 0.0f;
    int phase_ = 0;
    static constexpr float SCROLL_SPEED = 600.0f;
};
