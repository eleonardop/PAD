// PAD Engine — uma voz = uma nota do acorde, renderizando todas as camadas.
//
// Ganho final da voz = envelope (Attack/Release) x fade de crossfade (equal-power)
//                      x fade de roubo (voice stealing) x ganho da nota x movimento.
// Todos os ganhos são calculados por sub-bloco e interpolados linearmente por amostra,
// então nenhuma mudança de estado causa degrau no sinal.
#pragma once

#include <cstdint>
#include "RenderContext.h"
#include "../dsp/Envelope.h"
#include "../dsp/Svf.h"
#include "../dsp/Random.h"

namespace pad {

class Voice {
public:
    enum class State : uint8_t { Free, Active, FadingOut, Releasing, Stealing };

    void prepare(float sampleRate, uint32_t index);

    // Inicia a nota. useAttack=true: entra pelo envelope (início do zero).
    // useAttack=false: envelope já sustentado e entrada pelo crossfade (troca de acorde).
    void start(int note, bool isRoot, int lowNote, float noteGain, bool useAttack, float attackSec, float fadeSec,
               uint64_t age, const RenderContext& ctx);

    void fadeOut(float seconds);                 // sai do acorde (crossfade)
    void reviveFade(float seconds);              // volta ao acorde durante o crossfade
    void reAttack(float attackSec);              // volta durante o release (a partir do nível atual)
    void release(float seconds);                 // parada normal (Release)
    void steal(float seconds = 0.015f);          // roubo rápido e sem clique
    void setRole(bool isRoot, int lowNote, float noteGain, float fadeSec);

    // Soma n (<= kSubBlock) amostras nos barramentos.
    void render(const RenderContext& ctx, Buses& out, int n);

    State state() const { return state_; }
    bool isFree() const { return state_ == State::Free; }
    int note() const { return note_; }
    bool isRoot() const { return isRoot_; }
    uint64_t age() const { return age_; }
    float currentGain() const { return lastGain_; }

private:
    float computeGain(int n);   // avança envelope/fades e devolve o ganho no fim do sub-bloco
    void free();

    float sr_ = 48000.0f;
    uint32_t index_ = 0;
    State state_ = State::Free;
    int note_ = 60;
    float f0_ = 261.6f;
    uint64_t age_ = 0;

    PadEnvelope env_;
    float fadePos_ = 1.0f;      // 0..1, ganho = eqPower(fadePos_)
    float fadeRate_ = 0.0f;     // por amostra (negativo = saindo)
    float stealGain_ = 1.0f;
    float stealRate_ = 0.0f;
    float noteGain_ = 1.0f, noteGainTarget_ = 1.0f;
    float lastGain_ = 0.0f;
    float lastLowGain_ = 0.0f;

    // Camada Low (só na fundamental do acorde), com rampa própria para trocas de papel.
    bool isRoot_ = false;
    int lowNote_ = 36;
    float lowF0_ = 65.4f;
    float lowPos_ = 0.0f, lowRate_ = 0.0f;
    WavetableOsc lowOsc_;
    int lowLevel_ = 0;

    // Warm
    WavetableOsc warm_[3];
    int warmLevel_ = 0;
    SvfStereo warmFilter_;

    // Strings / Texture
    const SampleZone* zone_ = nullptr;
    double sRatio_ = 1.0;
    double sPosL_ = 0.0, sPosR_ = 0.0;

    // Air (ruído afinado em harmônicos da nota)
    Rng noise_;
    SvfCoeffs airC_[2];
    SvfState airL_[2], airR_[2];
    float airCenterKey_ = -1.0f;

    // Shimmer (oitava e 12ª acima)
    WavetableOsc shim_[2];
    int shimLevel_[2] = {0, 0};

    // Movimento orgânico
    SmoothRandom modPitch_, modAmp_, modCut_, modPan_, modAir_;
    float panL_ = 1.0f, panR_ = 1.0f;
    float lastCutoff_ = -1.0f;
};

} // namespace pad
