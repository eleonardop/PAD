// PAD Engine — shimmer externo, comum aos três reverbs.
//
// A saída do reverb do sub-bloco anterior é transposta +12 semitons (pitch shifter granular
// de duas cabeças), filtrada entre 700 Hz e 5,2 kHz e devolvida à entrada do reverb.
// Cada passagem sobe uma oitava e é atenuada; em no máximo ~3 passagens o conteúdo sai da
// banda do filtro, então o laço é estável mesmo com ganho alto. O atraso extra de um
// sub-bloco (0,67 ms) no laço é desprezível.
#pragma once

#include "PitchShifter.h"
#include "Svf.h"

namespace pad {

class ShimmerLoop {
public:
    static constexpr int kMaxBlock = 64;

    void prepare(float sampleRate) {
        l_.prepare(sampleRate, 82.0f);
        r_.prepare(sampleRate, 77.0f); // janelas diferentes descorrelacionam L/R
        l_.setRatio(2.0f);
        r_.setRatio(2.0f);
        hp_.c.highpass(700.0f, 0.707f, sampleRate);
        lp_.c.lowpass(5200.0f, 0.6f, sampleRate);
        reset();
    }
    void reset() {
        l_.reset(); r_.reset(); hp_.reset(); lp_.reset();
        for (int i = 0; i < kMaxBlock; ++i) prevL_[i] = prevR_[i] = 0.0f;
    }
    // Soma ao envio (sendL/R) o retorno transposto, com ganho 'feedback' (0..~0,5).
    void addToSend(float* sendL, float* sendR, int n, float feedback) {
        if (feedback <= 1e-4f && !active_) return;
        active_ = feedback > 1e-4f;
        for (int i = 0; i < n; ++i) {
            float a = l_.process(prevL_[i]);
            float b = r_.process(prevR_[i]);
            hp_.process(a, b);
            lp_.process(a, b);
            sendL[i] += a * feedback;
            sendR[i] += b * feedback;
        }
    }
    void storeWet(const float* wetL, const float* wetR, int n) {
        for (int i = 0; i < n; ++i) { prevL_[i] = wetL[i]; prevR_[i] = wetR[i]; }
    }

private:
    PitchShifter l_, r_;
    SvfStereo hp_, lp_;
    float prevL_[kMaxBlock] = {}, prevR_[kMaxBlock] = {};
    bool active_ = false;
};

} // namespace pad
