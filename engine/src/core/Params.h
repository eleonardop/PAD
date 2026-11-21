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
    Air,           // 0..1  "ar": faixa de 10–16 kHz e abertura do ruído afinado da camada Air
    Low,           // 0..1  controle de graves do conjunto (0,5 = neutro)
    Width,         // 0..1.5 largura estéreo (0 = mono, 1 = natural)
    Movement,      // 0..1  movimento orgânico (deriva lenta de afinação, filtro, volume e pan)
    Detune,        // 0..1  desafinação entre osciladores (0 a 14 cents)
    // Espaço
    Reverb,        // 0..1  mixagem do reverb
    ReverbDecay,   // segundos (RT60)
    ReverbSize,    // 0..1
    ReverbDamping, // 0..1 (1 = cauda mais escura)
    Shimmer,       // 0..1  realimentação de oitava do reverb
    // Envelope / transições
    Attack,        // segundos
    Release,       // segundos
    CrossfadeTime, // segundos (troca de acorde/tonalidade)
    MasterVolume,  // 0..1
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
    };
    return kInfo[static_cast<int>(id)];
}

} // namespace pad
