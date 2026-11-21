// PAD Engine — envelope de amplitude para pads (Attack -> Sustain -> Release).
//
// Curvas:
//  - Attack: smoothstep (3t^2 - 2t^3) partindo do nível atual. Começa e termina com
//    derivada zero: entrada "respirada", sem clique, mesmo com attack curto.
//  - Release: (1 - t)^3 a partir do nível atual. Cai de forma parecida com uma
//    exponencial (perceptivamente natural) mas chega exatamente a zero no tempo
//    pedido, com derivada zero no fim — a voz pode ser liberada sem degrau.
// Retrigger e release no meio do attack partem do nível corrente (sem saltos).
#pragma once

#include "DspMath.h"

namespace pad {

class PadEnvelope {
public:
    enum class Stage : uint8_t { Idle, Attack, Sustain, Release };

    void setSampleRate(float sr) { sr_ = sr; }

    void startAttack(float seconds) {
        start_ = level_;
        pos_ = 0.0f;
        inc_ = 1.0f / std::max(seconds * sr_, 1.0f);
        stage_ = Stage::Attack;
    }
    void startSustain() { level_ = 1.0f; stage_ = Stage::Sustain; }
    void startRelease(float seconds) {
        if (stage_ == Stage::Idle) return;
        start_ = level_;
        pos_ = 0.0f;
        inc_ = 1.0f / std::max(seconds * sr_, 1.0f);
        stage_ = Stage::Release;
    }
    void reset() { stage_ = Stage::Idle; level_ = 0.0f; }

    // Avança n amostras e devolve o nível ao final (usado em taxa de sub-bloco,
    // com interpolação linear dentro do sub-bloco pelo chamador).
    float advance(int n) {
        switch (stage_) {
            case Stage::Attack: {
                pos_ += inc_ * n;
                if (pos_ >= 1.0f) { level_ = 1.0f; stage_ = Stage::Sustain; }
                else level_ = start_ + (1.0f - start_) * smoothStep(pos_);
                break;
            }
            case Stage::Release: {
                pos_ += inc_ * n;
                if (pos_ >= 1.0f) { level_ = 0.0f; stage_ = Stage::Idle; }
                else { const float r = 1.0f - pos_; level_ = start_ * r * r * r; }
                break;
            }
            case Stage::Sustain: level_ = 1.0f; break;
            case Stage::Idle: level_ = 0.0f; break;
        }
        return level_;
    }

    float level() const { return level_; }
    Stage stage() const { return stage_; }
    bool isIdle() const { return stage_ == Stage::Idle; }
    bool isReleasing() const { return stage_ == Stage::Release; }

private:
    float sr_ = 48000.0f;
    float level_ = 0.0f, start_ = 0.0f, pos_ = 0.0f, inc_ = 0.0f;
    Stage stage_ = Stage::Idle;
};

} // namespace pad
