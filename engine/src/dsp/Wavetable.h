// PAD Engine — wavetables com mipmap por oitava (osciladores sem aliasing).
//
// Cada tabela é gerada por síntese aditiva: para cada faixa de frequência fundamental
// só entram harmônicos abaixo de 0,45*fs, portanto não há aliasing por construção.
// Tamanho 4096 e no máximo 512 harmônicos por tabela: o harmônico mais alto tem 8
// amostras por ciclo, o que mantém o erro da interpolação Hermite abaixo de -90 dB
// em relação ao sinal (o harmônico mais alto já é ~-50 dB).
#pragma once

#include <vector>
#include <functional>
#include "DspMath.h"

namespace pad {

class Wavetable {
public:
    static constexpr int kSize = 4096;
    static constexpr int kMask = kSize - 1;
    static constexpr int kLevels = 10;     // faixas: f0 <= 40 Hz * 2^level
    static constexpr int kMaxHarmonics = 512;
    static constexpr float kBaseTopHz = 40.0f;

    // amplitude(h) para h >= 1. Fases aleatórias determinísticas reduzem o fator de crista
    // (mais headroom) sem alterar o timbre de um som sustentado.
    void build(const std::function<float(int)>& amplitude, float sampleRate, uint32_t phaseSeed, bool randomPhases);

    // Escolhe o nível de mipmap para uma fundamental (com margem para modulação de pitch).
    int levelFor(float f0) const {
        int l = 0;
        float top = kBaseTopHz;
        while (l < kLevels - 1 && f0 * 1.08f > top) { top *= 2.0f; ++l; }
        return l;
    }

    // Leitura com Hermite. phase em [0,1).
    inline float read(int level, float phase) const {
        const float* t = data_.data() + static_cast<size_t>(level) * (kSize + 3) + 1; // guarda: 1 antes, 2 depois
        const float idx = phase * kSize;
        const int i = static_cast<int>(idx);
        const float f = idx - static_cast<float>(i);
        return hermite4(t[i - 1], t[i], t[i + 1], t[i + 2], f);
    }

    bool empty() const { return data_.empty(); }

private:
    std::vector<float> data_;
};

// Oscilador de fase acumulada sobre uma Wavetable.
struct WavetableOsc {
    float phase = 0.0f;
    inline float tick(const Wavetable& wt, int level, float inc) {
        const float y = wt.read(level, phase);
        phase += inc;
        phase -= static_cast<float>(static_cast<int>(phase)); // inc sempre >= 0
        return y;
    }
};

} // namespace pad
