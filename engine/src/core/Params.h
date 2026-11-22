// PAD Engine — parâmetros do motor (faixas seguras, valores padrão e suavização).
//
// Todos os valores estão em unidades "naturais" (segundos, 0..1 etc.). A interface
// Android lê esta mesma tabela (via JNI) para montar controles e presets.
#pragma once

#include <cstdint>

namespace pad {

enum class ParamId : int32_t {
    // Volumes de camada (0..1)
    LayerWarm = 0,
    LayerAir,
    LayerStrings,
    LayerShimmer,
    LayerLow,
    // Timbre
    Warmth,        // 0..1  calor: realce de médios-graves + saturação suave + agudos mais redondos
    Brightness,    // 0..1  brilho: abertura dos filtros das vozes + shelf de agudos
    Air,           // 0..1  abertura da camada Air (passa-baixa 5,5–9 kHz) e parte enviada ao reverb
    Low,           // 0..1  controle de graves do conjunto (0,5 = neutro)
    Width,         // 0..1.5 largura estéreo (0 = mono, 1 = natural)
    Movement,      // 0..1  movimento orgânico (deriva lenta de afinação, filtro, volume e pan)
    Detune,        // 0..1  diferença de afinação entre as leituras L e R das fontes (0 a 9 cents): largura viva
    // Espaço
    Reverb,        // 0..1  mixagem do reverb
    ReverbDecay,   // segundos (RT60)
    ReverbSize,    // 0..1
    ReverbDamping, // 0..1 (1 = cauda mais escura)
    Shimmer,       // 0..1  realimentação de oitava em volta do reverb
    // Envelope / transições
    Attack,        // segundos
    Release,       // segundos
    CrossfadeTime, // segundos (troca de acorde/tonalidade)
    MasterVolume,  // 0..1
    // ---- Acrescentados na versão 0.2 (no fim, para manter os ids anteriores)
    LayerVox,          // 0..1  coral PADsynth ("ah" <-> "oh")
    LayerTexture,      // 0..1  textura real (piano "Cathedral" ou órgão suave)
    Chorus,            // 0..1  ensemble de 3 fases no barramento
    Evolution,         // 0..1  macro: morph de timbre, filtro, respiração e movimento ao longo do tempo
    Intensity,         // 0..1  dinâmica: abre o filtro, puxa o morph para os timbres abertos, cresce o volume
    ReverbType,        // 0 = Clássico (FDN v0.1), 1 = Plate (Dattorro), 2 = Cloud (Cloud Seed)
    PreDelay,          // segundos
    EarlyReflections,  // 0..1
    Diffusion,         // 0..1
    StringsSource,     // 0 = cordas reais (VSCO 2 CE), 1 = cordas sintéticas (v0.1)
    TextureType,       // 0 = piano "Cathedral", 1 = órgão suave
    // ---- v0.3 (no fim, para manter os ids anteriores)
    LayerKeys,         // 0..1  notas esparsas de piano suave/EP geradas pelo maestro (TEXTURE)
    KeysType,          // 0 = piano suave (VCSL), 1 = EP (FM próprio)
    LayerSparkle,      // 0..1  notas de vibrafone no registro alto (SPARKLE)
    LayerMetal,        // 0..1  pratos de baqueta macia nas transições e no ambiente
    Tape,              // 0..1  caráter de fita (wow/flutter, saturação, perda de agudos)
    Activity,          // 0..1  frequência das notas de textura
    Bloom,             // 0..1  intensidade das transições automáticas
    BloomChance,       // 0..1  frequência das transições automáticas
    SwellTime,         // segundos até o pico do pre-swell manual
    // ---- v0.5 (no fim, para manter os ids anteriores)
    ToneCut,           // 0..1  abafar: passa-baixa de 4 polos no conjunto (0 = desligado, 1 = ~420 Hz)
    VoicingStyle,      // 0 = voicing padrão, 1 = quinta por baixo (pads de fundo: 5ª abaixo da fundamental)
    LayerGuitar,       // 0..1  swells de guitarra (microeventos do tipo guitarra)
    Count
};

constexpr int kParamCount = static_cast<int>(ParamId::Count);

struct ParamInfo {
    const char* name;
    float minValue;
    float maxValue;
    float defaultValue;
    float smoothingSeconds; // constante de tempo da suavização no áudio
};

inline const ParamInfo& paramInfo(ParamId id) {
    static const ParamInfo kInfo[kParamCount] = {
        {"layer_warm", 0.0f, 1.0f, 0.80f, 0.05f},
        {"layer_air", 0.0f, 1.0f, 0.35f, 0.05f},
        {"layer_strings", 0.0f, 1.0f, 0.55f, 0.05f},
        {"layer_shimmer", 0.0f, 1.0f, 0.25f, 0.05f},
        {"layer_low", 0.0f, 1.0f, 0.45f, 0.05f},
        {"warmth", 0.0f, 1.0f, 0.55f, 0.08f},
        {"brightness", 0.0f, 1.0f, 0.45f, 0.08f},
        {"air", 0.0f, 1.0f, 0.40f, 0.08f},
        {"low", 0.0f, 1.0f, 0.50f, 0.08f},
        {"width", 0.0f, 1.5f, 1.00f, 0.08f},
        {"movement", 0.0f, 1.0f, 0.45f, 0.10f},
        {"detune", 0.0f, 1.0f, 0.40f, 0.10f},
        {"reverb", 0.0f, 1.0f, 0.45f, 0.08f},
        {"reverb_decay", 1.0f, 14.0f, 6.0f, 0.20f},
        {"reverb_size", 0.0f, 1.0f, 0.70f, 0.20f},
        {"reverb_damping", 0.0f, 1.0f, 0.45f, 0.10f},
        {"shimmer", 0.0f, 1.0f, 0.25f, 0.10f},
        {"attack", 0.01f, 20.0f, 2.5f, 0.0f},
        {"release", 0.05f, 30.0f, 4.0f, 0.0f},
        {"crossfade_time", 0.05f, 10.0f, 1.8f, 0.0f},
        {"master_volume", 0.0f, 1.0f, 0.80f, 0.05f},
        {"layer_vox", 0.0f, 1.0f, 0.0f, 0.05f},
        {"layer_texture", 0.0f, 1.0f, 0.0f, 0.05f},
        {"chorus", 0.0f, 1.0f, 0.5f, 0.08f},
        {"evolution", 0.0f, 1.0f, 0.5f, 0.15f},
        {"intensity", 0.0f, 1.0f, 0.5f, 0.25f},
        {"reverb_type", 0.0f, 2.0f, 2.0f, 0.0f},
        {"pre_delay", 0.0f, 0.12f, 0.03f, 0.10f},
        {"early_reflections", 0.0f, 1.0f, 0.35f, 0.10f},
        {"diffusion", 0.0f, 1.0f, 0.75f, 0.10f},
        {"strings_source", 0.0f, 1.0f, 0.0f, 0.0f},
        {"texture_type", 0.0f, 1.0f, 0.0f, 0.0f},
        {"layer_keys", 0.0f, 1.0f, 0.0f, 0.05f},
        {"keys_type", 0.0f, 1.0f, 0.0f, 0.0f},
        {"layer_sparkle", 0.0f, 1.0f, 0.0f, 0.05f},
        {"layer_metal", 0.0f, 1.0f, 0.0f, 0.05f},
        {"tape", 0.0f, 1.0f, 0.0f, 0.10f},
        {"activity", 0.0f, 1.0f, 0.0f, 0.10f},
        {"bloom", 0.0f, 1.0f, 0.0f, 0.10f},
        {"bloom_chance", 0.0f, 1.0f, 0.0f, 0.10f},
        {"swell_time", 1.5f, 8.0f, 4.0f, 0.0f},
        {"tone_cut", 0.0f, 1.0f, 0.0f, 0.15f},
        {"voicing_style", 0.0f, 1.0f, 0.0f, 0.0f},
        {"layer_guitar", 0.0f, 1.0f, 0.0f, 0.05f},
    };
    return kInfo[static_cast<int>(id)];
}

} // namespace pad
