// PAD Engine — cordas sintéticas da versão 0.1 (reserva) e utilidades de loop.
//
// Ensemble de cordas por síntese aditiva (4 "músicos", até 40 parciais por nota, deriva
// lenta, vibrato de ensemble). Continua disponível como alternativa às cordas reais
// (VSCO 2 CE) enquanto elas são validadas de ouvido. O bake é periódico por construção:
// todas as frequências e moduladores são múltiplos inteiros de 1/T, então o loop é contínuo.
#pragma once

#include <cstdint>
#include <vector>
#include "Zone.h"

namespace pad {

void bakeSynthStrings(ZoneBank& out, float sampleRate, uint64_t seed = 0xC0FFEE, int firstNote = 34, int lastNote = 90,
                      int step = 4, float loopSeconds = 6.0f);

// Torna [0, loopLen) um loop contínuo usando o trecho seguinte para o crossfade, com lei
// dependente da correlação medida (ver dsp/Crossfade.h). Devolve a correlação.
float makeSeamlessLoop(const std::vector<float>& in, int loopLen, int xfadeLen, std::vector<float>& out);

} // namespace pad
