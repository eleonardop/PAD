// PAD Engine — leis de crossfade.
//
// Para dois sinais com correlação rho, a potência da soma g1*a + g2*b é
//   P = g1^2 + g2^2 + 2*rho*g1*g2      (sinais de mesma potência).
// - rho = 0 (sinais independentes, ex.: acordes com notas diferentes): equal-power
//   (g1 = cos, g2 = sin) mantém P = 1.
// - rho = 1 (sinais idênticos/em fase): equal-power daria +3 dB no meio; o correto
//   é o crossfade linear (g1 + g2 = 1).
// A lei abaixo parte da curva equal-power e a normaliza pela correlação estimada,
// garantindo P = 1 para qualquer rho em [0, 1]. Para rho < 0 (oposição de fase)
// limitamos o ganho de compensação para não amplificar cancelamentos.
#pragma once

#include "DspMath.h"

namespace pad {

struct CrossfadeGains { float out; float in; };

// t em [0,1]: 0 = só o sinal antigo, 1 = só o novo.
inline CrossfadeGains crossfadeGains(float t, float rho) {
    t = clampf(t, 0.0f, 1.0f);
    const float a = eqPower(1.0f - t); // cos
    const float b = eqPower(t);        // sin
    const float r = clampf(rho, -0.5f, 1.0f);
    const float p = a * a + b * b + 2.0f * r * a * b;
    const float n = 1.0f / std::sqrt(std::max(p, 0.5f));
    return { a * n, b * n };
}

// Estima a correlação normalizada entre dois trechos (uso fora do callback: loops de samples).
inline float estimateCorrelation(const float* a, const float* b, int n) {
    double ab = 0, aa = 0, bb = 0;
    for (int i = 0; i < n; ++i) { ab += double(a[i]) * b[i]; aa += double(a[i]) * a[i]; bb += double(b[i]) * b[i]; }
    if (aa <= 1e-20 || bb <= 1e-20) return 0.0f;
    return static_cast<float>(ab / std::sqrt(aa * bb));
}

} // namespace pad
