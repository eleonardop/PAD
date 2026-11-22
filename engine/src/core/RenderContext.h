// PAD Engine — dados compartilhados entre o motor e as vozes durante a renderização.
#pragma once

#include "../dsp/Wavetable.h"
#include "Sources.h"

namespace pad {

constexpr int kSubBlock = 32; // tamanho do sub-bloco de controle (0,67 ms a 48 kHz)

// Wavetable da camada Low (grave limpo: fundamental + 2º/3º harmônicos discretos).
struct LayerTables {
    Wavetable low;
};

// Valores de parâmetros já suavizados para o sub-bloco atual.
struct RenderContext {
    float sampleRate = 48000.0f;
    float controlRate = 48000.0f / kSubBlock;
    const LayerTables* tables = nullptr;
    const SourceLibrary* sources = nullptr;

    float layerWarm = 0, layerVox = 0, layerStrings = 0, layerTexture = 0, layerAir = 0, layerShimmer = 0, layerLow = 0;
    float brightness = 0.5f, warmth = 0.5f, air = 0.5f, movement = 0.5f, detune = 0.5f;
    float evolution = 0.5f, intensity = 0.5f;
    // Moduladores lentos comuns a todas as vozes (−1..1): a Evolução move o conjunto inteiro,
    // como uma respiração da seção, em vez de variações independentes que se cancelam.
    float evoMorph = 0.0f, evoMorph2 = 0.0f, evoCut = 0.0f;
    // v0.3: texturas, transições e caráter
    float layerKeys = 0, layerSparkle = 0, layerMetal = 0, tape = 0;
    float activity = 0, bloom = 0, bloomChance = 0, swellTime = 4.0f;
    int keysType = 0;       // 0 = piano suave, 1 = EP
    // v0.4: paleta de microeventos do preset atual (cópia por bloco de controle)
    float microBell = 0, microPiano = 0, microPluck = 0, microHarmonic = 0, microReverse = 0, microMetal = 0;
    float microBow = 0, microTapeGust = 0, microPerMinute = 0, microHighBias = 0;
    // v0.5
    float microGuitar = 0, layerGuitar = 0;
    int stringsSource = 0;  // 0 = reais, 1 = sintéticas
    int textureType = 0;    // 0 = piano "Cathedral", 1 = órgão
};

// Barramentos de saída das vozes (estéreo). O motor aplica ensemble, espaço e mixagem depois.
struct Buses {
    float* padL; float* padR;     // corpo: Warm + Vox + Strings + Texture (passa pelo ensemble)
    float* airL; float* airR;     // camada Air (parte vai direto, parte só para o reverb)
    float* shimL; float* shimR;   // camada Shimmer (Glass)
    float* lowL; float* lowR;     // camada Low (mono nos dois canais)
};

} // namespace pad
