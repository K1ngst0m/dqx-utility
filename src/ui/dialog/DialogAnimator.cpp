#include "DialogAnimator.hpp"
#include <algorithm>
#include <cmath>

void DialogAnimator::reset() {
    accum_ = 0.0f;
    phase_ = 0;
}

void DialogAnimator::advance(float dt) {
    const float step = 0.35f;
    accum_ += dt;
    while (accum_ >= step) {
        accum_ -= step;
        phase_ = (phase_ + 1) % 4;
    }
}

const char* DialogAnimator::suffix() const {
    switch (phase_ % 4) {
        case 0: return ".";
        case 1: return "..";
        case 2: return "...";
        default: return "..";
    }
}

void DialogAnimator::updateFadeEffect(
    float& last_activity_time,
    float& current_alpha_multiplier,
    float fade_timeout,
    bool appended_since_last_frame,
    bool is_hovered
)
{
    if (last_activity_time == 0.0f)
    {
        last_activity_time = static_cast<float>(ImGui::GetTime());
    }

    if (appended_since_last_frame || is_hovered)
    {
        last_activity_time = static_cast<float>(ImGui::GetTime());
        current_alpha_multiplier = 1.0f;
        return;
    }

    float current_time = static_cast<float>(ImGui::GetTime());
    float time_since_activity = current_time - last_activity_time;

    float fade_start = fade_timeout * 0.75f;
    float fade_duration = fade_timeout * 0.25f;

    if (time_since_activity >= fade_start)
    {
        float fade_progress = (time_since_activity - fade_start) / fade_duration;
        fade_progress = std::clamp(fade_progress, 0.0f, 1.0f);
        current_alpha_multiplier = 1.0f - (fade_progress * fade_progress);
    }
    else
    {
        current_alpha_multiplier = 1.0f;
    }
}

void DialogAnimator::updateScrollAnimation(
    float& last_scroll_max_y,
    bool& scroll_animating,
    bool& scroll_initialized,
    float delta_time,
    bool auto_scroll_enabled
)
{
    if (!auto_scroll_enabled) return;

    const float curr_scroll = ImGui::GetScrollY();
    const float curr_max = ImGui::GetScrollMaxY();

    if (!scroll_initialized)
    {
        last_scroll_max_y = curr_max;
        scroll_initialized = true;
    }

    const bool content_grew = (curr_max > last_scroll_max_y + 0.5f);
    const bool was_at_bottom = (last_scroll_max_y <= 0.5f) || ((last_scroll_max_y - curr_scroll) <= 2.0f);
    
    if (!scroll_animating && content_grew && was_at_bottom)
    {
        scroll_animating = true;
    }

    if (scroll_animating)
    {
        const float target = curr_max;
        const float current = ImGui::GetScrollY();
        float delta = target - current;
        const float step = SCROLL_SPEED * delta_time;

        if (std::fabs(delta) <= step)
        {
            ImGui::SetScrollY(target);
            scroll_animating = false;
        }
        else
        {
            ImGui::SetScrollY(current + (delta > 0.0f ? step : -step));
        }
    }

    last_scroll_max_y = curr_max;
}
