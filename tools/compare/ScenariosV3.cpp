// Cenários de comparação v0.2 × v0.3 (testes A–M pedidos para a v0.3).
//
// O MESMO arquivo é compilado contra o motor v0.2 (branch versao-0.2-som, com -DPAD_V2) e
// contra o motor atual. Condições idênticas: mesmos acordes, mesmas durações, blocos de 192
// amostras, 48 kHz, mesmo volume geral. As fontes são esperadas antes de cada cenário (a
// renderização offline é mais rápida que o carregamento).
// Uso: pad_scenarios_v3 <pasta_saida> <pasta_assets> [filtro]
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
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

int presetIndex(const char* name) {
    for (int i = 0; i < presetCount(); ++i) if (std::strcmp(preset(i).name, name) == 0) return i;
    return -1;
}

void silence(PadEngine& e, double s) {
    std::vector<float> b(kBlock * 2);
    for (int i = 0; i < static_cast<int>(s * 48000 / kBlock); ++i) e.process(b.data(), kBlock);
}

void setup(PadEngine& e, int presetIdx, const std::function<void(PadEngine&)>& tweak) {
    e.panic();
    silence(e, 0.3);
    for (int i = 0; i < kParamCount; ++i) e.setParam(static_cast<ParamId>(i), paramInfo(static_cast<ParamId>(i)).defaultValue);
    if (presetIdx >= 0) e.applyPreset(presetIdx);
    if (tweak) tweak(e);
    e.waitForSources(90.0);
    silence(e, 12.0); // caudas do cenário anterior morrem
}

// Toca a sequência; `swellBefore` = segundos antes de cada troca em que o Swell é apertado
// (0 = nunca; só existe na v0.3). Solta o último acorde e deixa a cauda.
void play(PadEngine& e, const std::string& name, const std::vector<Step>& seq, double tail, const std::vector<int>& swellAt = {},
          double swellLead = 0.0) {
    if (g_filter && name.find(g_filter) == std::string::npos) return;
    double total = tail;
    for (const auto& s : seq) total += s.dur;
    std::vector<double> starts;
    double acc = 0;
    for (const auto& s : seq) { starts.push_back(acc); acc += s.dur; }
    std::vector<float> out(static_cast<size_t>(total * 48000) * 2), b(kBlock * 2);
    size_t next = 0;
    std::vector<bool> swelled(seq.size(), false);
    for (size_t f = 0; f + kBlock <= out.size() / 2; f += kBlock) {
        const double t = static_cast<double>(f) / 48000.0;
        if (next < seq.size() && t >= starts[next]) { e.playChord(seq[next].root, seq[next].type); ++next; }
        if (next == seq.size() && t >= acc && t - kBlock / 48000.0 < acc) e.stop();
#ifndef PAD_V2
        for (int k : swellAt) {
            if (k <= 0 || k >= static_cast<int>(seq.size()) || swelled[static_cast<size_t>(k)]) continue;
            if (t >= starts[static_cast<size_t>(k)] - swellLead) { e.swell(); swelled[static_cast<size_t>(k)] = true; }
        }
#else
        (void)swellAt; (void)swellLead;
#endif
        e.process(b.data(), kBlock);
        std::copy(b.begin(), b.end(), out.begin() + static_cast<long>(2 * f));
    }
    writeWav24(g_out + "/" + name + ".wav", out, 48000);
    std::printf("%s\n", name.c_str());
    std::fflush(stdout);
}

const ChordType M = ChordType::Major, m = ChordType::Minor;
// Progressão de worship em Sol, durações variadas (como num culto), 92 s + cauda.
const std::vector<Step> kWorship = {
    {7, M, 8}, {2, M, 6}, {4, m, 8}, {0, M, 6}, {7, M, 8}, {2, M, 6}, {0, M, 10},
    {4, m, 8}, {2, M, 6}, {7, M, 8}, {11, m, 6}, {0, M, 6}, {2, M, 6}};
const std::vector<Step> kSlow = {{7, M, 12}, {4, m, 12}, {0, M, 12}, {2, M, 12}, {7, M, 12}, {4, m, 12}};
std::vector<Step> fastSeq() {
    std::vector<Step> v;
    const int roots[8] = {7, 2, 4, 0, 9, 2, 7, 0};
    const ChordType ty[8] = {M, M, m, M, m, M, M, M};
    for (int i = 0; i < 20; ++i) v.push_back({roots[i % 8], ty[i % 8], 1.5});
    return v;
}

void zeroPad(PadEngine& e) {
    for (ParamId id : {ParamId::LayerWarm, ParamId::LayerAir, ParamId::LayerStrings, ParamId::LayerShimmer, ParamId::LayerLow,
                       ParamId::LayerVox, ParamId::LayerTexture})
        e.setParam(id, 0.0f);
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

#ifdef PAD_V2
    const int base = presetIndex("Warm");
    // Correspondência para o teste F: cada preset da v0.3 contra o mais próximo da v0.2.
    const char* fNames[8] = {"Warm", "Deep", "Cinematic", "Ambient", "Warm", "Cathedral", "Air", "Warm"};
#else
    const int base = presetIndex("Modern Worship");
    const char* fNames[8] = {"Warm Worship", "Deep Worship", "Cinematic", "Ambient", "Vintage", "Cathedral", "Ethereal", "Modern Worship"};
#endif
    const char* fFiles[8] = {"warm_worship", "deep_worship", "cinematic", "ambient", "vintage", "cathedral", "ethereal", "modern_worship"};

    // A — acorde sustentado 60 s
    setup(e, base, nullptr);
    play(e, "A_acorde_sustentado_60s", {{7, M, 60}}, 6);
    // B — progressão worship (~92 s)
    setup(e, base, nullptr);
    play(e, "B_progressao_worship", kWorship, 6);
    // C — troca lenta (12 s)
    setup(e, base, nullptr);
    play(e, "C_troca_lenta", kSlow, 6);
    // D — troca rápida (1,5 s)
    setup(e, base, nullptr);
    play(e, "D_troca_rapida", fastSeq(), 5);
    // E — Evolução 0/25/50/75/100 (progressão lenta de 48 s)
    for (int k = 0; k <= 4; ++k) {
        setup(e, base, [k](PadEngine& en) { en.setParam(ParamId::Evolution, k * 0.25f); });
        char name[64];
        std::snprintf(name, sizeof name, "E_evolucao_%03d", k * 25);
        play(e, name, {{7, M, 16}, {4, m, 16}, {0, M, 16}}, 5);
    }
    // F — cada preset (progressão de 48 s)
    for (int k = 0; k < 8; ++k) {
        setup(e, presetIndex(fNames[k]), nullptr);
        play(e, std::string("F_preset_") + fFiles[k], {{7, M, 12}, {2, M, 12}, {4, m, 12}, {0, M, 12}}, 6);
    }
    // G — só PAD (texturas, pratos e transições desligados)
    setup(e, base, [](PadEngine& en) {
#ifndef PAD_V2
        for (ParamId id : {ParamId::LayerKeys, ParamId::LayerSparkle, ParamId::LayerMetal, ParamId::Bloom, ParamId::Activity})
            en.setParam(id, 0.0f);
#else
        (void)en;
#endif
    });
    play(e, "G_somente_pad", kWorship, 6);
#ifndef PAD_V2
    // H — PAD + TEXTURE (sem transições nem pratos)
    setup(e, base, [](PadEngine& en) { en.setParam(ParamId::Bloom, 0.0f); en.setParam(ParamId::LayerMetal, 0.0f); });
    play(e, "H_pad_texture", kWorship, 6);
    // I — PAD + TEXTURE + TRANSITION (automáticas)
    setup(e, base, nullptr);
    play(e, "I_pad_texture_transition", kWorship, 6);
    // J — pratos isolados: blooms nas mudanças grandes, um swell e arco em acorde longo
    setup(e, base, [](PadEngine& en) {
        zeroPad(en);
        en.setParam(ParamId::LayerKeys, 0.0f);
        en.setParam(ParamId::LayerSparkle, 0.0f);
        en.setParam(ParamId::LayerMetal, 1.0f);
        en.setParam(ParamId::Bloom, 1.0f);
        en.setParam(ParamId::BloomChance, 1.0f);
        en.setParam(ParamId::Evolution, 0.9f);
        en.setParam(ParamId::Activity, 0.0f);
    });
    play(e, "J_pratos_isolados", {{7, M, 8}, {1, M, 8}, {7, M, 8}, {10, M, 40}, {3, M, 8}}, 8, {4}, 4.0);
    // K — reverse/swell isolado: swell manual antes de cada troca + bloom
    setup(e, base, [](PadEngine& en) {
        zeroPad(en);
        en.setParam(ParamId::LayerSparkle, 0.0f);
        en.setParam(ParamId::Activity, 0.0f);
        en.setParam(ParamId::SwellTime, 4.0f);
    });
    play(e, "K_reverse_swell_isolado", {{7, M, 9}, {2, M, 9}, {4, m, 9}, {0, M, 9}, {7, M, 9}}, 8, {1, 2, 3, 4}, 4.0);
    // L — mix completo: Modern Worship + swells manuais antes de três trocas
    setup(e, base, nullptr);
    play(e, "L_mix_completo", kWorship, 6, {2, 6, 10}, 4.0);
#endif
    return 0;
}
