// Testes das fontes e efeitos da versão 0.2: PADsynth, samples reais, Air, filtro ladder,
// ensemble, reverbs (Plate e Cloud Seed), reflexões iniciais, compressor e carregamento sob demanda.
#include "TestFramework.h"
#include "AudioAnalysis.h"
#include "core/Sources.h"
#include "core/SampleBank.h"
#include "dsp/PadSynth.h"
#include "dsp/Ladder.h"
#include "dsp/Ensemble.h"
#include "dsp/DattorroPlate.h"
#include "dsp/CloudReverb.h"
#include "dsp/ClassicReverb.h"
#include "dsp/EarlyReflections.h"
#include "dsp/GlueCompressor.h"
#include "dsp/Random.h"
#include <chrono>
#include <cmath>
#include <thread>

using namespace pad;
using namespace padtest;

namespace {
std::vector<float> zoneToFloat(const Zone& z, bool right = false) {
    std::vector<float> v(static_cast<size_t>(z.length));
    const auto& d = (right && z.channels == 2) ? z.r : z.l;
    for (int i = 0; i < z.length; ++i) v[static_cast<size_t>(i)] = d[static_cast<size_t>(i + 1)] * z.scale;
    return v;
}
double rmsOf(const std::vector<float>& v) {
    double e = 0;
    for (float x : v) e += double(x) * x;
    return std::sqrt(e / std::max<size_t>(v.size(), 1));
}
// Energia (fração) do espectro acima/abaixo de uma frequência, numa janela de 2^16.
double bandFraction(const std::vector<float>& v, double sr, double lo, double hi) {
    const size_t n = 1 << 16;
    auto db = magnitudeSpectrumDb(v, 0, n);
    double in = 0, tot = 0;
    for (size_t k = 1; k < db.size(); ++k) {
        const double p = std::pow(10.0, db[k] / 10.0), f = k * sr / n;
        tot += p;
        if (f >= lo && f < hi) in += p;
    }
    return in / (tot + 1e-30);
}
} // namespace

// PADsynth: loop perfeito e espectro com largura (ensemble), limitado em agudos na origem.
TEST(padsynth_loop_largura_e_agudos) {
    ZoneBank bank;
    CHECK(SourceLibrary::buildPadSynth(SourceId::WarmDark, 48000.0f, 1 << 18, bank));
    CHECK(bank.size() == 15);
    const Zone& z = *bank.zoneAt(6); // nota 58
    auto v = zoneToFloat(z);
    double maxD = 0;
    for (size_t i = 1; i < v.size(); ++i) maxD = std::max(maxD, double(std::fabs(v[i] - v[i - 1])));
    const double joint = std::fabs(v.front() - v.back());
    report("nota %d: salto na junção do loop %.5f (maior salto interno %.5f), RMS %.1f dBFS", z.rootNote, joint, maxD, 20 * std::log10(rmsOf(v)));
    CHECK(joint < maxD * 0.5);
    CHECK(std::fabs(20 * std::log10(rmsOf(v)) + 20.0) < 0.6); // normalizado em -20 dBFS (int16 com escala)

    // Largura do 1º harmônico: a energia em ±40 cents tem de se espalhar (não é uma linha fina).
    const double f0 = z.rootHz;
    const double inNarrow = bandFraction(v, 48000.0, f0 * std::exp2(-3.0 / 1200), f0 * std::exp2(3.0 / 1200));
    const double inWide = bandFraction(v, 48000.0, f0 * std::exp2(-40.0 / 1200), f0 * std::exp2(40.0 / 1200));
    report("1º harmônico: %.1f%% da energia em ±40 cents, só %.1f%% em ±3 cents (faixa larga, não linha)", 100 * inWide, 100 * inNarrow);
    CHECK(inNarrow < 0.5 * inWide);
    const double hf = bandFraction(v, 48000.0, 10000.0, 24000.0);
    report("energia acima de 10 kHz: %.1f dB", 10 * std::log10(hf + 1e-30));
    CHECK(10 * std::log10(hf + 1e-30) < -60.0);
}

// Morph: as duas tabelas da família têm as mesmas fases, então a mistura não "cava" o volume.
TEST(padsynth_morph_sem_cancelamento) {
    for (auto fam : {std::pair<SourceId, SourceId>{SourceId::WarmDark, SourceId::WarmOpen},
                     std::pair<SourceId, SourceId>{SourceId::VoxOh, SourceId::VoxAh},
                     std::pair<SourceId, SourceId>{SourceId::AirA, SourceId::AirB}}) {
        ZoneBank a, b;
        SourceLibrary::buildPadSynth(fam.first, 48000.0f, 1 << 17, a);
        SourceLibrary::buildPadSynth(fam.second, 48000.0f, 1 << 17, b);
        const auto va = zoneToFloat(*a.zoneAt(5)), vb = zoneToFloat(*b.zoneAt(5));
        const double ra = rmsOf(va), rb = rmsOf(vb);
        double worst = 0;
        for (double m : {0.25, 0.5, 0.75}) {
            std::vector<float> mix(va.size());
            for (size_t i = 0; i < va.size(); ++i) mix[i] = static_cast<float>(va[i] * (1 - m) + vb[i] * m);
            const double expected = ra * (1 - m) + rb * m;              // interpolação de espectros
            worst = std::max(worst, std::fabs(20 * std::log10(rmsOf(mix) / expected)));
        }
        report("%s <-> %s: maior desvio de nível no morph %.2f dB", sourceName(fam.first), sourceName(fam.second), worst);
        CHECK(worst < 1.5);
    }
}

// Air: harmônico, sem conteúdo grave e sem agudos ásperos (corrigido na origem, não no volume).
TEST(air_harmonico_sem_chiado) {
    for (SourceId s : {SourceId::AirA, SourceId::AirB}) {
        ZoneBank bank;
        SourceLibrary::buildPadSynth(s, 48000.0f, 1 << 17, bank);
        const auto v = zoneToFloat(*bank.zoneAt(6));
        const double low = bandFraction(v, 48000.0, 0, 500), hf = bandFraction(v, 48000.0, 9000, 24000),
                     mid = bandFraction(v, 48000.0, 1000, 5000);
        report("%s: <500 Hz %.1f dB, 1–5 kHz %.0f%%, >9 kHz %.1f dB", sourceName(s), 10 * std::log10(low + 1e-30), 100 * mid,
               10 * std::log10(hf + 1e-30));
        CHECK(10 * std::log10(low + 1e-30) < -30.0);
        CHECK(10 * std::log10(hf + 1e-30) < -35.0);
        CHECK(mid > 0.6);
    }
}

// Samples reais: os três conjuntos carregam, afinados e normalizados, com loop contínuo.
TEST(samples_reais_carregam_afinados) {
    FileAssetProvider assets(PAD_ASSETS_DIR);
    for (const char* set : {"strings", "piano_cathedral", "organ"}) {
        ZoneBank bank;
        const auto t0 = std::chrono::steady_clock::now();
        const bool ok = SourceLibrary::loadSampleSet(assets, set, bank);
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        CHECK(ok);
        if (!ok) continue;
        double worstCents = 0, worstJump = 0, minDb = 0, maxDb = -200;
        for (size_t i = 0; i < bank.size(); ++i) {
            const Zone& z = *bank.zoneAt(i);
            CHECK(z.sampleRate == 48000.0f);
            CHECK(z.channels == 2);
            worstCents = std::max(worstCents, std::fabs(1200.0 * std::log2(z.rootHz / midiToHz(z.rootNote))));
            auto l = zoneToFloat(z);
            // Salto na junção do loop comparado ao percentil 99,9 dos saltos internos.
            std::vector<float> d(l.size() - 1);
            for (size_t k = 1; k < l.size(); ++k) d[k - 1] = std::fabs(l[k] - l[k - 1]);
            std::nth_element(d.begin(), d.begin() + static_cast<long>(d.size() * 0.999), d.end());
            const double p999 = std::max(1e-9f, d[static_cast<size_t>(d.size() * 0.999)]);
            worstJump = std::max(worstJump, std::fabs(l.front() - l.back()) / p999);
            // RMS estéreo (média L/R): o piano tem panorâmica por registro, então um canal só
            // varia de nota para nota mesmo com a zona normalizada.
            const auto rr = zoneToFloat(z, true);
            const double db = 10 * std::log10(0.5 * (rmsOf(l) * rmsOf(l) + rmsOf(rr) * rmsOf(rr)));
            minDb = std::min(minDb, db);
            maxDb = std::max(maxDb, db);
        }
        report("%-16s %zu zonas, %.1f MB, %.2f s; maior desvio da raiz %.1f cents; junção/salto p99,9 %.2f; RMS %.1f a %.1f dBFS",
               set, bank.size(), bank.bytes() / 1e6, secs, worstCents, worstJump, minDb, maxDb);
        CHECK(bank.size() >= 15);
        CHECK(worstCents < 50.0);   // a raiz medida pode ter alguns cents; o motor usa a frequência medida
        CHECK(worstJump < 1.0);
        CHECK(minDb > -20.5 && maxDb < -19.5);
    }
}

// Filtro ladder: 24 dB/oitava, passa-banda plano, estável sob modulação rápida.
TEST(filtro_ladder_resposta_e_estabilidade) {
    auto gainAt = [](float f, float fc) {
        LadderFilter lf;
        lf.setCoefficients(fc, 0.0f, 48000.0f);
        lf.setDrive(0.05f); // praticamente linear
        double e = 0, ei = 0;
        for (int i = 0; i < 48000; ++i) {
            const float x = 0.1f * std::sin(2.0f * kPi * f * i / 48000.0f);
            const float y = lf.process(x);
            if (i > 24000) { e += double(y) * y; ei += double(x) * x; }
        }
        return 10 * std::log10(e / ei);
    };
    const double pass = gainAt(250.0f, 2000.0f), stop = gainAt(8000.0f, 2000.0f);
    report("corte 2 kHz: 250 Hz %.2f dB, 8 kHz %.1f dB (2 oitavas acima)", pass, stop);
    CHECK(std::fabs(pass) < 1.0);
    CHECK(stop < -38.0);
    LadderFilter lf;
    Rng rng(4);
    float peak = 0;
    for (int i = 0; i < 48000 * 2; ++i) {
        if (i % 32 == 0) lf.setCoefficients(expMap(rng.uniform(), 60.0f, 18000.0f), 0.7f, 48000.0f);
        const float y = lf.process(rng.bipolar());
        CHECK(std::isfinite(y));
        peak = std::max(peak, std::fabs(y));
    }
    CHECK(peak < 4.0f);
}

// Ensemble: nível preservado e saída estável.
TEST(ensemble_nivel_e_estabilidade) {
    EnsembleChorus ens;
    ens.prepare(48000.0f);
    ens.setDepth(1.0f);
    Rng rng(6);
    double ein = 0, eout = 0;
    float lp = 0;
    for (int i = 0; i < 48000 * 4; ++i) {
        lp = 0.95f * lp + 0.05f * rng.bipolar();
        float l, r;
        ens.process(lp, lp, l, r);
        CHECK(std::isfinite(l) && std::isfinite(r));
        if (i > 4800) { ein += double(lp) * lp; eout += 0.5 * (double(l) * l + double(r) * r); }
    }
    report("ensemble: ganho de nível %.2f dB", 10 * std::log10(eout / ein));
    CHECK(std::fabs(10 * std::log10(eout / ein)) < 2.0);
}

// Reverbs: RT60 coerente, estáveis e sem alocação no processamento (nem ao mudar parâmetros).
TEST(reverbs_rt60_estabilidade_sem_alocacao) {
    std::unique_ptr<ReverbBase> rv[3] = {std::make_unique<ClassicReverb>(), std::make_unique<DattorroPlate>(), std::make_unique<CloudReverb>()};
    const char* names[3] = {"Clássico (FDN)", "Plate (Dattorro)", "Cloud Seed"};
    for (int k = 0; k < 3; ++k) {
        ScopedNoDenormals nd;
        rv[k]->prepare(48000.0f);
        ReverbParams p;
        p.decaySeconds = 3.0f;
        p.early = 0.0f;
        rv[k]->setParams(p);
        std::vector<double> e(28, 0.0); // energia por 0,25 s
        float il[32], ir[32], ol[32], orr[32];
        bool finite = true;
        g_allocationCount = 0;
        const auto t0 = std::chrono::steady_clock::now();
        for (int b = 0; b < 48000 * 7 / 32; ++b) {
            for (int i = 0; i < 32; ++i) il[i] = ir[i] = (b < 8) ? (((i * 7 + b) % 13) / 13.0f - 0.5f) : 0.0f;
            if (b % 50 == 0) { p.damping = 0.3f + 0.2f * ((b / 50) % 3); g_trackAllocations = true; rv[k]->setParams(p); g_trackAllocations = false; }
            g_trackAllocations = true;
            rv[k]->process(il, ir, ol, orr, 32);
            g_trackAllocations = false;
            for (int i = 0; i < 32; ++i) {
                finite &= std::isfinite(ol[i]) && std::isfinite(orr[i]);
                e[static_cast<size_t>((b * 32 + i) / 12000)] += double(ol[i]) * ol[i] + double(orr[i]) * orr[i];
            }
        }
        const double cpu = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() / 7.0;
        // Taxa de queda entre 0,5 s e 2,9 s (80% do RT60, antes do chão numérico).
        const double rate = 10 * std::log10(e[2] / e[11]) / ((11 - 2) * 0.25);
        report("%-18s queda %.1f dB/s (RT60 3 s = 20 dB/s), alocações %ld, CPU %.2f%% de um núcleo", names[k], rate,
               g_allocationCount.load(), 100 * cpu);
        CHECK(finite);
        CHECK(g_allocationCount.load() == 0);
        CHECK_MSG(rate > 12.0 && rate < 30.0, "%s: %.1f dB/s", names[k], rate);
    }
}

// Reflexões iniciais: o primeiro eco chega depois do pré-delay pedido.
TEST(reflexoes_iniciais_predelay) {
    EarlyReflections er;
    er.prepare(48000.0f);
    er.set(0.040f, 0.5f, 1.0f);
    int first = -1;
    for (int i = 0; i < 48000 / 4; ++i) {
        float d, l, r;
        er.process(i == 0 ? 1.0f : 0.0f, d, l, r);
        if (first < 0 && (std::fabs(l) > 1e-4f || std::fabs(r) > 1e-4f)) first = i;
    }
    report("primeira reflexão em %.1f ms (pré-delay 40 ms + primeiro tap ~6 ms)", first / 48.0);
    CHECK(first > static_cast<int>(0.044 * 48000) && first < static_cast<int>(0.060 * 48000));
}

// Compressor de cola: transparente abaixo do limiar, suave acima.
TEST(compressor_cola_suave) {
    GlueCompressor g;
    g.prepare(48000.0f, 32);
    g.setThresholdDb(-17.0f);
    g.setRatio(1.6f);
    auto run = [&](float amp) {
        g.reset();
        float l[32], r[32];
        double ein = 0, eout = 0;
        for (int b = 0; b < 48000 * 3 / 32; ++b) {
            for (int i = 0; i < 32; ++i) l[i] = r[i] = amp * std::sin(2.0f * kPi * 220.0f * (b * 32 + i) / 48000.0f);
            for (int i = 0; i < 32; ++i) ein += double(l[i]) * l[i];
            g.process(l, r, 32);
            if (b > 48000 * 2 / 32) for (int i = 0; i < 32; ++i) eout += double(l[i]) * l[i];
        }
        return 10 * std::log10(eout / (ein / 3.0) + 1e-30);
    };
    const double quiet = run(0.05f), loud = run(0.5f);
    report("sinal a -29 dBFS RMS: %.2f dB; a -9 dBFS RMS: %.2f dB de redução", quiet, loud);
    CHECK(std::fabs(quiet) < 0.2);
    CHECK(loud < -2.0 && loud > -4.5);
}

// Carregamento sob demanda: carrega quando pedido, libera só quando nenhuma voz usa.
TEST(fontes_sob_demanda_liberacao_segura) {
    SourceLibrary lib;
    lib.configure(48000.0f, nullptr, 1 << 16);
    lib.start();
    lib.setWanted(sourceBit(SourceId::Glass));
    CHECK(lib.waitReady(sourceBit(SourceId::Glass), 20.0));
    CHECK(lib.get(SourceId::Glass) != nullptr);
    const size_t mem = lib.memoryBytes();
    uint16_t usage[kSourceCount] = {0};
    usage[static_cast<int>(SourceId::Glass)] = 1;  // uma voz ainda usando
    lib.publishUsage(usage);
    lib.setWanted(0);
    for (int i = 0; i < 20; ++i) { lib.publishUsage(usage); std::this_thread::sleep_for(std::chrono::milliseconds(20)); }
    CHECK(lib.get(SourceId::Glass) == nullptr);                       // novas vozes não pegam mais
    CHECK(lib.state(SourceId::Glass) == SourceLibrary::State::Retiring); // mas a memória continua lá
    CHECK(lib.memoryBytes() == mem);
    usage[static_cast<int>(SourceId::Glass)] = 0;
    for (int i = 0; i < 30; ++i) { lib.publishUsage(usage); std::this_thread::sleep_for(std::chrono::milliseconds(20)); }
    report("memória: %.1f MB com a fonte; %.1f MB depois de liberada", mem / 1e6, lib.memoryBytes() / 1e6);
    CHECK(lib.state(SourceId::Glass) == SourceLibrary::State::Unloaded);
    CHECK(lib.memoryBytes() == 0);
    lib.stop();
}
