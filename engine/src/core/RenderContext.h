// PAD Engine — dados compartilhados entre o motor e as vozes durante a renderização.
#pragma once

#include "../dsp/Wavetable.h"
#include "SampleBank.h"

namespace pad {

constexpr int kSubBlock = 32; // tamanho do sub-bloco de controle (0,67 ms a 48 kHz)

struct LayerTables {
    Wavetable warmA;   // corpo quente (série harmônica completa, queda suave)
    Wavetable warmB;   // corpo "oco" (ímpares realçados) — dá caráter ao par desafinado
    Wavetable shimmer; // quase senoidal, poucos harmônicos (brilho limpo)
    Wavetable low;     // grave redondo: fundamental + 2º/3º harmônicos discretos
};

// Valores de parâmetros já suavizados para o sub-bloco atual.
struct RenderContext {
    float sampleRate = 48000.0f;
    float controlRate = 48000.0f / kSubBlock;
    const LayerTables* tables = nullptr;
    const SampleBank* bank = nullptr;

    float layerWarm = 0, layerAir = 0, layerStrings = 0, layerShimmer = 0, layerLow = 0;
    float brightness = 0.5f, air = 0.5f, movement = 0.5f, detune = 0.5f;
};

// Barramentos de saída das vozes (estéreo). O motor aplica os efeitos depois.
struct Buses {
    float* padL; float* padR;     // Warm + Strings + Air + Shimmer
    float* shimL; float* shimR;   // cópia da camada Shimmer (envio extra ao reverb shimmer)
    float* lowL; float* lowR;     // camada Low (mono nos dois canais)
};

} // namespace pad
