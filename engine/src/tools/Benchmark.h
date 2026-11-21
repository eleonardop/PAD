// PAD Engine — teste de desempenho reproduzível (o mesmo código roda no computador, no CI
// ARM64 e dentro do app, no aparelho).
//
// Renderiza o motor fora do tempo real, em blocos do tamanho usado pelo Android, e mede o
// tempo de cada bloco em relação à duração do áudio que ele produz. 100% = o processador
// gastaria todo o tempo disponível; acima disso haveria falhas no som. O app só pode chamar
// esta função com a saída de áudio PARADA (o motor não pode ser processado por dois threads).
#pragma once

#include <string>
#include "PadEngine.h"

namespace pad {

struct BenchmarkOptions {
    double secondsPerCase = 12.0;  // áudio renderizado por caso
    int blockFrames = 192;         // tamanho típico de burst no Android (4 ms a 48 kHz)
    bool quick = false;            // só três casos (para o CI com sanitizers)
};

// Devolve um relatório em texto (português), uma linha por caso. Restaura os parâmetros
// que o motor tinha antes; as notas são interrompidas.
std::string runBenchmark(PadEngine& engine, const BenchmarkOptions& opt = {});

} // namespace pad
