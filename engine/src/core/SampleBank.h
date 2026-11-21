// PAD Engine — banco de samples da camada Strings/Texture.
//
// Arquitetura híbrida: a camada Strings/Texture toca samples com interpolação Hermite.
// Como não há gravações licenciadas disponíveis nesta etapa, o banco padrão é gerado
// pelo próprio motor ("bake" offline, fora do callback): um ensemble de cordas por síntese
// aditiva com 4 "músicos" desafinados, até 40 parciais por nota, deriva lenta de afinação,
// vibrato de ensemble e evolução independente da amplitude de cada parcial. Esse
// processo é caro demais para tempo real (até ~160 osciladores por nota), por isso vira sample.
//
// Loops sem emenda: o bake é periódico por construção (todas as frequências e moduladores
// são múltiplos inteiros de 1/T), então o fim do loop encaixa exatamente no começo.
// Para WAVs externos (gravações reais no futuro), makeSeamlessLoop() aplica um crossfade
// de loop com lei dependente da correlação medida entre os trechos.
//
// Transposição: zonas a cada 4 semitons -> deslocamento máximo de ±2 semitons
// (fator 0,89–1,12). Nesse limite a mudança de formantes é pequena (~12%) e, como o
// conteúdo é limitado a ~10 kHz no bake, a leitura acelerada não gera aliasing audível.
#pragma once

#include <vector>
#include <memory>
#include <string>
#include "../dsp/DspMath.h"

namespace pad {

struct SampleZone {
    int rootNote = 60;
    float rootHz = 261.63f;
    float sampleRate = 48000.0f;
    int length = 0;              // amostras no loop (o buffer inteiro é o loop)
    std::vector<float> data;     // mono, com guardas: [len-1] + loop + [0,1,2]

    inline float at(int i) const { return data[static_cast<size_t>(i + 1)]; }
    // Leitura Hermite com posição já dentro de [0, length).
    inline float read(double pos) const {
        const int i = static_cast<int>(pos);
        const float f = static_cast<float>(pos - i);
        const float* p = data.data() + 1 + i;
        return hermite4(p[-1], p[0], p[1], p[2], f);
    }
};

class SampleBank {
public:
    static constexpr int kMaxTransposeSemitones = 2;

    // Gera o banco padrão de textura de cordas (pode levar ~1 s; usa várias threads).
    void bakeStringTexture(float sampleRate, uint64_t seed = 0xC0FFEE, int firstNote = 34, int lastNote = 90, int step = 4,
                           float loopSeconds = 6.0f);

    // Adiciona uma zona a partir de áudio mono (ex.: WAV carregado). Cria o loop sem emenda.
    bool addZoneFromAudio(const std::vector<float>& mono, float sampleRate, int rootNote, float crossfadeSeconds = 0.5f);

    // Zona mais próxima da nota pedida e a razão de leitura para soar em targetHz no outputRate.
    const SampleZone* zoneFor(float targetHz, float outputRate, double& ratioOut) const;

    bool empty() const { return zones_.empty(); }
    size_t zoneCount() const { return zones_.size(); }
    size_t memoryBytes() const;
    const SampleZone& zone(size_t i) const { return zones_[i]; }

    // Torna o trecho [0, loopEnd) um loop contínuo usando o trecho seguinte para o crossfade.
    // Devolve a correlação medida (informativa). Exposta para testes.
    static float makeSeamlessLoop(const std::vector<float>& in, int loopLen, int xfadeLen, std::vector<float>& out);

private:
    static void finalizeZone(SampleZone& z, const std::vector<float>& loop);
    std::vector<SampleZone> zones_; // ordenadas por rootNote
};

} // namespace pad
