// PAD Engine — chorus "ensemble" de 3 fases (no estilo dos string machines dos anos 70), estéreo.
//
// Cada canal tem a própria linha de atraso (~7 ms) lida em três pontos. Cada ponto é modulado
// pela soma de dois LFOs senoidais: um lento (~0,55 Hz, profundidade maior) e um rápido
// (~5,9 Hz, profundidade pequena), defasados de 120° entre os três pontos. Com isso a variação
// de afinação de um ponto é compensada pelos outros: o som ganha largura e "vida" sem soar
// desafinado nem como vibrato. O canal direito usa as fases deslocadas de 60°, o que mantém
// os dois lados diferentes. Como cada canal processa o próprio sinal, a imagem estéreo das
// fontes é preservada (a versão anterior somava L e R e estreitava o som).
#pragma once

#include "DelayLine.h"

namespace pad {

class EnsembleChorus {
public:
    void prepare(float sampleRate) {
        sr_ = sampleRate;
        lineL_.prepare(static_cast<int>(0.030f * sampleRate) + 8);
        lineR_.prepare(static_cast<int>(0.030f * sampleRate) + 8);
        slowPhase_ = 0.0f;
        fastPhase_ = 0.0f;
    }
    void reset() { lineL_.reset(); lineR_.reset(); }

    // depth 0..1 (escala as duas profundidades)
    void setDepth(float depth) { depth_ = clampf(depth, 0.0f, 1.5f); }

    inline void process(float inL, float inR, float& outL, float& outR) {
        lineL_.push(inL);
        lineR_.push(inR);
        slowPhase_ += 0.55f / sr_;
        fastPhase_ += 5.9f / sr_;
        if (slowPhase_ >= 1.0f) slowPhase_ -= 1.0f;
        if (fastPhase_ >= 1.0f) fastPhase_ -= 1.0f;
        const float base = 0.0068f * sr_;
        const float dSlow = 0.0022f * sr_ * depth_;
        const float dFast = 0.00028f * sr_ * depth_;
        float l = 0.0f, r = 0.0f;
        for (int i = 0; i < 3; ++i) {
            const float off = static_cast<float>(i) / 3.0f;
            l += lineL_.readFrac(base + dSlow * sine(slowPhase_ + off) + dFast * sine(fastPhase_ + off));
            r += lineR_.readFrac(base + dSlow * sine(slowPhase_ + off + 1.0f / 6.0f) + dFast * sine(fastPhase_ + off + 1.0f / 6.0f));
        }
        // 3 pontos descorrelacionados somam ~+4,8 dB; 0,577 = 1/sqrt(3) preserva a potência.
        outL = 0.577f * l;
        outR = 0.577f * r;
    }

private:
    static inline float sine(float ph) {
        ph -= std::floor(ph);
        const float t = 4.0f * std::fabs(ph - 0.5f) - 1.0f;
        return t * (1.5f - 0.5f * t * t);
    }
    float sr_ = 48000.0f;
    DelayLine lineL_, lineR_;
    float slowPhase_ = 0.0f, fastPhase_ = 0.0f, depth_ = 1.0f;
};

} // namespace pad
