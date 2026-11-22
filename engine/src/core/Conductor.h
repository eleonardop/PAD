// PAD Engine — maestro (v0.3): comportamento musical ligado à harmonia e ao tempo.
//
// Roda no thread de áudio, uma vez por sub-bloco de controle, sem alocar memória. Não gera
// áudio: decide quando e o quê. Conhece o acorde atual e o anterior, a idade do "organismo"
// (que não volta a zero em mudanças pequenas), a Evolução e os controles do preset.
//  - ciclo de vida do acorde: Establishment → Expansion → Transformation → Sustain;
//  - segunda camada (cordas, coral, glass) que cresce com a idade;
//  - notas de textura esparsas (piano/EP/vibrafone) só com notas do acorde;
//  - bloom pós-ataque condicionado à distância harmônica e à duração do acorde anterior;
//  - pre-swell manual (comando Swell), com pico no fim de SwellTime.
// Regras completas: docs/sound-design/MUSICAL_DESIGN.md.
#pragma once

#include <array>
#include <cstdint>
#include "MusicTheory.h"
#include "RenderContext.h"
#include "TexturePlayer.h"
#include "../dsp/Random.h"

namespace pad {

enum class LifeStage : uint8_t { Silent = 0, Establishment, Expansion, Transformation, Sustain, Dissolve };

struct ConductorOut {
    float strings = 1.0f, vox = 1.0f, glass = 1.0f;  // ganhos extras das camadas (segunda camada)
    float bright = 0.0f;                              // soma no Brilho
    float send = 1.0f;                                // multiplicador do envio ao reverb
    float width = 0.0f;                               // largura extra (fração do lado)
    float tapeGust = 0.0f;                            // v0.4: "imperfeição" de fita (0..1)
    LifeStage stage = LifeStage::Silent;
    float age = 0.0f;
};

// instrument: 0 teclas (v0.3), 1 brilho (v0.3); v0.4: 10 sino, 11 piano agudo, 12 pluck,
// 13 harmônico, 14 reverse curto, 15 prato, 16 arco das cordas (sem nota), 17 fita (sem nota).
struct NoteLogEntry { float time; int16_t midi; int8_t rootPc; int8_t chordType; uint8_t instrument; int8_t keyPc; float absTime; };

enum class MicroType : uint8_t { Bell = 0, Piano, Pluck, Harmonic, Reverse, Metal, Bow, TapeGust, Guitar, Count };

class Conductor {
public:
    void prepare(float controlRate, uint64_t seed = 0x5eed);
    void reset();

    // Chamados pelo thread de áudio ao executar comandos.
    void onChord(const Voicing& v, ChordType type, int rootPc);
    void onStop();
    void onSwell();
    void reseed(uint64_t seed) { rng_.setSeed(seed); }

    void tick(const RenderContext& ctx, const SourceLibrary& lib, TexturePlayer& tex);
    const ConductorOut& out() const { return out_; }

    // 0 = mesmo acorde, 1 = muito distante (notas comuns + círculo de quintas + modo).
    static float harmonicDistance(int rootA, ChordType ta, int rootB, ChordType tb);

    // Estatísticas e registro (testes e bancada).
    uint32_t noteEvents() const { return noteEvents_; }
    uint32_t blooms() const { return blooms_; }
    uint32_t swells() const { return swells_; }
    uint32_t metalEvents() const { return metalEvents_; }
    static constexpr int kLog = 512;
    int logCount() const { return logCount_; }
    const NoteLogEntry& logAt(int i) const { return log_[static_cast<size_t>(i % kLog)]; }
    double now() const { return now_; }
    // v0.4: tonalidade estimada pelos acordes recentes (−1 = sem confiança) e contagem por tipo.
    int estimatedKey() const { return keyPc_; }
    uint32_t microCount(MicroType t) const { return microCounts_[static_cast<size_t>(t)]; }
    uint32_t microTotal() const { uint32_t n = 0; for (auto c : microCounts_) n += c; return n; }

private:
    void startBloom(const RenderContext& ctx, const SourceLibrary& lib, TexturePlayer& tex, float strength, float d);
    void startSwell(const RenderContext& ctx, const SourceLibrary& lib, TexturePlayer& tex);
    void textureNote(const RenderContext& ctx, const SourceLibrary& lib, TexturePlayer& tex);
    int chooseNote(int lo, int hi, bool allowNinth, int center);
    float nextInterval(const RenderContext& ctx);
    void logNote(int midi, uint8_t instrument);
    // v0.4: microeventos
    void updateKey();
    uint16_t colorMask(int midiLo) const;
    int chooseGuitarNote(int lo, int hi);
    int chooseMicroNote(int lo, int hi, bool colors, int center);
    void microEvent(const RenderContext& ctx, const SourceLibrary& lib, TexturePlayer& tex);
    float microInterval(const RenderContext& ctx);

    float dt_ = 1.0f / 1500.0f;
    double now_ = 0.0;
    Rng rng_;

    // Acorde
    bool playing_ = false, hasChord_ = false, chordPending_ = false;
    int rootPc_ = 0, prevRootPc_ = 0;
    ChordType type_ = ChordType::Pad5, prevType_ = ChordType::Pad5;
    uint16_t pcMask_ = 0;
    double chordStart_ = 0.0;
    float age_ = 0.0f;
    float lastDistance_ = 0.0f, lastPrevDuration_ = 0.0f;

    // Eventos
    double nextTexAt_ = 1e30, lastBloomAt_ = -1e30, nextMetalAt_ = 1e30;
    int lastNote_ = 0, repeatCount_ = 0, notesThisChord_ = 0;
    float lastPanSide_ = 1.0f;
    double bloomStart_ = -1e30;
    float bloomStrength_ = 0.0f;
    bool swellPending_ = false, swellActive_ = false;
    double swellEnd_ = 0.0;

    // v0.4: microeventos e contexto harmônico
    std::array<float, 12> pcWeight_{};
    int keyPc_ = -1;
    float chordDurEma_ = 6.0f;
    double nextMicroAt_ = 0.0, lastMicroAt_ = -1e30;   // nextMicroAt_: o mais cedo possível no acorde
    float microClock_ = 0.0f, microTarget_ = 0.0f;
    std::array<double, static_cast<size_t>(MicroType::Count)> lastTypeAt_{};
    int lastType_ = -1, sameTypeRun_ = 0;
    double bowStart_ = -1e30, gustStart_ = -1e30;
    float bowStrength_ = 0.0f;
    std::array<uint32_t, static_cast<size_t>(MicroType::Count)> microCounts_{};

    // Saídas suavizadas
    ConductorOut out_;
    uint32_t noteEvents_ = 0, blooms_ = 0, swells_ = 0, metalEvents_ = 0;
    std::array<NoteLogEntry, kLog> log_{};
    int logCount_ = 0;
};

} // namespace pad
