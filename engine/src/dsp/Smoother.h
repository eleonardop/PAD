// PAD Engine — suavização de parâmetros (evita "zipper noise" e cliques ao mover controles).
#pragma once

#include "DspMath.h"

namespace pad {

// Suavizador exponencial de 1 polo. O tempo informado é a constante de tempo (63%).
class Smoother {
public:
    void setTime(float seconds, float rate) {
        coef_ = seconds <= 0.0f ? 0.0f : std::exp(-1.0f / (seconds * rate));
    }
    void setTarget(float t) { target_ = t; }
    void snap(float v) { target_ = value_ = v; }
    float target() const { return target_; }
    float value() const { return value_; }
    inline float tick() {
        value_ = target_ + coef_ * (value_ - target_);
        if (std::fabs(value_ - target_) < 1e-7f) value_ = target_;
        return value_;
    }
private:
    float coef_ = 0.0f, value_ = 0.0f, target_ = 0.0f;
};

} // namespace pad
