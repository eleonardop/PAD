// Cenários da etapa de refinamento (v0.4): acordes sustentados de ~18 s por preset, presets
// antigos × novos, progressões diferentes e sementes diferentes. Tudo no motor atual (as
// versões anteriores dos presets continuam nele). Blocos de 192 amostras, 48 kHz.
// Uso: pad_scenarios_refino <saida> <assets> [filtro]
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <vector>
#include "PadEngine.h"
#include "core/Sources.h"
#include "core/WavIO.h"
#include "dsp/DspMath.h"

using namespace pad;

namespace {
std::string g_out;
const char* g_filter = nullptr;
constexpr int kBlock = 192;
struct Step { int root; ChordType type; double dur; };
const ChordType M = ChordType::Major, m = ChordType::Minor;

void silence(PadEngine& e, double s) {
    std::vector<float> b(kBlock * 2);
    for (int i = 0; i < static_cast<int>(s * 48000 / kBlock); ++i) e.process(b.data(), kBlock);
}

std::string slug(const char* name) {
    std::string s;
    for (const char* p = name; *p; ++p) {
        const char c = *p;
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) s += c;
        else if (c >= 'A' && c <= 'Z') s += static_cast<char>(c - 'A' + 'a');
        else if (!s.empty() && s.back() != '_') s += '_';
    }
    while (!s.empty() && s.back() == '_') s.pop_back();
    return s;
}

void play(PadEngine& e, int preset, uint32_t seed, const std::string& name, const std::vector<Step>& seq, double tail) {
    if (g_filter && name.find(g_filter) == std::string::npos) return;
    e.panic();
    silence(e, 0.3);
    for (int i = 0; i < kParamCount; ++i) e.setParam(static_cast<ParamId>(i), paramInfo(static_cast<ParamId>(i)).defaultValue);
    e.applyPreset(preset);
    e.setSeed(seed);
    e.waitForSources(90.0);
    silence(e, 12.0);
    double total = tail;
    for (const auto& s : seq) total += s.dur;
    std::vector<float> out(static_cast<size_t>(total * 48000) * 2), b(kBlock * 2);
    size_t next = 0;
    double acc = 0, start = 0;
    for (const auto& s : seq) acc += s.dur;
    for (size_t f = 0; f + kBlock <= out.size() / 2; f += kBlock) {
        const double t = static_cast<double>(f) / 48000.0;
        if (next < seq.size() && t >= start) { e.playChord(seq[next].root, seq[next].type); start += seq[next].dur; ++next; }
        if (next == seq.size() && t >= acc && t - kBlock / 48000.0 < acc) e.stop();
        e.process(b.data(), kBlock);
        std::copy(b.begin(), b.end(), out.begin() + static_cast<long>(2 * f));
    }
    writeWav24(g_out + "/" + name + ".wav", out, 48000);
    std::printf("%s\n", name.c_str());
    std::fflush(stdout);
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "uso: %s <saida> <assets> [filtro]\n", argv[0]); return 1; }
    g_out = argv[1];
    g_filter = argc > 3 ? argv[3] : nullptr;
    mkdir(g_out.c_str(), 0755);
    ScopedNoDenormals nd;
    PadEngine e;
    e.setAssetProvider(std::make_shared<FileAssetProvider>(argv[2]));
    if (!e.prepare(48000.0f)) return 1;

    const std::vector<Step> sustained = {{7, M, 18}};                                            // Sol maior, 18 s
    const std::vector<Step> worship = {{7, M, 6}, {2, M, 6}, {4, m, 6}, {0, M, 6}, {7, M, 6}, {2, M, 6}, {0, M, 8}}; // 44 s
    const std::vector<Step> minor = {{9, m, 9}, {5, M, 9}, {0, M, 9}, {7, M, 9}};                // Lá menor, 36 s
    const std::vector<Step> pad5 = {{2, ChordType::Pad5, 12}, {7, ChordType::Sus2, 12}, {9, ChordType::Pad5, 12}}; // 36 s

    int n = 0;
    for (int p = 0; p < presetCount(); ++p) {
        if (!presetIsCurrent(p)) continue;
        char pre[16];
        std::snprintf(pre, sizeof pre, "%d", ++n);
        const std::string s = slug(preset(p).name);
        play(e, p, 1, std::string("1_sustentado_") + pre + "_" + s, sustained, 5);
        play(e, p, 1, std::string("3_progressao_worship_") + pre + "_" + s, worship, 6);
        play(e, p, 1, std::string("4_progressao_menor_") + pre + "_" + s, minor, 6);
        play(e, p, 1, std::string("5_progressao_pad5_sus_") + pre + "_" + s, pad5, 6);
    }
    for (int p = 0; p < presetCount(); ++p) {
        if (presetIsCurrent(p)) continue;
        play(e, p, 1, std::string("2_antigo_sustentado_") + slug(preset(p).name), sustained, 5);
    }
    for (const char* name : {"Soft Piano", "Air", "Cinematic", "Reverse Bloom"}) {
        for (uint32_t seed : {11u, 22u, 33u}) {
            int idx = -1;
            for (int p = 0; p < presetCount(); ++p) if (presetIsCurrent(p) && std::strcmp(preset(p).name, name) == 0) idx = p;
            char nm[96];
            std::snprintf(nm, sizeof nm, "6_semente_%s_%u", slug(name).c_str(), seed);
            play(e, idx, seed, nm, sustained, 5);
        }
    }
    return 0;
}
