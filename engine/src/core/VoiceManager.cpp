#include "VoiceManager.h"

namespace pad {

void VoiceManager::prepare(float sampleRate) {
    for (int i = 0; i < kMaxVoices; ++i) voices_[static_cast<size_t>(i)].prepare(sampleRate, static_cast<uint32_t>(i));
    playing_ = false;
    ageCounter_ = 0;
    steals_ = hardSteals_ = 0;
}

int VoiceManager::activeCount() const {
    int c = 0;
    for (const auto& v : voices_) c += v.isFree() ? 0 : 1;
    return c;
}

int VoiceManager::allocate(const bool* claimed) {
    int used = 0;
    for (const auto& v : voices_) used += v.isFree() ? 0 : 1;

    // Acima do limite lógico: escolhe uma vítima e faz fade rápido (a reserva cobre a nova nota).
    if (used >= kMaxVoices - kReserveVoices) {
        int victim = -1;
        float best = 1e9f;
        for (int i = 0; i < kMaxVoices; ++i) {
            const Voice& v = voices_[static_cast<size_t>(i)];
            if (v.isFree() || claimed[i] || v.state() == Voice::State::Stealing) continue;
            float score = v.currentGain();
            if (v.state() == Voice::State::Active) score += 10.0f;                 // evita sustentadas
            if (v.isRoot() && v.state() == Voice::State::Active) score += 5.0f;   // a fundamental é a mais importante
            score -= static_cast<float>(ageCounter_ - v.age()) * 1e-6f;           // desempate: mais antiga
            if (score < best) { best = score; victim = i; }
        }
        if (victim >= 0) { voices_[static_cast<size_t>(victim)].steal(); ++steals_; }
    }

    for (int i = 0; i < kMaxVoices; ++i)
        if (voices_[static_cast<size_t>(i)].isFree()) return i;

    // Caso patológico: nenhuma voz livre nem na reserva. Reaproveita a voz em roubo
    // de menor ganho (ela já está quase em silêncio).
    int idx = -1;
    float best = 1e9f;
    for (int i = 0; i < kMaxVoices; ++i) {
        const Voice& v = voices_[static_cast<size_t>(i)];
        if (claimed[i]) continue;
        const float s = v.currentGain() + (v.state() == Voice::State::Stealing ? 0.0f : 10.0f);
        if (s < best) { best = s; idx = i; }
    }
    if (idx >= 0) ++hardSteals_;
    return idx;
}

void VoiceManager::playChord(const Voicing& v, float attackSec, float crossfadeSec, float releaseSec,
                             const RenderContext& ctx) {
    (void)releaseSec;
    bool claimed[kMaxVoices] = {false};
    bool noteDone[kMaxChordNotes] = {false};
    const float noteGain = 0.9f / std::sqrt(static_cast<float>(std::max(v.count, 1)));

    // Havia acorde soando? Então a troca é por crossfade; senão, entrada pelo Attack.
    bool crossfadeMode = false;
    for (const auto& vo : voices_) if (vo.state() == Voice::State::Active) { crossfadeMode = true; break; }

    // 1) Notas comuns: mantém (Active), revive (FadingOut) ou reataca (Releasing).
    for (int k = 0; k < v.count; ++k) {
        const int note = v.notes[k];
        const bool isRoot = (note == v.rootNote);
        int found = -1;
        // Preferência: Active > FadingOut > Releasing (a mais "viva").
        const Voice::State order[3] = {Voice::State::Active, Voice::State::FadingOut, Voice::State::Releasing};
        for (Voice::State st : order) {
            for (int i = 0; i < kMaxVoices && found < 0; ++i) {
                const Voice& vo = voices_[static_cast<size_t>(i)];
                if (!claimed[i] && vo.state() == st && vo.note() == note) found = i;
            }
            if (found >= 0) break;
        }
        if (found < 0) continue;
        Voice& vo = voices_[static_cast<size_t>(found)];
        claimed[found] = true;
        noteDone[k] = true;
        if (vo.state() == Voice::State::FadingOut) vo.reviveFade(crossfadeSec);
        else if (vo.state() == Voice::State::Releasing) vo.reAttack(crossfadeMode ? crossfadeSec : attackSec);
        vo.setRole(isRoot, v.lowNote, noteGain, crossfadeSec);
    }

    // 2) Vozes ativas que não pertencem ao novo acorde saem por crossfade.
    for (int i = 0; i < kMaxVoices; ++i) {
        Voice& vo = voices_[static_cast<size_t>(i)];
        if (!claimed[i] && vo.state() == Voice::State::Active) vo.fadeOut(crossfadeSec);
    }

    // 3) Notas novas.
    for (int k = 0; k < v.count; ++k) {
        if (noteDone[k]) continue;
        const int idx = allocate(claimed);
        if (idx < 0) continue;
        claimed[idx] = true;
        const int note = v.notes[k];
        voices_[static_cast<size_t>(idx)].start(note, note == v.rootNote, v.lowNote, noteGain, !crossfadeMode, attackSec,
                                                crossfadeSec, ++ageCounter_, ctx);
    }
    playing_ = true;
}

void VoiceManager::stopAll(float releaseSec) {
    for (auto& vo : voices_) {
        if (vo.state() == Voice::State::Active || vo.state() == Voice::State::FadingOut) vo.release(releaseSec);
    }
    playing_ = false;
}

void VoiceManager::panic() {
    for (auto& vo : voices_) vo.steal(0.02f);
    playing_ = false;
}

void VoiceManager::countSourceUsage(uint16_t* counts) const {
    for (int i = 0; i < kSourceCount; ++i) counts[i] = 0;
    for (const auto& v : voices_) {
        const uint32_t m = v.sourcesInUse();
        if (!m) continue;
        for (int i = 0; i < kSourceCount; ++i) counts[i] = static_cast<uint16_t>(counts[i] + ((m >> i) & 1u));
    }
}

void VoiceManager::render(const RenderContext& ctx, Buses& buses, int n) {
    for (auto& vo : voices_) vo.render(ctx, buses, n);
}

} // namespace pad
