#include "DialogWaitAnimation.hpp"

void DialogWaitAnimation::reset() {
    accum_ = 0.0f;
    phase_ = 0;
}

void DialogWaitAnimation::advance(float dt) {
    const float step = 0.35f;
    accum_ += dt;
    while (accum_ >= step) {
        accum_ -= step;
        phase_ = (phase_ + 1) % 4;
    }
}

const char* DialogWaitAnimation::suffix() const {
    switch (phase_ % 4) {
        case 0: return ".";
        case 1: return "..";
        case 2: return "...";
        default: return "..";
    }
}
