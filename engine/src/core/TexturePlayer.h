// PAD Engine — reprodutor de texturas e transições (v0.3).
//
// Vozes leves, separadas das 48 vozes do pad: tocam zonas one-shot (piano suave, EP, vibrafone,
// pratos, reverses) ou, em swells harmônicos, zonas em loop (Glass). Cada voz: leitura Hermite
// em ponto fixo 32.32, envelope ataque/sustentação/release, passa-alta e passa-baixa SVF fixos
// por evento e pan de potência constante (sem atraso entre canais: seguro em mono).
//
// Tempo real: tudo pré-alocado; trigger() e render() são chamados só pelo thread de áudio.
#pragma once

#include <array>
#include <cstdint>
#include "Sources.h"
#include "../dsp/Svf.h"

namespace pad {

enum class TexBus : uint8_t { Tonal = 0, Metal = 1 };

struct TexEvent {
    SourceId source = SourceId::PianoSoft;
    int midi = 0;              // nota alvo (0 = não tonal: usa kind/kindIndex)
    const char* kind = nullptr;
    int kindIndex = 0;
    float level = 0.5f;        // ganho linear
    float pan = 0.0f;          // -1 (esquerda) .. 1 (direita)
    float attack = 0.003f;     // s
    float attackCurve = 1.0f;  // 1 = linear; >1 começa mais devagar (swell)
    float hold = -1.0f;        // s; <0 = até o fim da zona (one-shot) ou até releaseGroup()
    float release = 1.5f;      // s até −60 dB
    float startOffset = 0.0f;  // s dentro da zona
    float delay = 0.0f;        // s até começar (alinha o pico de swells mais curtos que o pedido)
    float lowpassHz = 18000.0f;
    float highpassHz = 20.0f;
    TexBus bus = TexBus::Tonal;
    uint8_t priority = 1;      // 0 brilho, 1 teclas, 2 prato, 3 swell
    uint8_t group = 0;         // 0 = livre; 1 = partes tonais do swell manual; 2 = bloom; 3 = microevento tonal
};

class TexturePlayer {
public:
    static constexpr int kVoices = 24;   // limite lógico
    static constexpr int kReserve = 4;   // vozes extras para não esperar o fade de um roubo

    void prepare(float sampleRate);
    // Dispara um evento; devolve false se a fonte ainda não está carregada.
    bool trigger(const TexEvent& e, const SourceLibrary& lib);
    void releaseGroup(uint8_t group, float seconds);
    // v0.4: solta as vozes do grupo cuja nota NÃO pertence ao novo acorde (máscara de 12 bits).
    void releaseGroupNotInMask(uint8_t group, uint16_t pcMask, float seconds);
    // v0.4: vozes do grupo ainda soando (para limitar a densidade).
    int activeInGroup(uint8_t group) const;
    void releaseAll(float seconds);
    void panic();

    // Soma nos barramentos (não zera).
    void render(float* tonalL, float* tonalR, float* metalL, float* metalR, int n);

    int activeCount() const;
    uint64_t stealCount() const { return steals_; }
    void countSourceUsage(uint16_t* counts) const;

private:
    enum class Stage : uint8_t { Free, Pending, Attack, Hold, Release };
    struct V {
        Stage stage = Stage::Free;
        SourceId source = SourceId::PianoSoft;
        const Zone* zone = nullptr;
        uint64_t pos = 0, inc = 0, len = 0;
        bool loop = false;
        float level = 0, gl = 1, gr = 1;
        float t = 0, attackStep = 0, curve = 1;      // t: progresso do ataque 0..1
        int holdLeft = -1;                           // amostras (-1 = indefinido)
        int delayLeft = 0;                           // amostras até começar
        float relMul = 0.999f;                       // multiplicador por amostra no release
        float env = 0;                               // ganho atual do envelope
        uint8_t priority = 0, group = 0;
        int midi = 0;
        TexBus bus = TexBus::Tonal;
        SvfStereo lp, hp;
        uint64_t age = 0;
    };
    int allocate(uint8_t priority);
    void startRelease(V& v, float seconds);

    std::array<V, kVoices + kReserve> v_;
    float sr_ = 48000.0f;
    uint64_t ageCounter_ = 0, steals_ = 0;
};

} // namespace pad
