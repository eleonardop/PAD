// PAD Engine — geradores pseudoaleatórios determinísticos e modulador aleatório suave.
#pragma once

#include <cstdint>
#include "DspMath.h"

namespace pad {

// PCG32 pequeno: rápido, determinístico (testes reprodutíveis), sem alocação.
class Rng {
public:
    explicit Rng(uint64_t seed = 0x853c49e6748fea9bULL) { setSeed(seed); }
    void setSeed(uint64_t seed) { state_ = 0; next(); state_ += seed; next(); }
    uint32_t next() {
        const uint64_t old = state_;
        state_ = old * 6364136223846793005ULL + 1442695040888963407ULL;
        const uint32_t xorshifted = static_cast<uint32_t>(((old >> 18u) ^ old) >> 27u);
        const uint32_t rot = static_cast<uint32_t>(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
    }
    float uniform() { return static_cast<float>(next() >> 8) * (1.0f / 16777216.0f); } // [0,1)
    float bipolar() { return uniform() * 2.0f - 1.0f; }                                 // [-1,1)
    float range(float lo, float hi) { return lo + (hi - lo) * uniform(); }
private:
    uint64_t state_ = 0;
};

// Modulador aleatório suave ("movimento orgânico"): pontos aleatórios ligados por
// spline Catmull-Rom, ou seja, contínuo e com derivada contínua (sem "degraus").
// Saída em [-1, 1] aproximadamente. Atualizado em taxa de controle.
class SmoothRandom {
public:
    void init(uint64_t seed, float rateHz, float controlRate) {
        rng_.setSeed(seed);
        p_[0] = rng_.bipolar(); p_[1] = rng_.bipolar(); p_[2] = rng_.bipolar(); p_[3] = rng_.bipolar();
        phase_ = rng_.uniform();
        setRate(rateHz, controlRate);
    }
    void setRate(float rateHz, float controlRate) { inc_ = rateHz / controlRate; }
    // Avança 1 passo de controle e devolve o valor.
    float tick() {
        phase_ += inc_;
        while (phase_ >= 1.0f) {
            phase_ -= 1.0f;
            p_[0] = p_[1]; p_[1] = p_[2]; p_[2] = p_[3]; p_[3] = rng_.bipolar();
        }
        return clampf(hermite4(p_[0], p_[1], p_[2], p_[3], phase_), -1.0f, 1.0f);
    }
private:
    Rng rng_;
    float p_[4] = {0, 0, 0, 0};
    float phase_ = 0.0f;
    float inc_ = 0.0f;
};

} // namespace pad
