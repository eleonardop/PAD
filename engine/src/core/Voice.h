// PAD Engine — uma voz = uma nota do acorde, renderizando todas as camadas.
//
// Camadas (v0.2):
//  - Warm  : tabela PADsynth com morph contínuo entre "escuro" e "aberto"
//  - Vox   : coral PADsynth com morph entre as vogais "oh" e "ah"
//  - Strings: cordas reais (VSCO 2 CE) ou sintéticas (v0.1, reserva)
//  - Texture: piano "Cathedral" (Salamander, congelado no espectro) ou órgão suave (VCSL)
//  - Air   : camada harmônica respirada (PADsynth, só harmônicos >= 2/3, sem ruído)
//  - Shimmer: "Glass" uma oitava acima
//  - Low   : fundamental grave limpa (wavetable)
//
// As tabelas de morph de uma família (ex.: Warm escuro/aberto) são geradas com as MESMAS
// fases aleatórias por faixa de frequência. Por isso a mistura linear das duas equivale a
// interpolar os espectros: o timbre muda sem cancelamentos nem "phasing".
//
// Ganho final = envelope x fade de crossfade (equal-power) x fade de roubo x ganho da nota
// x intensidade x respiração. Todos os ganhos são calculados por sub-bloco e interpolados
// linearmente por amostra: nenhuma mudança de estado causa degrau.
#pragma once

#include <cstdint>
#include "RenderContext.h"
#include "../dsp/Envelope.h"
#include "../dsp/Svf.h"
#include "../dsp/Ladder.h"
#include "../dsp/Random.h"

namespace pad {

class Voice {
public:
    enum class State : uint8_t { Free, Active, FadingOut, Releasing, Stealing };

    void prepare(float sampleRate, uint32_t index);

    void start(int note, bool isRoot, int lowNote, float noteGain, bool useAttack, float attackSec, float fadeSec,
               uint64_t age, const RenderContext& ctx);

    void fadeOut(float seconds);
    void reviveFade(float seconds);
    void reAttack(float attackSec);
    void release(float seconds);
    void steal(float seconds = 0.015f);
    void setRole(bool isRoot, int lowNote, float noteGain, float fadeSec);

    // Soma n (<= kSubBlock) amostras nos barramentos.
    void render(const RenderContext& ctx, Buses& out, int n);

    // Quais fontes esta voz está usando (para o carregador saber o que pode liberar).
    uint32_t sourcesInUse() const;

    State state() const { return state_; }
    bool isFree() const { return state_ == State::Free; }
    int note() const { return note_; }
    bool isRoot() const { return isRoot_; }
    uint64_t age() const { return age_; }
    float currentGain() const { return lastGain_; }

private:
    // Leitura de uma família de tabelas com morph (A <-> B) ou de um sample (só A).
    struct Layer {
        const Zone* a = nullptr;
        const Zone* b = nullptr;
        SourceId srcA = SourceId::Count, srcB = SourceId::Count;
        double ratio = 1.0;
        // Posição em ponto fixo 32.32 (inteiro.fração): avanço e volta do loop sem conversões
        // de double por amostra.
        uint64_t posL = 0, posR = 0;
        float avail = 0.0f;       // rampa de entrada quando a fonte fica pronta depois do início da nota
        float lastGain = 0.0f;    // ganho do sub-bloco anterior (interpolação)
        float lastMorph = 0.0f;
        bool active() const { return a != nullptr; }
    };

    float computeGain(int n);
    void free();
    bool acquire(Layer& L, const RenderContext& ctx, SourceId a, SourceId b);
    void acquireMissing(const RenderContext& ctx);
    // Soma a camada em (oL, oR) com ganho interpolado g0->g1 e morph m0->m1.
    void renderLayer(Layer& L, float* oL, float* oR, int n, float pitchRatio, float detuneCents, float gain, float morph);

    float sr_ = 48000.0f;
    uint32_t index_ = 0;
    State state_ = State::Free;
    int note_ = 60;
    float f0_ = 261.6f;
    uint64_t age_ = 0;

    PadEnvelope env_;
    float fadePos_ = 1.0f, fadeRate_ = 0.0f;
    float stealGain_ = 1.0f, stealRate_ = 0.0f;
    float noteGain_ = 1.0f, noteGainTarget_ = 1.0f;
    float lastGain_ = 0.0f;

    // Low
    bool isRoot_ = false;
    int lowNote_ = 36;
    float lowF0_ = 65.4f;
    float lowPos_ = 0.0f, lowRate_ = 0.0f, lastLowGain_ = 0.0f;
    WavetableOsc lowOsc_;
    int lowLevel_ = 0;

    // Camadas
    Layer warm_, vox_, strings_, texture_, air_, glass_;

    // Filtros
    LadderFilter ladL_, ladR_;     // corpo sintetizado (Warm + Vox)
    SvfStereo sampleTone_;         // samples reais: passa-baixa suave de 2 polos
    SvfStereo airTone_;            // Air: passa-baixa 12 dB/oitava (5,5–9 kHz)
    float lastCutoff_ = -1.0f, lastSampleCut_ = -1.0f, lastAirCut_ = -1.0f, lastDrive_ = -1.0f;

    // Movimento e evolução
    Rng rng_;
    SmoothRandom modPitch_, modAmp_, modCut_, modPan_, modBreath_, morphWarm_, morphVox_, morphAir_, evoCut_;
    float panL_ = 1.0f, panR_ = 1.0f;
};

} // namespace pad
