// PAD Engine — utilidades matemáticas de DSP.
// Tudo aqui é inline, sem alocação e seguro para o callback de áudio.
#pragma once

#include <cmath>
#include <cstdint>
#include <algorithm>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__)
#include <xmmintrin.h>
#include <pmmintrin.h>
#endif

namespace pad {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;
constexpr float kHalfPi = 1.57079632679489661923f;
constexpr double kPiD = 3.14159265358979323846;

inline float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float dbToGain(float db) { return std::pow(10.0f, db * 0.05f); }
inline float gainToDb(float g) { return 20.0f * std::log10(std::max(g, 1e-12f)); }
inline double midiToHz(double midi) { return 440.0 * std::pow(2.0, (midi - 69.0) / 12.0); }
inline float centsToRatio(float cents) { return std::exp2(cents * (1.0f / 1200.0f)); }

// Mapeamento exponencial 0..1 -> [lo, hi] (para frequências e tempos).
inline float expMap(float t, float lo, float hi) { return lo * std::pow(hi / lo, clampf(t, 0.0f, 1.0f)); }

// Interpolação de Hermite de 4 pontos e 3ª ordem (Catmull-Rom).
// Escolhida para leitura de wavetables, samples e linhas de atraso moduladas:
// erro bem menor que a linear (ver teste "interpolacao_hermite_vs_linear") com custo baixo.
inline float hermite4(float xm1, float x0, float x1, float x2, float t) {
    const float c0 = x0;
    const float c1 = 0.5f * (x1 - xm1);
    const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * t + c2) * t + c1) * t + c0;
}

// Saturação suave (aproximação racional de tanh, contínua e com derivada contínua).
// Usada com moderação no "Warmth"; nunca como limitador.
inline float softSat(float x) {
    x = clampf(x, -3.0f, 3.0f);
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

// Curvas de crossfade/envelope sem trigonometria por amostra.
inline float smoothStep(float t) { t = clampf(t, 0.0f, 1.0f); return t * t * (3.0f - 2.0f * t); }

// Seno rápido para o intervalo [0, pi/2] (erro < 1e-6), usado nas curvas equal-power.
inline float fastSinQuarter(float x) {
    const float x2 = x * x;
    return x * (1.0f + x2 * (-0.16666667f + x2 * (0.0083333310f + x2 * (-0.00019840874f + x2 * 2.7525562e-6f))));
}
// Ganho equal-power para posição p em [0,1]: sin(p*pi/2). Seu complemento é eqPower(1-p) = cos(p*pi/2).
inline float eqPower(float p) { return fastSinQuarter(clampf(p, 0.0f, 1.0f) * kHalfPi); }

// Desliga denormais no thread atual enquanto o objeto existir (evita picos de CPU em caudas de reverb).
class ScopedNoDenormals {
public:
    ScopedNoDenormals() {
#if defined(__x86_64__) || defined(_M_X64)
        oldCsr_ = _mm_getcsr();
        _mm_setcsr(oldCsr_ | 0x8040); // FTZ | DAZ
#elif defined(__aarch64__)
        uint64_t fpcr;
        asm volatile("mrs %0, fpcr" : "=r"(fpcr));
        oldFpcr_ = fpcr;
        fpcr |= (1ull << 24); // FZ
        asm volatile("msr fpcr, %0" : : "r"(fpcr));
#endif
    }
    ~ScopedNoDenormals() {
#if defined(__x86_64__) || defined(_M_X64)
        _mm_setcsr(oldCsr_);
#elif defined(__aarch64__)
        asm volatile("msr fpcr, %0" : : "r"(oldFpcr_));
#endif
    }
    ScopedNoDenormals(const ScopedNoDenormals&) = delete;
    ScopedNoDenormals& operator=(const ScopedNoDenormals&) = delete;
private:
#if defined(__x86_64__) || defined(_M_X64)
    unsigned int oldCsr_ = 0;
#elif defined(__aarch64__)
    uint64_t oldFpcr_ = 0;
#endif
};

} // namespace pad
