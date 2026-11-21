// PAD Engine — pitch shifter granular de duas cabeças (usado no Shimmer).
//
// Duas leituras sobre uma linha de atraso percorrem a janela em velocidade (1 - ratio),
// defasadas de meio período. Cada cabeça recebe janela sin^2; as duas somam exatamente 1
// em qualquer instante (sin^2 + cos^2), então não há modulação de amplitude.
// Janela longa (~80 ms) favorece sons sustentados (pads), reduzindo "granulação".
#pragma once

#include "DelayLine.h"

namespace pad {

class PitchShifter {
public:
    void prepare(float sampleRate, float windowMs = 80.0f) {
        window_ = std::max(64.0f, windowMs * 0.001f * sampleRate);
        line_.prepare(static_cast<int>(window_) + 8);
        phase_ = 0.0f;
    }
    void setRatio(float ratio) { ratio_ = ratio; }
    void reset() { line_.reset(); phase_ = 0.0f; }

    inline float process(float x) {
        line_.push(x);
        // phase_ em [0,1): fração da janela. Atraso diminui quando ratio > 1.
        phase_ += (1.0f - ratio_) / window_;
        phase_ -= std::floor(phase_);
        float p2 = phase_ + 0.5f;
        if (p2 >= 1.0f) p2 -= 1.0f;
        const float d1 = 2.0f + phase_ * window_;
        const float d2 = 2.0f + p2 * window_;
        const float w1 = fastSinQuarter(phase_ <= 0.5f ? phase_ * kPi : (1.0f - phase_) * kPi);
        const float w2 = fastSinQuarter(p2 <= 0.5f ? p2 * kPi : (1.0f - p2) * kPi);
        return line_.readFrac(d1) * (w1 * w1) + line_.readFrac(d2) * (w2 * w2);
    }
private:
    DelayLine line_;
    float window_ = 3840.0f;
    float phase_ = 0.0f;
    float ratio_ = 2.0f;
};

} // namespace pad
