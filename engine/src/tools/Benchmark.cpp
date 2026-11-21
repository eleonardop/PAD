#include "tools/Benchmark.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>
#include "dsp/DspMath.h"

namespace pad {

namespace {

struct CaseResult {
    double meanPct = 0, p99Pct = 0, maxPct = 0, loadSeconds = 0;
    int maxVoices = 0;
    size_t memBytes = 0;
};

// Progressão G–D–Em–C com troca a cada 3 s (crossfades constantes: o pior caso real de vozes).
CaseResult runCase(PadEngine& e, const BenchmarkOptions& o) {
    CaseResult r;
    const auto tl = std::chrono::steady_clock::now();
    e.waitForSources(60.0);
    r.loadSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - tl).count();
    const int bs = o.blockFrames;
    std::vector<float> buf(static_cast<size_t>(bs) * 2);
    const double budget = static_cast<double>(bs) / 48000.0;
    const int blocks = static_cast<int>(o.secondsPerCase * 48000.0 / bs);
    std::vector<double> t(static_cast<size_t>(blocks));
    const int roots[4] = {7, 2, 4, 0};
    const ChordType types[4] = {ChordType::Major, ChordType::Major, ChordType::Minor, ChordType::Major};
    int chord = 0;
    const int perChord = static_cast<int>(3.0 * 48000 / bs);
    // 6 s de silêncio deixam morrer a cauda do caso anterior (inclusive a de um reverb que
    // acabou de ser trocado, que continua rodando até silenciar); depois 1 s de aquecimento.
    for (int b = 0; b < static_cast<int>(6 * 48000 / bs); ++b) e.process(buf.data(), bs);
    e.playChord(roots[0], types[0]);
    for (int b = 0; b < static_cast<int>(48000 / bs); ++b) e.process(buf.data(), bs);
    for (int b = 0; b < blocks; ++b) {
        if (b > 0 && b % perChord == 0) { chord = (chord + 1) % 4; e.playChord(roots[chord], types[chord]); }
        const auto t0 = std::chrono::steady_clock::now();
        e.process(buf.data(), bs);
        t[static_cast<size_t>(b)] = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() / budget * 100.0;
        if (b % 64 == 0) r.maxVoices = std::max(r.maxVoices, e.readStats().activeVoices);
    }
    double sum = 0;
    for (double v : t) sum += v;
    r.meanPct = sum / blocks;
    std::vector<double> s = t;
    std::sort(s.begin(), s.end());
    r.p99Pct = s[static_cast<size_t>(0.99 * (s.size() - 1))];
    r.maxPct = s.back();
    r.memBytes = e.readStats().sourcesMemoryBytes;
    e.panic();
    for (int b = 0; b < static_cast<int>(0.2 * 48000 / bs); ++b) e.process(buf.data(), bs);
    return r;
}

} // namespace

std::string runBenchmark(PadEngine& e, const BenchmarkOptions& o) {
    ScopedNoDenormals nd;
    float saved[kParamCount];
    for (int i = 0; i < kParamCount; ++i) saved[i] = e.getParam(static_cast<ParamId>(i));

    std::string out;
    char line[256];
    std::snprintf(line, sizeof line, "Blocos de %d amostras (%.1f ms). Valores = tempo de CPU / duração do bloco.\n",
                  o.blockFrames, o.blockFrames / 48.0);
    out += line;
    out += "caso                         média   p99    pior   vozes  carga das fontes  memória\n";
    auto report = [&](const char* name, const CaseResult& r) {
        std::snprintf(line, sizeof line, "%-27s %5.1f%% %5.1f%% %6.1f%%  %3d    %5.2f s          %5.0f MB\n", name, r.meanPct,
                      r.p99Pct, r.maxPct, r.maxVoices, r.loadSeconds, r.memBytes / 1e6);
        out += line;
    };

    const int n = o.quick ? 1 : presetCount();
    for (int p = 0; p < n; ++p) {
        e.applyPreset(p);
        char name[64];
        std::snprintf(name, sizeof name, "preset %s", preset(p).name);
        report(name, runCase(e, o));
    }
    const char* rvNames[3] = {"Warm + reverb Clássico", "Warm + reverb Plate", "Warm + reverb Cloud"};
    for (int k = o.quick ? 1 : 0; k < 3; ++k) {
        e.applyPreset(0);
        e.setParam(ParamId::ReverbType, static_cast<float>(k));
        report(rvNames[k], runCase(e, o));
    }
    // Pior caso: todas as camadas no máximo, chorus, shimmer e Cloud.
    e.applyPreset(0);
    for (ParamId id : {ParamId::LayerWarm, ParamId::LayerAir, ParamId::LayerStrings, ParamId::LayerShimmer, ParamId::LayerLow,
                       ParamId::LayerVox, ParamId::LayerTexture, ParamId::Chorus, ParamId::Shimmer, ParamId::Evolution})
        e.setParam(id, 1.0f);
    e.setParam(ParamId::ReverbType, 2.0f);
    report("pior caso (tudo no máximo)", runCase(e, o));

    for (int i = 0; i < kParamCount; ++i) e.setParam(static_cast<ParamId>(i), saved[i]);
    e.waitForSources(30.0);
    return out;
}

} // namespace pad
