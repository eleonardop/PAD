// PAD Engine — gerenciamento de vozes e polifonia.
//
// - 48 vozes pré-alocadas (nenhuma alocação no callback).
// - Troca de acorde com "voice leading": notas comuns ao acorde anterior e ao novo
//   continuam soando na mesma voz (sem retrigger, sem dobrar o sinal); as demais saem e
//   entram por crossfade equal-power (sinais de notas diferentes são descorrelacionados).
// - Trocas rápidas e sucessivas: cada voz guarda a própria posição de fade; uma nota que
//   sai e volta é "revivida" a partir do ganho atual, sem saltos.
// - Voice stealing: limite lógico de (48 - 6) vozes; acima dele uma vítima recebe fade
//   rápido de 15 ms e a nova nota usa uma das 6 vozes de reserva. Prioridade da vítima:
//   vozes em Release/saída com menor ganho; depois as mais antigas e mais baixas.
//   Notas do acorde atual nunca são roubadas.
#pragma once

#include <array>
#include "Voice.h"
#include "MusicTheory.h"

namespace pad {

class VoiceManager {
public:
    static constexpr int kMaxVoices = 48;
    static constexpr int kReserveVoices = 6;

    void prepare(float sampleRate);

    void playChord(const Voicing& v, float attackSec, float crossfadeSec, float releaseSec, const RenderContext& ctx);
    void stopAll(float releaseSec);
    void panic(); // fade rápido de tudo (uso interno/testes)

    void render(const RenderContext& ctx, Buses& buses, int n);

    int activeCount() const;
    bool isPlaying() const { return playing_; }
    uint64_t stealCount() const { return steals_; }
    uint64_t hardStealCount() const { return hardSteals_; }
    const Voice& voice(int i) const { return voices_[static_cast<size_t>(i)]; }

private:
    int allocate(const bool* claimed);
    std::array<Voice, kMaxVoices> voices_;
    bool playing_ = false;
    uint64_t ageCounter_ = 0;
    uint64_t steals_ = 0, hardSteals_ = 0;
};

} // namespace pad
