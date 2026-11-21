// PAD Engine — reverb FDN (Feedback Delay Network) estéreo de 8 linhas, com Shimmer.
//
// Por que FDN: densidade de ecos alta e cauda lisa com custo baixo (8 linhas + matriz
// de Householder, que é sem perdas e custa O(N)). Para evitar o som metálico típico:
//  - difusão de entrada com 4 allpass em série por canal;
//  - comprimentos das linhas mutuamente "primos" e espalhados (37 a 97 ms);
//  - modulação lenta e independente do atraso de cada linha (leitura Hermite),
//    que quebra as ressonâncias fixas dos modos;
//  - damping de 1 polo por linha com ganho calculado para RT60 exato por linha.
// Shimmer: a saída da cauda passa por pitch shift de +12 semitons, filtro passa-banda
// (corta graves embolados e agudos ásperos) e volta à entrada da rede. Cada passagem
// sobe uma oitava e é atenuada, criando o "brilho" ascendente sem estridência.
#pragma once

#include <array>
#include "DelayLine.h"
#include "PitchShifter.h"
#include "Svf.h"
#include "Random.h"

namespace pad {

class FdnReverb {
public:
    static constexpr int kLines = 8;

    void prepare(float sampleRate);
    void reset();

    // decaySeconds: RT60; size 0..1; damping 0..1 (1 = mais escuro); shimmer 0..1.
    void setParams(float decaySeconds, float size, float damping, float shimmer);

    // Processa uma amostra estéreo de entrada (envio) e devolve a saída "wet".
    void process(float inL, float inR, float& outL, float& outR);

private:
    void updateGains();

    float sr_ = 48000.0f;
    std::array<DelayLine, kLines> lines_;
    std::array<float, kLines> baseDelay_{};   // em amostras (já com "size")
    std::array<float, kLines> gain_{};
    std::array<OnePole, kLines> damp_{};
    std::array<float, kLines> modPhase_{}, modInc_{}, modDepth_{};
    std::array<AllpassDiffuser, 4> diffL_, diffR_;
    DelayLine preDelayL_, preDelayR_;
    int preDelay_ = 960;

    PitchShifter shiftL_, shiftR_;
    SvfStereo shimmerHp_, shimmerLp_;
    float shimmer_ = 0.0f;
    float fbL_ = 0.0f, fbR_ = 0.0f;

    float decay_ = 4.0f, size_ = 0.6f, damping_ = 0.4f;
    bool dirty_ = true;
};

} // namespace pad
