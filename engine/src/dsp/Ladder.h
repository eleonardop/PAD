// PAD Engine — filtro passa-baixa ladder de 4 polos, ZDF (atraso zero na realimentação).
//
// Topologia "Moog" resolvida de forma implícita (Zavalishin, "The Art of VA Filter Design"):
// os 4 estágios TPT de 1 polo e a realimentação são resolvidos juntos, sem o atraso de uma
// amostra das versões ingênuas. Resultado: resposta correta em qualquer frequência de corte,
// estável sob modulação, 24 dB/oitava, sem "zipper".
// Uma saturação suave na entrada do laço (proporcional ao 'drive') dá o arredondamento
// típico dos filtros analógicos quando o sinal é mais forte, sem distorção audível em pads.
#pragma once

#include "DspMath.h"

namespace pad {

class LadderFilter {
public:
    void reset() { s1_ = s2_ = s3_ = s4_ = 0.0f; }

    // k: realimentação (0 = sem ressonância, 4 = auto-oscilação). Usamos <= 1,2.
    void setCoefficients(float cutoffHz, float k, float sampleRate) {
        const float fc = clampf(cutoffHz, 20.0f, sampleRate * 0.45f);
        const float g = std::tan(kPi * fc / sampleRate);
        G_ = g / (1.0f + g);
        k_ = k;
        G4_ = G_ * G_ * G_ * G_;
        comp_ = 1.0f + 0.6f * k; // compensa parte da perda de grave causada pela realimentação
    }
    void setDrive(float d) { drive_ = std::max(d, 0.05f); invDrive_ = 1.0f / drive_; }

    inline float process(float x) {
        const float G = G_;
        // Contribuição dos estados para a saída (y4 = G^4 u + S)
        const float S = G * G * G * (1.0f - G) * s1_ + G * G * (1.0f - G) * s2_ + G * (1.0f - G) * s3_ + (1.0f - G) * s4_;
        const float y4est = (G4_ * x + S) / (1.0f + k_ * G4_);
        float u = x - k_ * y4est;
        u = softSat(u * drive_) * invDrive_;
        // Estágios TPT
        float v = G * (u - s1_); float y = v + s1_; s1_ = y + v;
        v = G * (y - s2_); y = v + s2_; s2_ = y + v;
        v = G * (y - s3_); y = v + s3_; s3_ = y + v;
        v = G * (y - s4_); y = v + s4_; s4_ = y + v;
        return y * comp_;
    }

private:
    float s1_ = 0, s2_ = 0, s3_ = 0, s4_ = 0;
    float G_ = 0.5f, G4_ = 0.0625f, k_ = 0.0f, comp_ = 1.0f;
    float drive_ = 0.6f, invDrive_ = 1.0f / 0.6f;
};

} // namespace pad
