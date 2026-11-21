// PAD Engine — caráter de fita (v0.3, preset Vintage).
//
// Wow (~0,4–0,9 Hz, ruído suave) + flutter (~6,5 Hz) numa linha de atraso curta, saturação
// suave e perda de agudos. A MESMA modulação vale para os dois canais: o efeito muda a
// afinação por instantes, nunca a relação de fase entre L e R (seguro em mono). Sem chiado.
#pragma once

#include <array>
#include "DspMath.h"
#include "Random.h"
#include "Svf.h"

namespace pad {

class TapeCharacter {
public:
    void prepare(float sampleRate) {
        sr_ = sampleRate;
        bufL_.fill(0.0f);
        bufR_.fill(0.0f);
        w_ = 0;
        wow_.init(0x7a9e, 0.6f, sampleRate / 32.0f);
        lp_.reset();
        mixK_ = 1.0f - std::exp(-1.0f / (0.05f * sampleRate));
        mix_ = 0.0f;
    }
    // amount 0..1; processa no lugar. Chamar por sub-bloco (n ≤ 32).
    void process(float* l, float* r, int n, float amount) {
        if (amount <= 1e-4f && lastAmount_ <= 1e-4f && mix_ < 1e-4f) {
            // Mantém a linha cheia para religar sem buraco.
            for (int i = 0; i < n; ++i) { bufL_[w_] = l[i]; bufR_[w_] = r[i]; w_ = (w_ + 1) & kMask; }
            return;
        }
        const float a0 = lastAmount_, a1 = amount;
        lastAmount_ = amount;
        // Taxa de controle: um passo do ruído do wow por sub-bloco.
        const float wow = wow_.tick();
        lp_.c.lowpass(16000.0f - 9000.0f * a1, 0.6f, sr_);
        for (int i = 0; i < n; ++i) {
            const float a = a0 + (a1 - a0) * static_cast<float>(i + 1) / static_cast<float>(n);
            flutterPhase_ += kTwoPi * 6.5f / sr_;
            if (flutterPhase_ > kTwoPi) flutterPhase_ -= kTwoPi;
            // Até ±0,9 ms de wow (≈ ±6 cents a 0,6 Hz) e ±0,05 ms de flutter.
            const float dMs = 4.0f + a * (0.9f * wow + 0.05f * std::sin(flutterPhase_));
            const float d = dMs * 0.001f * sr_;
            bufL_[w_] = l[i];
            bufR_[w_] = r[i];
            const float rp = static_cast<float>(w_) - d + static_cast<float>(kSize);
            const int i0 = static_cast<int>(rp);
            const float f = rp - static_cast<float>(i0);
            auto rd = [&](const std::array<float, kSize>& b) {
                const float x0 = b[static_cast<size_t>((i0 - 1) & kMask)], x1 = b[static_cast<size_t>(i0 & kMask)];
                const float x2 = b[static_cast<size_t>((i0 + 1) & kMask)], x3 = b[static_cast<size_t>((i0 + 2) & kMask)];
                return hermite4(x0, x1, x2, x3, f);
            };
            float yl = rd(bufL_), yr = rd(bufR_);
            w_ = (w_ + 1) & kMask;
            // Saturação suave (compensada) e perda de agudos.
            const float drive = 1.0f + 1.5f * a;
            yl = std::tanh(yl * drive) / drive;
            yr = std::tanh(yr * drive) / drive;
            lp_.process(yl, yr);
            // Liga/desliga por troca rápida (50 ms) entre seco e fita: misturar os dois de forma
            // contínua somaria um eco de 4 ms (filtro em pente). A profundidade vem de `a`.
            mix_ += ((a > 1e-3f ? 1.0f : 0.0f) - mix_) * mixK_;
            l[i] = l[i] + (yl - l[i]) * mix_;
            r[i] = r[i] + (yr - r[i]) * mix_;
        }
    }

private:
    static constexpr int kSize = 1024, kMask = kSize - 1;
    std::array<float, kSize> bufL_{}, bufR_{};
    int w_ = 0;
    float sr_ = 48000.0f, flutterPhase_ = 0.0f, lastAmount_ = 0.0f, mix_ = 0.0f, mixK_ = 0.001f;
    SmoothRandom wow_;
    SvfStereo lp_;
};

} // namespace pad
