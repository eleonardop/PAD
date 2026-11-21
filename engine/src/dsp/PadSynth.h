// PAD Engine — gerador PADsynth (algoritmo de Paul Nasca).
//
// Cada harmônico vira uma faixa estreita de frequências (largura em cents) com fases
// aleatórias, sintetizada por uma IFFT longa. Resultado: um "ensemble" natural (vários
// instrumentos levemente diferentes tocando juntos), sem o batimento periódico de
// osciladores desafinados, e um loop perfeito por construção (a IFFT é periódica).
// Referência: https://zynaddsubfx.sourceforge.io/doc/PADsynth/PADsynth.htm (código de
// referência em domínio público). Esta é uma implementação própria.
//
// Roda só na preparação (fora do callback).
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace pad {

struct PadSynthSpec {
    // Amplitude do harmônico h (h >= 1) cuja frequência é freqHz.
    std::function<float(int h, float freqHz)> amplitude;
    float bandwidthCents = 25.0f;   // largura do 1º harmônico
    float bandwidthScale = 1.0f;    // 1 = largura constante em cents; > 1 = agudos mais largos
    float maxFreqHz = 16000.0f;     // nada acima disso (agudos controlados na origem)
    int firstHarmonic = 1;
    int maxHarmonics = 128;
};

// Gera 'n' (potência de 2) amostras de um loop periódico para a fundamental f0.
// Saída normalizada para RMS 0,1 (-20 dBFS), igual aos samples reais.
void generatePadSynth(const PadSynthSpec& spec, float f0, float sampleRate, int n, uint64_t seed, std::vector<float>& out);

} // namespace pad
