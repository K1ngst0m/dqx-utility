 #include "WindowAnimator.hpp"
 #include <algorithm>
 #include <cmath>

 namespace ui {

 void WindowAnimator::reset() {
     accum_ = 0.0f;
     phase_ = 0;
     last_scroll_max_y_ = 0.0f;
     scroll_animating_ = false;
     scroll_initialized_ = false;
 }

 const char* WindowAnimator::waitSuffix() const {
     switch (phase_ & 3) {
         case 0: return ".";
         case 1: return "..";
         case 2: return "...";
         default: return "..";
     }
 }

 void WindowAnimator::update(UIState& s, float dt, bool appended_since_last_frame, bool is_hovered) {
     // Advance wait-phase
     accum_ += dt;
     const float step = 0.35f;
     while (accum_ >= step) { accum_ -= step; phase_ = (phase_ + 1) & 3; }

     // Fade
     if (!s.fade_enabled) {
         s.current_alpha_multiplier = 1.0f;
     } else {
         if (s.last_activity_time == 0.0f) s.last_activity_time = static_cast<float>(ImGui::GetTime());
         if (appended_since_last_frame || is_hovered) {
             s.last_activity_time = static_cast<float>(ImGui::GetTime());
             s.current_alpha_multiplier = 1.0f;
         } else {
             float current_time = static_cast<float>(ImGui::GetTime());
             float time_since = current_time - s.last_activity_time;
             float fade_start = s.fade_timeout * 0.75f;
             float fade_duration = s.fade_timeout * 0.25f;
             if (time_since >= fade_start) {
                 float t = (fade_duration > 0.0f) ? (time_since - fade_start) / fade_duration : 1.0f;
                 t = std::clamp(t, 0.0f, 1.0f);
                 s.current_alpha_multiplier = 1.0f - (t * t);
             } else {
                 s.current_alpha_multiplier = 1.0f;
             }
         }
     }

     // Scroll to bottom
     if (s.auto_scroll_to_new) {
         const float curr_scroll = ImGui::GetScrollY();
         const float curr_max = ImGui::GetScrollMaxY();
         if (!scroll_initialized_) { last_scroll_max_y_ = curr_max; scroll_initialized_ = true; }
         const bool content_grew = (curr_max > last_scroll_max_y_ + 0.5f);
         const bool was_at_bottom = (last_scroll_max_y_ <= 0.5f) || ((last_scroll_max_y_ - curr_scroll) <= 2.0f);
         if (!scroll_animating_ && content_grew && was_at_bottom) scroll_animating_ = true;
         if (scroll_animating_) {
             const float target = curr_max;
             const float current = ImGui::GetScrollY();
             float delta = target - current;
             const float step_px = SCROLL_SPEED * dt;
             if (std::fabs(delta) <= step_px) { ImGui::SetScrollY(target); scroll_animating_ = false; }
             else { ImGui::SetScrollY(current + (delta > 0.0f ? step_px : -step_px)); }
         }
         last_scroll_max_y_ = curr_max;
     }
 }

 } // namespace ui
