// PAD Engine — filtro de estado variável TPT (topologia "trapezoidal", Zavalishin/Simper).
// Vantagens para pads: estável sob modulação rápida de frequência, sem "zipper",
// resposta idêntica ao protótipo analógico no centro da banda, sem warping de ganho.
#pragma once

#include "DspMath.h"

namespace pad {

struct SvfCoeffs {
    float a1 = 1, a2 = 0, a3 = 0, k = 1;
    float m0 = 0, m1 = 0, m2 = 1;

    static float g(float freq, float sampleRate) {
        const float f = clampf(freq, 5.0f, sampleRate * 0.49f);
        return std::tan(kPi * f / sampleRate);
    }
    void setCore(float gv, float kv) {
        k = kv;
        a1 = 1.0f / (1.0f + gv * (gv + kv));
        a2 = gv * a1;
        a3 = gv * a2;
    }
    void lowpass(float f, float q, float sr) { setCore(g(f, sr), 1.0f / q); m0 = 0; m1 = 0; m2 = 1; }
    void highpass(float f, float q, float sr) { setCore(g(f, sr), 1.0f / q); m0 = 1; m1 = -k; m2 = -1; }
    // Passa-banda com ganho unitário no centro.
    void bandpass(float f, float q, float sr) { setCore(g(f, sr), 1.0f / q); m0 = 0; m1 = k; m2 = 0; }
    void bell(float f, float q, float gainDb, float sr) {
        const float A = std::pow(10.0f, gainDb / 40.0f);
        setCore(g(f, sr), 1.0f / (q * A)); m0 = 1; m1 = k * (A * A - 1.0f); m2 = 0;
    }
    void lowShelf(float f, float q, float gainDb, float sr) {
        const float A = std::pow(10.0f, gainDb / 40.0f);
        setCore(g(f, sr) / std::sqrt(A), 1.0f / q); m0 = 1; m1 = k * (A - 1.0f); m2 = A * A - 1.0f;
    }
    void highShelf(float f, float q, float gainDb, float sr) {
        const float A = std::pow(10.0f, gainDb / 40.0f);
        setCore(g(f, sr) * std::sqrt(A), 1.0f / q); m0 = A * A; m1 = k * (1.0f - A) * A; m2 = 1.0f - A * A;
    }
};

struct SvfState {
    float ic1 = 0, ic2 = 0;
    void reset() { ic1 = ic2 = 0; }
    inline float process(const SvfCoeffs& c, float v0) {
        const float v3 = v0 - ic2;
        const float v1 = c.a1 * ic1 + c.a2 * v3;
        const float v2 = ic2 + c.a2 * ic1 + c.a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        return c.m0 * v0 + c.m1 * v1 + c.m2 * v2;
    }
};

// Filtro estéreo com coeficientes compartilhados.
struct SvfStereo {
    SvfCoeffs c;
    SvfState l, r;
    void reset() { l.reset(); r.reset(); }
    inline void process(float& xl, float& xr) { xl = l.process(c, xl); xr = r.process(c, xr); }
};

// Filtro de 1 polo (suavização, damping de reverb, bloqueio de DC).
struct OnePole {
    float a = 0, z = 0;
    void setLowpass(float f, float sr) { a = std::exp(-kTwoPi * clampf(f, 1.0f, sr * 0.49f) / sr); }
    inline float lp(float x) { z = x + a * (z - x); return z; }
    inline float hp(float x) { return x - lp(x); }
    void reset() { z = 0; }
};

} // namespace pad
