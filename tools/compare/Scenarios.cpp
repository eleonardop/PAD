// PAD Worship — cenários de comparação "antes x depois".
//
// O MESMO arquivo é compilado contra o motor v0.1 (tag v0.1.0-motor, com -DPAD_V1) e
// contra o motor atual. Os cenários usam só o que existe nas duas versões (parâmetros
// pelo nome), então as duas renderizam exatamente as mesmas notas, tempos e acordes.
// Parâmetros que não existem numa versão são ignorados nela.
//
// Uso: pad_scenarios <pasta_saida> [filtro]
#include "PadEngine.h"
#include "core/WavIO.h"
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <vector>

using namespace pad;

namespace {

std::string g_out;
const char* g_filter = nullptr;

void setByName(PadEngine& e, const char* name, float v) {
    for (int i = 0; i < kParamCount; ++i)
        if (std::strcmp(paramInfo(static_cast<ParamId>(i)).name, name) == 0) { e.setParam(static_cast<ParamId>(i), v); return; }
}

int presetIndex(const char* name) {
    for (int i = 0; i < presetCount(); ++i) if (std::strcmp(preset(i).name, name) == 0) return i;
    return -1;
}

void reset(PadEngine& e, int presetIdx) {
    e.panic();
    for (int i = 0; i < kParamCount; ++i) e.setParam(static_cast<ParamId>(i), paramInfo(static_cast<ParamId>(i)).defaultValue);
    if (presetIdx >= 0) e.applyPreset(presetIdx);
    const float rv = e.getParam(ParamId::Reverb);
    e.setParam(ParamId::Reverb, 0.0f);
#ifndef PAD_V1
    e.waitForSources(60.0);
#endif
    std::vector<float> buf(1024);
    for (int i = 0; i < 48000 * 6 / 512; ++i) e.process(buf.data(), 512);   // esvazia vozes e caudas
    e.setParam(ParamId::Reverb, rv);
    for (int i = 0; i < 48000 / 2 / 512; ++i) e.process(buf.data(), 512);
}

// Renderização offline é muito mais rápida que o tempo real: depois de mudar fontes, espera o
// carregador (no app, a camada entra com rampa quando fica pronta).
void sync(PadEngine& e) {
#ifndef PAD_V1
    e.waitForSources(60.0);
#else
    (void)e;
#endif
}

void idle(PadEngine& e, double seconds) {
    std::vector<float> buf(1024);
    for (int i = 0; i < static_cast<int>(seconds * 48000 / 512); ++i) e.process(buf.data(), 512);
}

using Events = std::function<void(PadEngine&, double)>;

void render(PadEngine& e, const std::string& name, double seconds, const Events& ev) {
    if (g_filter && name.find(g_filter) == std::string::npos) return;
    sync(e);
    const size_t frames = static_cast<size_t>(seconds * 48000.0);
    std::vector<float> out(frames * 2);
    for (size_t done = 0; done < frames; done += 192) {
        const int n = static_cast<int>(std::min<size_t>(192, frames - done));
        if (ev) ev(e, static_cast<double>(done) / 48000.0);
        e.process(out.data() + 2 * done, n);
    }
    writeWav24(g_out + "/" + name + ".wav", out, 48000);
    std::printf("  %s\n", name.c_str());
}

bool at(double t, double when) { return t >= when && t - 192.0 / 48000.0 < when; }

// Progressão I–V–vi–IV em Sol (G D Em C), 2x, comum no louvor.
void progression(PadEngine& e, double t, double step, double stopAt) {
    static const int deg[4] = {7, 2, 4, 0};
    static const ChordType ty[4] = {ChordType::Major, ChordType::Major, ChordType::Minor, ChordType::Major};
    for (int k = 0; k < 8; ++k) if (at(t, k * step)) e.playChord(deg[k % 4], ty[k % 4]);
    if (at(t, stopAt)) e.stop();
}

void soloLayer(PadEngine& e, const char* layer) {
    for (const char* l : {"layer_warm", "layer_air", "layer_strings", "layer_shimmer", "layer_low", "layer_vox", "layer_texture"})
        setByName(e, l, std::strcmp(l, layer) == 0 ? 0.9f : 0.0f);
}

} // namespace

int main(int argc, char** argv) {
    g_out = argc > 1 ? argv[1] : "cenarios";
    g_filter = argc > 2 ? argv[2] : nullptr;
    mkdir(g_out.c_str(), 0755);
    PadEngine e;
#ifdef PAD_V1
    e.prepare(48000.0f);
#else
    e.setAssetProvider(std::make_shared<FileAssetProvider>(PAD_ASSETS_DIR));
    e.prepare(48000.0f);
#endif
    const int warm = presetIndex("Warm");

    // ---- Camadas isoladas (mesma nota, mesmo preset base; o resto zerado)
    for (const char* layer : {"layer_warm", "layer_air", "layer_strings", "layer_shimmer", "layer_low", "layer_vox", "layer_texture"}) {
        reset(e, warm);
        soloLayer(e, layer);
        render(e, std::string("camada_") + (layer + 6), 12.0, [](PadEngine& en, double t) {
            if (at(t, 0.0)) en.playChord(9, ChordType::Pad5);
            if (at(t, 9.0)) en.stop();
        });
    }
#ifndef PAD_V1
    reset(e, warm);
    soloLayer(e, "layer_texture");
    setByName(e, "texture_type", 1.0f);
    render(e, "camada_texture_orgao", 12.0, [](PadEngine& en, double t) {
        if (at(t, 0.0)) en.playChord(9, ChordType::Pad5);
        if (at(t, 9.0)) en.stop();
    });
    reset(e, warm);
    soloLayer(e, "layer_strings");
    setByName(e, "strings_source", 1.0f);
    render(e, "camada_strings_sinteticas", 12.0, [](PadEngine& en, double t) {
        if (at(t, 0.0)) en.playChord(9, ChordType::Pad5);
        if (at(t, 9.0)) en.stop();
    });
#endif

    // ---- Todas as camadas juntas (volumes iguais)
    reset(e, warm);
    for (const char* l : {"layer_warm", "layer_air", "layer_strings", "layer_shimmer", "layer_low", "layer_vox", "layer_texture"}) setByName(e, l, 0.6f);
    render(e, "todas_camadas", 14.0, [](PadEngine& en, double t) {
        if (at(t, 0.0)) en.playChord(2, ChordType::Major);
        if (at(t, 11.0)) en.stop();
    });

    // ---- Presets com a progressão
    for (int p = 0; p < presetCount(); ++p) {
        reset(e, p);
        render(e, std::string("preset_") + preset(p).name, 40.0, [](PadEngine& en, double t) { progression(en, t, 4.0, 32.0); });
    }

    // ---- Tipos de acorde (preset Warm): maior, menor, sus2, sus4, add9, maj7
    reset(e, warm);
    render(e, "acordes_tipos", 28.0, [](PadEngine& en, double t) {
        const ChordType types[6] = {ChordType::Major, ChordType::Minor, ChordType::Sus2, ChordType::Sus4, ChordType::Add9, ChordType::Major7};
        for (int k = 0; k < 6; ++k) if (at(t, k * 4.0)) en.playChord(2, types[k]);
        if (at(t, 24.0)) en.stop();
    });

    // ---- Transições: tonalidades pelo ciclo de quintas e crossfades rápidos
    reset(e, warm);
    render(e, "transicoes_tonalidades", 40.0, [](PadEngine& en, double t) {
        for (int k = 0; k < 12; ++k) if (at(t, k * 3.0)) en.setKey((k * 7) % 12);
        if (at(t, 36.0)) en.stop();
    });
    reset(e, warm);
    setByName(e, "crossfade_time", 0.25f);
    render(e, "transicoes_rapidas", 16.0, [](PadEngine& en, double t) {
        static const int keys[8] = {0, 5, 7, 9, 2, 4, 11, 6};
        for (int k = 0; k < 40; ++k) if (at(t, k * 0.3)) en.playChord(keys[k % 8], k % 3 ? ChordType::Major : ChordType::Minor);
        if (at(t, 12.0)) en.stop();
    });

#ifndef PAD_V1
    // ---- Com e sem chorus ensemble (preset Warm)
    for (float c : {0.0f, 0.5f, 1.0f}) {
        reset(e, warm);
        setByName(e, "chorus", c);
        char name[64];
        std::snprintf(name, sizeof name, "chorus_%03d", static_cast<int>(c * 100));
        render(e, name, 20.0, [](PadEngine& en, double t) { progression(en, t, 4.0, 16.0); });
    }
    // ---- Comparação dos reverbs (preset Warm, mesmo envio). O tipo é trocado antes de 2 s de
    // silêncio, então só o reverb escolhido soa na gravação.
    const char* rnames[3] = {"reverb_classico_fdn", "reverb_plate_dattorro", "reverb_cloud_seed"};
    for (int r = 0; r < 3; ++r) {
        reset(e, warm);
        setByName(e, "reverb_type", static_cast<float>(r));
        idle(e, 2.0);
        render(e, rnames[r], 24.0, [](PadEngine& en, double t) {
            if (at(t, 0.0)) en.playChord(4, ChordType::Sus2);
            if (at(t, 8.0)) en.playChord(9, ChordType::Minor);
            if (at(t, 14.0)) en.stop();
        });
    }
    // ---- Resposta de cada reverb a uma nota curta (RT60, densidade de ecos, cor da cauda)
    for (int r = 0; r < 3; ++r) {
        reset(e, warm);
        soloLayer(e, "layer_warm");
        setByName(e, "reverb_type", static_cast<float>(r));
        setByName(e, "reverb", 1.0f);
        setByName(e, "shimmer", 0.0f);
        setByName(e, "attack", 0.01f);
        setByName(e, "release", 0.05f);
        setByName(e, "chorus", 0.0f);
        idle(e, 2.0);
        render(e, std::string("impulso_") + (rnames[r] + 7), 10.0, [](PadEngine& en, double t) {
            if (at(t, 0.0)) en.playChord(9, ChordType::Pad5);
            if (at(t, 0.1)) en.stop();
        });
    }
#endif
    return 0;
}
