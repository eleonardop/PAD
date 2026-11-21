// pad_bench — executa o teste de desempenho do motor fora do app (computador ou CI ARM64).
// Uso: pad_bench [--rapido] [--bloco N]
#include <cstdio>
#include <cstring>
#include <memory>
#include "PadEngine.h"
#include "core/Sources.h"
#include "tools/Benchmark.h"

int main(int argc, char** argv) {
    pad::BenchmarkOptions o;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--rapido")) { o.quick = true; o.secondsPerCase = 4.0; }
        else if (!std::strcmp(argv[i], "--bloco") && i + 1 < argc) o.blockFrames = std::atoi(argv[++i]);
    }
    pad::PadEngine e;
    e.setAssetProvider(std::make_shared<pad::FileAssetProvider>(PAD_ASSETS_DIR));
    if (!e.prepare(48000.0f)) { std::fprintf(stderr, "falha ao preparar o motor\n"); return 1; }
    std::fputs(pad::runBenchmark(e, o).c_str(), stdout);
    return 0;
}
