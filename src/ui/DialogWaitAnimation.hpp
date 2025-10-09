#pragma once

class DialogWaitAnimation {
public:
    void reset();
    void advance(float dt);
    const char* suffix() const;
private:
    float accum_ = 0.0f;
    int phase_ = 0;
};
