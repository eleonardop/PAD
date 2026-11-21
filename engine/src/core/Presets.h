// PAD Engine — presets de fábrica (identidades sonoras).
#pragma once

#include "Params.h"

namespace pad {

struct Preset {
    const char* name;
    const char* description;
    float values[kParamCount];
    // Ajuste de nível (dB) para que todos os presets soem com loudness parecido (medido em
    // LUFS com a progressão de teste; ver docs/RELATORIO_ETAPA2.md). Não é um parâmetro do
    // operador: o Volume geral continua sendo dele.
    float trimDb = 0.0f;
};

int presetCount();
const Preset& preset(int index);

} // namespace pad
