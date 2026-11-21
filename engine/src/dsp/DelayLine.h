// PAD Engine — linha de atraso circular com leitura fracionária (Hermite).
// Memória alocada apenas em prepare(); nada é alocado durante o processamento.
#pragma once

#include <vector>
#include "DspMath.h"

namespace pad {

class DelayLine {
public:
    void prepare(int maxDelaySamples) {
        int size = 1;
        while (size < maxDelaySamples + 4) size <<= 1;
        buf_.assign(static_cast<size_t>(size), 0.0f);
        mask_ = size - 1;
        write_ = 0;
    }
    void reset() { std::fill(buf_.begin(), buf_.end(), 0.0f); }
    inline void push(float x) { buf_[static_cast<size_t>(write_)] = x; write_ = (write_ + 1) & mask_; }
    // Atraso inteiro >= 1 (1 = última amostra escrita).
    inline float readInt(int delay) const { return buf_[static_cast<size_t>((write_ - delay) & mask_)]; }
    // Atraso fracionário >= 1.
    inline float readFrac(float delay) const {
        const int di = static_cast<int>(delay);
        const float f = delay - static_cast<float>(di);
        // Leitura "para trás": amostras em delay-1, delay, delay+1, delay+2
        const float xm1 = readInt(di - 1 < 1 ? 1 : di - 1);
        const float x0 = readInt(di);
        const float x1 = readInt(di + 1);
        const float x2 = readInt(di + 2);
        return hermite4(xm1, x0, x1, x2, f);
    }
    int capacity() const { return mask_ + 1; }
private:
    std::vector<float> buf_;
    int mask_ = 0;
    int write_ = 0;
};

// Allpass de difusão (Schroeder) com atraso fixo.
class AllpassDiffuser {
public:
    void prepare(int delaySamples, float g) { line_.prepare(delaySamples + 1); delay_ = std::max(delaySamples, 1); g_ = g; }
    void reset() { line_.reset(); }
    inline float process(float x) {
        const float d = line_.readInt(delay_);
        const float v = x + g_ * d;
        line_.push(v);
        return d - g_ * v;
    }
private:
    DelayLine line_;
    int delay_ = 1;
    float g_ = 0.6f;
};

} // namespace pad
