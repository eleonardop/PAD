// PAD Engine — pré-delay + reflexões iniciais estéreo + difusão de entrada.
//
// Usado na frente do Plate (Dattorro) e do FDN. Os 9 taps por canal têm tempos espalhados
// entre ~6 e ~70 ms (escalados pelo tamanho da sala), polaridades alternadas e ganhos
// decrescentes, com os tempos de L e R diferentes (descorrelação). As reflexões passam por
// um passa-baixa suave (paredes absorvem agudos), o que dá distância sem aspereza.
#pragma once

#include <array>
#include "DelayLine.h"
#include "Svf.h"

namespace pad {

class EarlyReflections {
public:
    static constexpr int kTaps = 9;

    void prepare(float sampleRate) {
        sr_ = sampleRate;
        line_.prepare(static_cast<int>(0.25f * sampleRate) + 16);
        lp_.c.lowpass(5500.0f, 0.6f, sampleRate);
        reset();
    }
    void reset() { line_.reset(); lp_.reset(); }

    void set(float preDelaySeconds, float size, float level) {
        pre_ = clampf(preDelaySeconds, 0.0f, 0.12f) * sr_;
        level_ = level;
        const float scale = 0.6f + 0.8f * clampf(size, 0.0f, 1.0f);
        static const float kMsL[kTaps] = {6.1f, 11.3f, 17.9f, 23.2f, 31.7f, 38.9f, 47.3f, 58.1f, 69.7f};
        static const float kMsR[kTaps] = {7.3f, 12.9f, 16.1f, 25.7f, 29.9f, 41.3f, 44.9f, 61.7f, 66.1f};
        for (int i = 0; i < kTaps; ++i) {
            dl_[static_cast<size_t>(i)] = kMsL[i] * 0.001f * sr_ * scale;
            dr_[static_cast<size_t>(i)] = kMsR[i] * 0.001f * sr_ * scale;
            const float g = std::pow(0.78f, static_cast<float>(i)) * ((i & 1) ? -1.0f : 1.0f);
            gl_[static_cast<size_t>(i)] = g;
            gr_[static_cast<size_t>(i)] = -g;
        }
    }

    // Entrada mono-somada; devolve o sinal pré-atrasado (para a cauda) e as reflexões (L/R).
    inline void process(float in, float& delayedOut, float& erL, float& erR) {
        line_.push(in);
        delayedOut = line_.readFrac(std::max(1.0f, pre_));
        float l = 0.0f, r = 0.0f;
        for (int i = 0; i < kTaps; ++i) {
            l += gl_[static_cast<size_t>(i)] * line_.readInt(static_cast<int>(pre_ + dl_[static_cast<size_t>(i)]));
            r += gr_[static_cast<size_t>(i)] * line_.readInt(static_cast<int>(pre_ + dr_[static_cast<size_t>(i)]));
        }
        l *= 0.33f * level_;
        r *= 0.33f * level_;
        lp_.process(l, r);
        erL = l;
        erR = r;
    }

private:
    float sr_ = 48000.0f, pre_ = 0.0f, level_ = 0.4f;
    DelayLine line_;
    std::array<float, kTaps> dl_{}, dr_{}, gl_{}, gr_{};
    SvfStereo lp_;
};

} // namespace pad
