// Cenários da v0.5 (pads de fundo): cada preset SOFT PADS sustentado nas tonalidades das
// referências enviadas pelo usuário (C, G, B, D, F, E, Em7), o preset v0.4 mais parecido no
// mesmo acorde, e uma progressão. Blocos de 192 amostras, 48 kHz.
// Uso: pad_scenarios_pads <saida> <assets> [filtro]
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
    auto idx = [](const char* n) { for (int p = 0; p < presetCount(); ++p) if (presetIsCurrent(p) && std::strcmp(preset(p).name, n) == 0) return p; return -1; };
    // Cada referência: nome do arquivo, tom, tipo, preset v0.5 pensado para ela e o v0.4 mais parecido.
    struct Ref { const char* tag; int root; ChordType t; const char* v05; const char* v04; };
    const Ref refs[] = {
        {"ref1_C_shimmer", 0, ChordType::Pad5, "Shimmer Pad", "Warm"},
        {"ref2_G_soft", 7, ChordType::Pad5, "Deep Pad", "Vintage"},
        {"ref3_G_shimmer", 7, ChordType::Pad5, "Shimmer Pad", "Vintage"},
        {"ref4_E_guitar", 4, ChordType::Pad5, "Guitar Pad", "Ambient"},
        {"ref5_B", 11, ChordType::Pad5, "Soft Pad", "Soft Piano"},
        {"ref6_Em7", 4, ChordType::Minor7, "Deep Pad", "Warm"},
        {"ref7_F_ambient", 5, ChordType::Pad5, "Deep Pad", "Vintage"},
        {"ref8_F_largo", 5, ChordType::Pad5, "Wide Pad", "Vintage"},
        {"ref9_D", 2, ChordType::Pad5, "Shimmer Pad", "Soft Piano"},
    };
    for (const Ref& r : refs) {
        const std::vector<Step> s = {{r.root, r.t, 30}};
        play(e, idx(r.v05), 1, std::string(r.tag) + "_v05_" + slug(r.v05), s, 6);
        play(e, idx(r.v04), 1, std::string(r.tag) + "_v04_" + slug(r.v04), s, 6);
    }
    // Progressão de louvor em G com os 5 pads (pads de fundo costumam acompanhar acordes de 4–8 s).
    const std::vector<Step> prog = {{7, ChordType::Pad5, 8}, {2, ChordType::Pad5, 8}, {4, ChordType::Minor, 8}, {0, ChordType::Major, 8},
                                    {7, ChordType::Pad5, 8}, {2, ChordType::Sus2, 8}, {0, ChordType::Major, 10}};
    for (const char* n : {"Soft Pad", "Shimmer Pad", "Deep Pad", "Wide Pad", "Guitar Pad"})
        play(e, idx(n), 1, std::string("progressao_G_") + slug(n), prog, 6);
    return 0;
}
