// PAD Engine — reverb de placa de Jon Dattorro ("Effect Design Part 1", JAES 1997),
// com pré-delay e reflexões iniciais próprias (EarlyReflections).
//
// Estrutura: passa-baixa de banda na entrada -> 4 allpass de difusão -> "tanque" em figura
// de 8 (dois ramos, cada um com allpass modulado, linha de atraso, amortecimento, ganho de
// decaimento, segundo allpass e segunda linha, cruzando para o outro ramo). As saídas L/R
// são somas de 7 pontos do tanque (tabela do artigo). Os tempos do artigo (29,761 kHz) são
// convertidos para a taxa atual e escalados pelo tamanho; os allpasses do tanque são
// modulados lentamente com leitura Hermite para evitar ressonância metálica.
#pragma once

#include "ReverbBase.h"
#include "DelayLine.h"
#include "EarlyReflections.h"
#include "Svf.h"

namespace pad {

class DattorroPlate : public ReverbBase {
public:
    void prepare(float sampleRate) override;
    void reset() override;
    void setParams(const ReverbParams& p) override;
    void process(const float* inL, const float* inR, float* outL, float* outR, int n) override;

private:
    struct ModAllpass {
        DelayLine line;
        float base = 100.0f, depth = 0.0f, g = 0.5f, phase = 0.0f, inc = 0.0f;
        inline float process(float x, bool modulate) {
            float d = base;
            if (modulate) {
                phase += inc;
                if (phase >= 1.0f) phase -= 1.0f;
                const float t = 4.0f * std::fabs(phase - 0.5f) - 1.0f;
                d += depth * t * (1.5f - 0.5f * t * t);
            }
            const float delayed = line.readFrac(std::max(2.0f, d));
            const float v = x + g * delayed;   // forma canônica (sinal do artigo: entrada - g*atrasado nos de decaimento)
            line.push(v);
            return delayed - g * v;
        }
        // Leitura de um ponto interno (para as saídas do tanque).
        inline float tap(int samplesAgo) const { return line.readInt(samplesAgo); }
    };

    float scaled(float samples29k) const { return samples29k * (sr_ / 29761.0f) * sizeScale_; }
    void updateLengths();

    float sr_ = 48000.0f;
    float sizeScale_ = 1.0f;
    EarlyReflections early_;
    OnePole bandwidth_;
    ModAllpass inDiff_[4];
    ModAllpass tankAp1_[2];   // modulados, "decay diffusion 1"
    ModAllpass tankAp2_[2];   // "decay diffusion 2"
    DelayLine tankD1_[2], tankD2_[2];
    OnePole damp_[2];
    float len1_[2] = {0, 0}, len2_[2] = {0, 0};
    float decayGain_ = 0.5f;
    float fb_[2] = {0, 0};
    float erLevel_ = 0.4f;
    ReverbParams p_;
    bool dirty_ = true;
};

} // namespace pad
