#include <string>
// Testes do motor da versão 0.2: fontes sob demanda, troca de reverb, reserva sem assets,
// Evolução, Intensidade, chorus e consistência dos presets.
#include "TestFramework.h"
#include "AudioAnalysis.h"
#include <chrono>
#include <cmath>
#include <thread>

using namespace pad;
using namespace padtest;

namespace {
constexpr float kSr = 48000.0f;
constexpr float kLimit = 0.8913f;
size_t fr(double s) { return static_cast<size_t>(s * kSr); }

// Centróide só da faixa de timbre (300 Hz–8 kHz): o grave domina a energia e esconderia
// as mudanças de cor que o ouvido percebe.
double bandCentroid(const std::vector<float>& m, size_t start, size_t n) {
    auto db = magnitudeSpectrumDb(m, start, n);
    double num = 0, den = 0;
    for (size_t k = 1; k < db.size(); ++k) {
        const double f = k * kSr / n;
        if (f < 300 || f > 8000) continue;
        const double p = std::pow(10.0, db[k] / 10.0);
        num += p * f;
        den += p;
    }
    return num / (den + 1e-30);
}
std::vector<double> centroidSeries(const Stereo& x, double from, double to, double win) {
    std::vector<double> c;
    const auto m = mono(x);
    const size_t n = win >= 1.9 ? 65536 : 16384;
    for (double t = from; t + win <= to; t += win) c.push_back(bandCentroid(m, fr(t), n));
    return c;
}
double stddev(const std::vector<double>& v) {
    double m = 0, s = 0;
    for (double x : v) m += x;
    m /= v.size();
    for (double x : v) s += (x - m) * (x - m);
    return std::sqrt(s / v.size());
}
} // namespace

// Presets: todos os valores dentro das faixas e o volume preenchido (detecta preset incompleto).
TEST(presets_valores_validos) {
    for (int p = 0; p < presetCount(); ++p) {
        const Preset& pr = preset(p);
        for (int i = 0; i < kParamCount; ++i) {
            const ParamInfo& info = paramInfo(static_cast<ParamId>(i));
            CHECK_MSG(pr.values[i] >= info.minValue && pr.values[i] <= info.maxValue, "%s.%s = %g", pr.name, info.name, pr.values[i]);
        }
        CHECK_MSG(pr.values[static_cast<int>(ParamId::MasterVolume)] > 0.7f, "%s: volume ausente", pr.name);
        CHECK(pr.values[static_cast<int>(ParamId::ReverbDecay)] >= 1.0f);
    }
    report("%d presets verificados", presetCount());
}

// Troca de tipo de reverb com som tocando: sem clique, sem alocação.
TEST(motor_troca_de_reverb_sem_clique) {
    PadEngine& e = sharedEngine();
    resetEngine(e, 0);
    e.playChord(2, ChordType::Major);
    g_allocationCount = 0;
    int k = 0;
    auto x = render(e, 12.0, 192, [&](double t) {
        if (t >= 4.0 + 2.0 * k && k < 4) { e.setParam(ParamId::ReverbType, static_cast<float>((k + 1) % 3)); ++k; }
    }, true);
    const double clicks = clickScore(x, fr(3.5), fr(12.0));
    report("4 trocas de reverb durante o acorde: índice de clique %.2f, alocações %ld", clicks, g_allocationCount.load());
    CHECK(clicks < 3.0);
    CHECK(g_allocationCount.load() == 0);
    CHECK(peakAbs(x) <= kLimit);
}

// Camada ligada com o acorde tocando: a fonte é carregada sob demanda e entra em rampa.
TEST(motor_fontes_sob_demanda_sem_clique) {
    PadEngine& e = sharedEngine();
    resetEngine(e, 0);
    e.setParam(ParamId::LayerVox, 0.0f);
    e.setParam(ParamId::Reverb, 0.0f);
    // Espera a liberação (nenhuma voz usa o Vox): o carregador tira da memória.
    render(e, 1.0);
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    render(e, 0.5);
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    const size_t memOff = e.readStats().sourcesMemoryBytes;
    e.playChord(7, ChordType::Major);
    auto x0 = render(e, 4.0);
    const double before = rmsDb(x0, fr(2.0), fr(4.0)); // acorde parado, sem o coral
    e.setParam(ParamId::LayerVox, 1.0f);     // pede o coral com o acorde soando
    e.waitForSources(30.0);
    const size_t memOn = e.readStats().sourcesMemoryBytes;
    auto x = render(e, 3.0);
    const double after = rmsDb(x, fr(2.0), fr(3.0));
    const double clicks = clickScore(x, 0, fr(3.0));
    report("memória sem Vox %.1f MB, com Vox %.1f MB; nível antes %.1f dB, depois %.1f dB; índice de clique %.2f",
           memOff / 1e6, memOn / 1e6, before, after, clicks);
    CHECK(memOn > memOff);
    CHECK(after > before + 0.5);
    CHECK(clicks < 3.0);
}

// Sem assets (samples ausentes): as cordas usam automaticamente a versão sintética.
TEST(motor_sem_assets_usa_cordas_sinteticas) {
    PadEngine e; // sem setAssetProvider
    e.prepare(kSr, 30.0);
    for (ParamId id : {ParamId::LayerWarm, ParamId::LayerAir, ParamId::LayerShimmer, ParamId::LayerLow, ParamId::LayerVox, ParamId::LayerTexture})
        e.setParam(id, 0.0f);
    e.setParam(ParamId::LayerStrings, 1.0f);
    e.setParam(ParamId::StringsSource, 0.0f);
    e.waitForSources(30.0);
    e.setParam(ParamId::Attack, 0.1f);
    e.playChord(9, ChordType::Pad5);
    auto x = render(e, 3.0);
    const auto st = e.readStats();
    report("falhas %x (cordas reais), prontas %x; nível %.1f dB", st.sourcesFailed, st.sourcesReady, rmsDb(x, fr(1.5), fr(3.0)));
    CHECK(st.sourcesFailed & sourceBit(SourceId::StringsReal));
    CHECK(st.sourcesReady & sourceBit(SourceId::StringsSynth));
    CHECK(rmsDb(x, fr(1.5), fr(3.0)) > -40.0);
}

// Evolução: o timbre muda ao longo do tempo (morph + filtro), sem mudar a afinação.
// Mede-se a inclinação espectral (energia 1–5 kHz menos 150–800 Hz, em dB) da camada Warm
// isolada, ao longo de 90 s. Com Evolução 0 ela fica parada (só o ruído de estimativa);
// com Evolução 1 ela passeia lentamente. Compara-se a amplitude pico a pico da série
// medida em janelas longas, que separa a evolução lenta do ruído de medida.
// Média de Welch (janelas de 32768 com 50% de sobreposição) ao longo de `len` amostras.
double tiltDb(const std::vector<float>& m, size_t start, size_t len) {
    const size_t n = 32768;
    double lo = 0, hi = 0;
    for (size_t off = 0; off + n <= len; off += n / 2) {
        auto db = magnitudeSpectrumDb(m, start + off, n);
        for (size_t k = 1; k < db.size(); ++k) {
            const double f = k * kSr / n, p = std::pow(10.0, db[k] / 10.0);
            if (f >= 150 && f < 800) lo += p;
            else if (f >= 1000 && f < 5000) hi += p;
        }
    }
    return 10 * std::log10((hi + 1e-30) / (lo + 1e-30));
}
TEST(motor_evolucao_movimenta_timbre) {
    PadEngine& e = sharedEngine();
    double span[2];
    const float evo[2] = {0.0f, 1.0f};
    for (int k = 0; k < 2; ++k) {
        resetEngine(e, 0);
        for (ParamId id : {ParamId::LayerVox, ParamId::LayerStrings, ParamId::LayerTexture, ParamId::LayerAir,
                           ParamId::LayerShimmer, ParamId::LayerLow, ParamId::Reverb, ParamId::Movement, ParamId::Chorus})
            e.setParam(id, 0.0f);
        e.setParam(ParamId::LayerWarm, 1.0f);
        e.setParam(ParamId::Evolution, evo[k]);
        e.setParam(ParamId::Attack, 0.3f);
        e.playChord(9, ChordType::Pad5);
        auto x = render(e, 92.0);
        const auto m = mono(x);
        // Janelas de 262144 amostras (5,5 s, o comprimento da tabela PADsynth): cada janela
        // cobre a tabela inteira, então a flutuação natural das fases aleatórias se cancela e
        // sobra só a mudança lenta de timbre. Salto de 1,5 s.
        double lo = 1e9, hi = -1e9;
        std::string ser;
        for (double s = 2.0; s + 5.5 <= 92.0; s += 1.5) {
            const double v = tiltDb(m, fr(s), 262144);
            ser += std::to_string(int(std::lrint(v * 10))) + " ";
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
        span[k] = hi - lo;
        report("série (x10 dB): %s", ser.c_str());
        report("Evolução %.0f: inclinação espectral (1–5 kHz vs 150–800 Hz) varia %.1f dB ao longo de 90 s", evo[k], span[k]);
    }
    // Com Evolução 0 a variação que sobra é ruído de estimativa (~2 dB nesta medida).
    CHECK(span[1] > 1.6 * span[0]);
    CHECK(span[1] > 3.0);
}

// Intensidade: mais brilho e mais volume, de forma contínua.
TEST(motor_intensidade_dinamica) {
    PadEngine& e = sharedEngine();
    double c[2], l[2];
    for (int k = 0; k < 2; ++k) {
        resetEngine(e, 0);
        e.setParam(ParamId::Reverb, 0.0f);
        e.setParam(ParamId::Evolution, 0.0f);
        e.setParam(ParamId::Intensity, static_cast<float>(k));
        e.setParam(ParamId::Attack, 0.3f);
        e.playChord(2, ChordType::Major);
        auto x = render(e, 9.0);
        c[k] = tiltDb(mono(x), fr(3.0), fr(6.0)); // Welch em 6 s: estável frente às fases aleatórias
        l[k] = rmsDb(x, fr(3.0), fr(9.0));
    }
    report("Intensidade 0 -> 1: inclinação espectral (1–5 kHz vs 150–800 Hz) %.1f -> %.1f dB, nível %.1f -> %.1f dB",
           c[0], c[1], l[0], l[1]);
    CHECK(c[1] > c[0] + 2.5);
    CHECK(l[1] > l[0] + 3.0);
}

// Chorus ensemble: muda o caráter sem mudar o volume percebido nem criar cliques.
TEST(motor_chorus_nivel_e_cliques) {
    PadEngine& e = sharedEngine();
    double lv[2], corr[2], clk[2];
    for (int k = 0; k < 2; ++k) {
        resetEngine(e, 0);
        e.setParam(ParamId::Reverb, 0.0f);
        e.setParam(ParamId::Chorus, static_cast<float>(k));
        e.setParam(ParamId::Attack, 0.3f);
        e.playChord(4, ChordType::Minor);
        auto x = render(e, 8.0);
        lv[k] = rmsDb(x, fr(3.0), fr(8.0));
        corr[k] = lrCorrelation(x, fr(3.0), fr(8.0));
        clk[k] = clickScore(x, fr(2.0), fr(8.0));
    }
    report("chorus 0 -> 1: nível %.1f -> %.1f dB, correlação L/R %.2f -> %.2f, índice de clique %.2f / %.2f",
           lv[0], lv[1], corr[0], corr[1], clk[0], clk[1]);
    CHECK(std::fabs(lv[1] - lv[0]) < 2.0);
    CHECK(clk[0] < 3.0 && clk[1] < 3.0);
}
