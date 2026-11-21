// PAD Engine — compressor "de cola" do barramento master.
//
// Bem suave (razão 1,6:1, joelho de 8 dB, ataque 40 ms, release 450 ms): junta as camadas
// e segura os picos de energia quando várias notas e o reverb se somam, sem "bombear".
// Detector RMS estéreo (1 polo); o ganho é calculado em dB por sub-bloco e interpolado por
// amostra. Atua antes do volume geral, então o volume do operador não muda o caráter.
#pragma once

#include "DspMath.h"

namespace pad {

class GlueCompressor {
public:
    void prepare(float sampleRate, int controlBlock) {
        sr_ = sampleRate;
        detCoef_ = std::exp(-1.0f / (0.030f * sampleRate));
        const float cr = sampleRate / static_cast<float>(controlBlock);
        att_ = std::exp(-1.0f / (0.040f * cr));
        rel_ = std::exp(-1.0f / (0.450f * cr));
        reset();
    }
    void reset() { power_ = 0.0f; grDb_ = 0.0f; gain_ = 1.0f; }
    void setThresholdDb(float t) { thr_ = t; }
    void setRatio(float r) { ratio_ = std::max(1.0f, r); }

    // Processa um sub-bloco in-place.
    void process(float* l, float* r, int n) {
        for (int i = 0; i < n; ++i) {
            const float p = 0.5f * (l[i] * l[i] + r[i] * r[i]);
            power_ = p + detCoef_ * (power_ - p);
        }
        const float levelDb = 10.0f * std::log10(power_ + 1e-12f);
        const float over = levelDb - thr_;
        float target;
        if (over <= -kKnee * 0.5f) target = 0.0f;
        else if (over >= kKnee * 0.5f) target = over * (1.0f - 1.0f / ratio_);
        else { const float x = over + kKnee * 0.5f; target = (1.0f - 1.0f / ratio_) * x * x / (2.0f * kKnee); }
        grDb_ = target > grDb_ ? target + att_ * (grDb_ - target) : target + rel_ * (grDb_ - target);
        const float g1 = dbToGain(-grDb_);
        const float g0 = gain_;
        const float inv = 1.0f / static_cast<float>(n);
        for (int i = 0; i < n; ++i) {
            const float g = g0 + (g1 - g0) * static_cast<float>(i + 1) * inv;
            l[i] *= g;
            r[i] *= g;
        }
        gain_ = g1;
        maxGr_ = std::max(maxGr_, grDb_);
    }
    float takeMaxReductionDb() { const float m = maxGr_; maxGr_ = 0.0f; return m; }

private:
    static constexpr float kKnee = 8.0f;
    float sr_ = 48000.0f, detCoef_ = 0.0f, att_ = 0.0f, rel_ = 0.0f;
    float power_ = 0.0f, grDb_ = 0.0f, gain_ = 1.0f, maxGr_ = 0.0f;
    float thr_ = -16.0f, ratio_ = 1.6f;
};

} // namespace pad
