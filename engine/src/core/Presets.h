// PAD Engine — presets de fábrica (identidades sonoras).
#pragma once

#include "Params.h"

namespace pad {

// Paleta de microeventos (v0.4): pesos relativos de cada tipo de acontecimento e densidade-alvo.
// Faz parte da identidade do preset (não é parâmetro do operador). Paleta vazia = comportamento
// da v0.3 (notas de teclas/vibrafone pelos níveis das camadas).
struct MicroPalette {
    float bell = 0, piano = 0, pluck = 0, harmonic = 0, reverse = 0, metal = 0, bow = 0, tapeGust = 0;
    float perMinute = 0;   // eventos por minuto com Atividade 0,5 (o dobro com Atividade 1)
    float highBias = 0;    // −1 registro mais baixo do permitido … +1 mais alto
    float guitar = 0;      // v0.5: swells de guitarra com eco
    bool empty() const { return bell + piano + pluck + harmonic + reverse + metal + bow + tapeGust + guitar <= 0.0f; }
};

struct Preset {
    const char* name;
    const char* description;
    float values[kParamCount];
    // Ajuste de nível (dB) para que todos os presets soem com loudness parecido (medido em
    // LUFS com a progressão de teste; ver docs/RELATORIO_ETAPA2.md). Não é um parâmetro do
    // operador: o Volume geral continua sendo dele.
    float trimDb = 0.0f;
    const char* category = "";   // v0.4: FOUNDATION, ATMOSPHERE, CHARACTER, TEXTURES ("" = versão anterior)
    MicroPalette micro{};
};

// v0.4: presets da biblioteca principal (os demais ficam como "versões anteriores").
bool presetIsCurrent(int index);

int presetCount();
const Preset& preset(int index);

} // namespace pad
