// PAD Engine — gerador de arquivos WAV para avaliação auditiva fora do aplicativo.
//
// Uso:  pad_wavgen <pasta_de_saida> [taxa]
// Gera exemplos de camadas, combinações, tonalidades, transições, crossfades rápidos,
// presets completos e efeitos, além de um relatorio.txt com medições de cada arquivo.
// O áudio sai do MESMO motor usado no app (mesmo código, mesma cadeia de efeitos).
#include "PadEngine.h"
#include "core/WavIO.h"
#include "core/Sources.h"
#include "dsp/Random.h"
#include <cstdio>
#include <cmath>
#include <functional>
#include <string>
#include <vector>
#include <sys/stat.h>

using namespace pad;

namespace {

struct Ctx {
    PadEngine& e;
    std::string dir;
    FILE* report;
};

void silenceAndReset(PadEngine& e, int presetIndex) {
    e.panic();
    for (int i = 0; i < kParamCount; ++i) e.setParam(static_cast<ParamId>(i), paramInfo(static_cast<ParamId>(i)).defaultValue);
    e.setParam(ParamId::Reverb, 0.0f);
    std::vector<float> buf(1024);
    for (int i = 0; i < static_cast<int>(e.sampleRate() * 4) / 512; ++i) e.process(buf.data(), 512);
    if (presetIndex >= 0) e.applyPreset(presetIndex);
    else e.setParam(ParamId::Reverb, paramInfo(ParamId::Reverb).defaultValue);
    // A renderização offline é muito mais rápida que o tempo real: espera as fontes que
    // o preset pede (no app, quem chega atrasado entra com rampa; aqui o arquivo ficaria
    // com o início sem a camada).
    e.waitForSources(60.0);
    for (int i = 0; i < static_cast<int>(e.sampleRate() * 0.3) / 512; ++i) e.process(buf.data(), 512);
}

using Events = std::function<void(double)>;

void renderTo(Ctx& c, const std::string& name, const std::string& description, double seconds, const Events& ev) {
    const float sr = c.e.sampleRate();
    const size_t frames = static_cast<size_t>(seconds * sr);
    std::vector<float> out(frames * 2);
    const int block = 192;
    for (size_t done = 0; done < frames; done += block) {
        const int n = static_cast<int>(std::min<size_t>(block, frames - done));
        if (ev) ev(static_cast<double>(done) / sr);
        c.e.process(out.data() + 2 * done, n);
    }
    // Medições para o relatório
    double s = 0, lr = 0, ll = 0, rr = 0;
    float pk = 0;
    for (size_t i = 0; i < frames; ++i) {
        const double l = out[2 * i], r = out[2 * i + 1];
        s += 0.5 * (l * l + r * r); lr += l * r; ll += l * l; rr += r * r;
        pk = std::max(pk, static_cast<float>(std::max(std::fabs(l), std::fabs(r))));
    }
    const auto st = c.e.readStats();
    const std::string path = c.dir + "/" + name + ".wav";
    const bool ok = writeWav24(path, out, static_cast<int>(sr));
    std::fprintf(c.report, "%-40s %5.1f s  RMS %6.1f dBFS  pico %6.1f dBFS  correl. L/R %5.2f  limitador %5.1f dB\n    %s\n",
                 (name + ".wav").c_str(), seconds, 10 * std::log10(s / frames + 1e-20), 20 * std::log10(pk + 1e-12),
                 lr / std::sqrt(ll * rr + 1e-30), 20 * std::log10(st.limiterMinGain), description.c_str());
    std::printf("%s %s\n", ok ? "gerado" : "ERRO ao gravar", path.c_str());
}

void solo(PadEngine& e, std::initializer_list<ParamId> on) {
    for (ParamId id : {ParamId::LayerWarm, ParamId::LayerAir, ParamId::LayerStrings, ParamId::LayerShimmer, ParamId::LayerLow,
                       ParamId::LayerVox, ParamId::LayerTexture}) {
        bool enabled = false;
        for (ParamId o : on) enabled |= (o == id);
        e.setParam(id, enabled ? 0.9f : 0.0f);
    }
    e.waitForSources(60.0);
}

// Progressão I–V–vi–IV (muito comum em louvor), em Sol: G, D, Em, C.
void progression(PadEngine& e, double t, double step, int& idx, int key = 7) {
    static const int degrees[4] = {0, 7, 9, 5};
    static const ChordType types[4] = {ChordType::Major, ChordType::Major, ChordType::Minor, ChordType::Major};
    if (idx < 8 && t >= idx * step) {
        e.playChord((key + degrees[idx % 4]) % 12, types[idx % 4]);
        ++idx;
    }
}

} // namespace

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : "wav_exemplos";
    const float sr = argc > 2 ? static_cast<float>(std::atof(argv[2])) : 48000.0f;
#ifdef _WIN32
    _mkdir(dir.c_str());
#else
    mkdir(dir.c_str(), 0755);
#endif
    PadEngine engine;
    engine.setAssetProvider(std::make_shared<FileAssetProvider>(PAD_ASSETS_DIR));
    std::printf("Preparando motor (%.0f Hz)...\n", sr);
    if (!engine.prepare(sr)) { std::printf("falha no prepare\n"); return 1; }
    FILE* rep = std::fopen((dir + "/relatorio.txt").c_str(), "w");
    if (!rep) { std::printf("não foi possível criar o relatório\n"); return 1; }
    std::fprintf(rep, "PAD Engine — exemplos de áudio (%.0f Hz, PCM 24 bits)\n\n", sr);
    Ctx c{engine, dir, rep};

    // ---- Camadas individuais (Lá, Pad5)
    struct LayerDemo { const char* file; ParamId id; const char* desc; float strings, texture; };
    const LayerDemo layers[] = {
        {"01_camada_warm", ParamId::LayerWarm, "Camada Warm isolada (PADsynth, morph escuro/aberto), A (Pad5).", 0, 0},
        {"02_camada_vox", ParamId::LayerVox, "Camada Vox isolada: coral PADsynth com vogais oh/ah.", 0, 0},
        {"03_camada_strings_reais", ParamId::LayerStrings, "Camada Strings isolada: naipes reais (VSCO 2 CE).", 0, 0},
        {"04_camada_strings_sinteticas", ParamId::LayerStrings, "Camada Strings isolada: cordas sintéticas da v0.1 (alternativa).", 1, 0},
        {"05_camada_textura_piano", ParamId::LayerTexture, "Textura: piano Cathedral (Salamander, espectro congelado).", 0, 0},
        {"06_camada_textura_orgao", ParamId::LayerTexture, "Textura: órgão de tubos suave (VCSL).", 0, 1},
        {"07_camada_air", ParamId::LayerAir, "Camada Air isolada: harmônicos agudos da própria nota, sem ruído.", 0, 0},
        {"08_camada_shimmer", ParamId::LayerShimmer, "Camada Shimmer (Glass) isolada, com reverb shimmer.", 0, 0},
        {"09_camada_low", ParamId::LayerLow, "Camada Low isolada (fundamental grave).", 0, 0},
    };
    for (const auto& l : layers) {
        silenceAndReset(engine, -1);
        engine.setParam(ParamId::StringsSource, l.strings);
        engine.setParam(ParamId::TextureType, l.texture);
        solo(engine, {l.id});
        if (l.id == ParamId::LayerShimmer) engine.setParam(ParamId::Shimmer, 0.5f);
        renderTo(c, l.file, l.desc, 12.0, [&](double t) {
            if (t == 0.0) engine.playChord(9, ChordType::Pad5);
            if (t >= 9.0 && t - 192.0 / sr < 9.0) engine.stop();
        });
    }

    // ---- Combinações
    silenceAndReset(engine, -1);
    solo(engine, {ParamId::LayerWarm, ParamId::LayerStrings});
    renderTo(c, "10_combinacao_warm_strings", "Warm + Strings, D maior.", 12.0, [&](double t) {
        if (t == 0.0) engine.playChord(2, ChordType::Major);
        if (t >= 9.0 && t - 192.0 / sr < 9.0) engine.stop();
    });
    silenceAndReset(engine, -1);
    solo(engine, {ParamId::LayerWarm, ParamId::LayerVox, ParamId::LayerAir, ParamId::LayerLow});
    renderTo(c, "11_combinacao_warm_vox_air_low", "Warm + Vox + Air + Low, E (Pad5).", 12.0, [&](double t) {
        if (t == 0.0) engine.playChord(4, ChordType::Pad5);
        if (t >= 9.0 && t - 192.0 / sr < 9.0) engine.stop();
    });

    // ---- Tonalidades: ciclo de quintas, 12 tons
    silenceAndReset(engine, 0);
    {
        int k = 0;
        renderTo(c, "12_tonalidades_ciclo_de_quintas", "Preset Warm, Pad5 nos 12 tons (ciclo de quintas), 3 s cada.", 40.0, [&](double t) {
            if (k < 12 && t >= k * 3.0) { engine.setKey((k * 7) % 12); ++k; }
            if (t >= 36.0 && t - 192.0 / sr < 36.0) engine.stop();
        });
    }

    // ---- Transições entre acordes (I–V–vi–IV)
    silenceAndReset(engine, 0);
    {
        int idx = 0;
        renderTo(c, "13_transicoes_I_V_vi_IV", "Preset Warm, G–D–Em–C (2x), 5 s por acorde, crossfade 1,8 s.", 46.0, [&](double t) {
            progression(engine, t, 5.0, idx);
            if (t >= 40.0 && t - 192.0 / sr < 40.0) engine.stop();
        });
    }

    // ---- Crossfades rápidos
    silenceAndReset(engine, 0);
    {
        engine.setParam(ParamId::CrossfadeTime, 0.25f);
        Rng rng(5);
        double next = 0;
        renderTo(c, "14_crossfades_rapidos", "Troca a cada 0,3 s (crossfade 0,25 s), tons e tipos aleatórios.", 16.0, [&](double t) {
            if (t >= next && t < 12.0) {
                engine.playChord(static_cast<int>(rng.uniform() * 12), static_cast<ChordType>(static_cast<int>(rng.uniform() * 10)));
                next = t + 0.3;
            }
            if (t >= 12.0 && t - 192.0 / sr < 12.0) engine.stop();
        });
    }

    // ---- Timbres completos (presets) com a progressão
    for (int p = 0; p < presetCount(); ++p) {
        silenceAndReset(engine, p);
        int idx = 0;
        char name[64];
        std::snprintf(name, sizeof name, "%02d_preset_%s", 20 + p, preset(p).name);
        renderTo(c, name, std::string("Preset ") + preset(p).name + ": " + preset(p).description + " Progressão G–D–Em–C.",
                 40.0, [&](double t) {
                     progression(engine, t, 4.0, idx);
                     if (t >= 32.0 && t - 192.0 / sr < 32.0) engine.stop();
                 });
    }

    // ---- Efeitos
    silenceAndReset(engine, 0);
    renderTo(c, "30_efeito_reverb_seco_ate_cheio", "Reverb sobe de 0 a 1 em 16 s (preset Warm, C maior).", 20.0, [&](double t) {
        if (t == 0.0) { engine.setParam(ParamId::Reverb, 0.0f); engine.playChord(0, ChordType::Major); }
        engine.setParam(ParamId::Reverb, static_cast<float>(std::min(1.0, t / 16.0)));
        if (t >= 17.0 && t - 192.0 / sr < 17.0) engine.stop();
    });
    silenceAndReset(engine, 0);
    renderTo(c, "31_efeito_shimmer_0_vs_max", "Shimmer 0 (0–8 s) e máximo (8–20 s), reverb longo.", 24.0, [&](double t) {
        if (t == 0.0) { engine.setParam(ParamId::Shimmer, 0.0f); engine.setParam(ParamId::ReverbDecay, 9.0f); engine.playChord(4, ChordType::Sus2); }
        if (t >= 8.0) engine.setParam(ParamId::Shimmer, 1.0f);
        if (t >= 18.0 && t - 192.0 / sr < 18.0) engine.stop();
    });
    silenceAndReset(engine, 0);
    renderTo(c, "32_efeito_largura_mono_ate_larga", "Width de 0 (mono) até 1,5 em 12 s.", 15.0, [&](double t) {
        if (t == 0.0) engine.playChord(5, ChordType::Major7);
        engine.setParam(ParamId::Width, static_cast<float>(std::min(1.5, t / 8.0)));
        if (t >= 13.0 && t - 192.0 / sr < 13.0) engine.stop();
    });
    silenceAndReset(engine, 0);
    renderTo(c, "33_efeito_brilho_e_calor", "Brightness 0->1 (0–8 s), depois Warmth 0->1 (8–16 s).", 19.0, [&](double t) {
        if (t == 0.0) engine.playChord(9, ChordType::Minor);
        if (t < 8.0) engine.setParam(ParamId::Brightness, static_cast<float>(t / 8.0));
        else engine.setParam(ParamId::Warmth, static_cast<float>(std::min(1.0, (t - 8.0) / 8.0)));
        if (t >= 16.5 && t - 192.0 / sr < 16.5) engine.stop();
    });
    silenceAndReset(engine, 0);
    renderTo(c, "34_efeito_movimento_0_vs_max", "Movement 0 (0–8 s) e 1 (8–16 s).", 19.0, [&](double t) {
        if (t == 0.0) { engine.setParam(ParamId::Movement, 0.0f); engine.playChord(0, ChordType::Pad5); }
        if (t >= 8.0) engine.setParam(ParamId::Movement, 1.0f);
        if (t >= 16.0 && t - 192.0 / sr < 16.0) engine.stop();
    });

    silenceAndReset(engine, 0);
    renderTo(c, "35_efeito_evolucao_0_vs_max", "Evolução 0 (0–30 s) e 1 (30–60 s), preset Warm, D (Pad5).", 63.0, [&](double t) {
        if (t == 0.0) { engine.setParam(ParamId::Evolution, 0.0f); engine.playChord(2, ChordType::Pad5); }
        if (t >= 30.0) engine.setParam(ParamId::Evolution, 1.0f);
        if (t >= 60.0 && t - 192.0 / sr < 60.0) engine.stop();
    });
    silenceAndReset(engine, 0);
    renderTo(c, "36_efeito_intensidade_0_ate_1", "Intensidade sobe de 0 a 1 em 16 s (preset Warm, G maior).", 20.0, [&](double t) {
        if (t == 0.0) engine.playChord(7, ChordType::Major);
        engine.setParam(ParamId::Intensity, static_cast<float>(std::min(1.0, t / 16.0)));
        if (t >= 17.0 && t - 192.0 / sr < 17.0) engine.stop();
    });
    silenceAndReset(engine, 0);
    renderTo(c, "37_efeito_chorus_0_vs_max", "Chorus 0 (0–8 s) e 1 (8–16 s), preset Warm, C (Add9).", 19.0, [&](double t) {
        if (t == 0.0) { engine.setParam(ParamId::Chorus, 0.0f); engine.playChord(0, ChordType::Add9); }
        if (t >= 8.0) engine.setParam(ParamId::Chorus, 1.0f);
        if (t >= 16.0 && t - 192.0 / sr < 16.0) engine.stop();
    });
    {
        const char* names[3] = {"38_reverb_classico", "39_reverb_plate", "40_reverb_cloud"};
        const char* desc[3] = {"Reverb Clássico (FDN da v0.1)", "Reverb Plate (Dattorro)", "Reverb Cloud (Cloud Seed, padrão)"};
        for (int k = 0; k < 3; ++k) {
            silenceAndReset(engine, 0);
            engine.setParam(ParamId::ReverbType, static_cast<float>(k));
            std::vector<float> tmp(1024);
            for (int i = 0; i < static_cast<int>(sr * 6) / 512; ++i) engine.process(tmp.data(), 512); // cauda do tipo anterior
            int idx = 0;
            renderTo(c, names[k], std::string(desc[k]) + ", preset Warm, G–D–Em–C e acorde final soltando.", 30.0, [&](double t) {
                if (t < 16.0) progression(engine, t, 4.0, idx);
                if (t >= 20.0 && t - 192.0 / sr < 20.0) engine.stop();
            });
        }
    }

    std::fclose(rep);
    std::printf("Relatório: %s/relatorio.txt\n", dir.c_str());
    return 0;
}
