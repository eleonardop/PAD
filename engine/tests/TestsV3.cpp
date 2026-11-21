// Testes da versão 0.3: maestro (regras musicais), texturas, transições, swell manual, mono,
// tempo real e estabilidade.
#include "TestFramework.h"
#include "AudioAnalysis.h"
#include <chrono>
#include <cmath>
#include <thread>
#include "core/Conductor.h"
#include "core/TexturePlayer.h"

using namespace pad;
using namespace padtest;

namespace {
constexpr float kSr = 48000.0f;
constexpr float kLimit = 0.8913f;
constexpr int kModern = 15, kVintage = 12;
size_t fr(double s) { return static_cast<size_t>(s * kSr); }

uint16_t chordMask(int root, ChordType t) {
    const Voicing v = buildVoicing(root, t);
    uint16_t m = 0;
    for (int i = 0; i < v.count; ++i) m = static_cast<uint16_t>(m | (1u << (v.notes[i] % 12)));
    return m;
}

RenderContext textureContext(float evo, float activity, float bloom, float chance) {
    RenderContext c;
    c.evolution = evo;
    c.activity = activity;
    c.bloom = bloom;
    c.bloomChance = chance;
    c.layerKeys = 1.0f;
    c.layerSparkle = 1.0f;
    c.layerMetal = 1.0f;
    c.brightness = 0.5f;
    c.swellTime = 4.0f;
    return c;
}

// Roda só o maestro (sem áudio): as notas escolhidas vão para o registro dele mesmo quando a
// fonte não está carregada.
void runConductor(Conductor& c, const RenderContext& ctx, double seconds) {
    SourceLibrary lib; // vazia: trigger() falha, o registro continua
    TexturePlayer tex;
    tex.prepare(kSr);
    const int steps = static_cast<int>(seconds * ctx.controlRate);
    for (int i = 0; i < steps; ++i) c.tick(ctx, lib, tex);
}

void zeroPadLayers(PadEngine& e) {
    for (ParamId id : {ParamId::LayerWarm, ParamId::LayerAir, ParamId::LayerStrings, ParamId::LayerShimmer, ParamId::LayerLow,
                       ParamId::LayerVox, ParamId::LayerTexture})
        e.setParam(id, 0.0f);
}

// Progressão G–D–Em–C com troca a cada `step` segundos até `until`.
void progression(PadEngine& e, double t, double step, double until, int& idx) {
    static const int roots[4] = {7, 2, 4, 0};
    static const ChordType ty[4] = {ChordType::Major, ChordType::Major, ChordType::Minor, ChordType::Major};
    if (t < until && t >= idx * step) { e.playChord(roots[idx % 4], ty[idx % 4]); ++idx; }
}
} // namespace

// Distância harmônica: zero no mesmo acorde, pequena em vizinhos, grande em acordes distantes.
TEST(maestro_distancia_harmonica) {
    const float same = Conductor::harmonicDistance(7, ChordType::Major, 7, ChordType::Major);
    const float rel = Conductor::harmonicDistance(7, ChordType::Major, 4, ChordType::Minor);   // G → Em
    const float iv = Conductor::harmonicDistance(7, ChordType::Major, 0, ChordType::Major);    // G → C
    const float v = Conductor::harmonicDistance(7, ChordType::Major, 2, ChordType::Major);     // G → D
    const float far = Conductor::harmonicDistance(7, ChordType::Major, 1, ChordType::Major);   // G → C#
    const float bb = Conductor::harmonicDistance(7, ChordType::Major, 10, ChordType::Major);   // G → Bb
    report("G→G %.2f, G→Em %.2f, G→C %.2f, G→D %.2f, G→Bb %.2f, G→C# %.2f", same, rel, iv, v, bb, far);
    CHECK(same == 0.0f);
    CHECK(rel < bb && iv < bb && v < bb);
    CHECK(far > 0.7f);
    CHECK(std::fabs(iv - Conductor::harmonicDistance(0, ChordType::Major, 7, ChordType::Major)) < 1e-6f);
}

// Toda nota gerada pertence ao acorde (ou é a 9ª permitida, só no brilho), no registro certo,
// nunca no instante da troca. 12 tons × 10 tipos, 40 s cada, atividade e Evolução no máximo.
TEST(maestro_notas_pertencem_ao_acorde) {
    long total = 0, bad = 0, badRegister = 0, early = 0, thirdsOnPad5 = 0;
    for (int root = 0; root < 12; ++root) {
        for (int t = 0; t < static_cast<int>(ChordType::Count); ++t) {
            Conductor c;
            RenderContext ctx = textureContext(1.0f, 1.0f, 1.0f, 1.0f);
            c.prepare(ctx.controlRate, 1000 + root * 17 + t);
            const ChordType type = static_cast<ChordType>(t);
            c.onChord(buildVoicing(root, type), type, root);
            runConductor(c, ctx, 40.0);
            const uint16_t mask = chordMask(root, type);
            for (int i = 0; i < c.logCount() && i < Conductor::kLog; ++i) {
                const NoteLogEntry& n = c.logAt(i);
                ++total;
                const int pc = n.midi % 12;
                bool ok = (mask >> pc) & 1;
                const bool ninthAllowed = type != ChordType::Sus4 && type != ChordType::Minor7 && type != ChordType::Dominant7;
                if (!ok && n.instrument == 1 && ninthAllowed && pc == (root + 2) % 12) ok = true;
                if (!ok) ++bad;
                if (type == ChordType::Pad5 && (pc == (root + 3) % 12 || pc == (root + 4) % 12)) ++thirdsOnPad5;
                const bool reg = n.instrument == 1 ? (n.midi >= 72 && n.midi <= 88) : (n.midi >= 64 && n.midi <= 86);
                if (!reg) ++badRegister;
                if (n.time < 0.5f) ++early;
            }
        }
    }
    report("%ld notas geradas: %ld fora do acorde, %ld fora do registro, %ld antes de 0,5 s, %ld terças no Pad5",
           total, bad, badRegister, early, thirdsOnPad5);
    CHECK(total > 1000);
    CHECK(bad == 0);
    CHECK(badRegister == 0);
    CHECK(early == 0);
    CHECK(thirdsOnPad5 == 0);
}

// Linha melódica: saltos de no máximo uma 5ª entre notas consecutivas do mesmo acorde (exceto
// a nota dobrada por baixo, que é registrada logo depois da principal).
TEST(maestro_linha_melodica_sem_saltos_grandes) {
    Conductor c;
    RenderContext ctx = textureContext(0.5f, 1.0f, 0.0f, 0.0f);
    ctx.layerSparkle = 0.0f;
    c.prepare(ctx.controlRate, 77);
    c.onChord(buildVoicing(2, ChordType::Major), ChordType::Major, 2);
    runConductor(c, ctx, 300.0);
    int big = 0, repeats3 = 0;
    for (int i = 1; i < c.logCount() && i < Conductor::kLog; ++i) {
        const int a = c.logAt(i - 1).midi, b = c.logAt(i).midi;
        if (std::abs(a - b) > 7) ++big;
        if (i >= 2 && a == b && c.logAt(i - 2).midi == a) ++repeats3;
    }
    report("%d notas em 300 s; saltos > 5ª: %d; mesma nota 3x seguidas: %d", c.logCount(), big, repeats3);
    CHECK(c.logCount() > 40);
    CHECK(big == 0);
    CHECK(repeats3 == 0);
}

// Com Atividade e Bloom em zero o maestro não gera nada.
TEST(maestro_controles_zerados_sem_eventos) {
    Conductor c;
    RenderContext ctx = textureContext(1.0f, 0.0f, 0.0f, 0.0f);
    c.prepare(ctx.controlRate, 5);
    for (int k = 0; k < 20; ++k) {
        c.onChord(buildVoicing(k * 5 % 12, ChordType::Major), ChordType::Major, k * 5 % 12);
        runConductor(c, ctx, 6.0);
    }
    report("eventos %u, blooms %u", c.noteEvents(), c.blooms());
    CHECK(c.noteEvents() == 0);
    CHECK(c.blooms() == 0);
}

// Trocas rápidas (0,8 s) quase não disparam bloom; mudanças distantes disparam mais que
// mudanças vizinhas.
TEST(maestro_bloom_condicionado) {
    Conductor fast;
    RenderContext ctx = textureContext(0.5f, 0.0f, 1.0f, 1.0f);
    fast.prepare(ctx.controlRate, 9);
    for (int k = 0; k < 75; ++k) {
        fast.onChord(buildVoicing(k * 7 % 12, ChordType::Major), ChordType::Major, k * 7 % 12);
        runConductor(fast, ctx, 0.8);
    }
    ctx = textureContext(0.5f, 0.0f, 0.6f, 0.5f);
    Conductor nearC, farC;
    nearC.prepare(ctx.controlRate, 11);
    farC.prepare(ctx.controlRate, 11);
    for (int k = 0; k < 60; ++k) {
        const bool odd = k % 2;
        nearC.onChord(buildVoicing(odd ? 4 : 7, odd ? ChordType::Minor : ChordType::Major), odd ? ChordType::Minor : ChordType::Major, odd ? 4 : 7);
        farC.onChord(buildVoicing(odd ? 1 : 7, ChordType::Major), ChordType::Major, odd ? 1 : 7);
        runConductor(nearC, ctx, 6.0);
        runConductor(farC, ctx, 6.0);
    }
    report("trocas a cada 0,8 s: %u blooms em 75 trocas; G↔Em: %u, G↔C#: %u em 60 trocas", fast.blooms(), nearC.blooms(), farC.blooms());
    CHECK(fast.blooms() <= 1); // só o primeiro acorde, vindo do silêncio
    CHECK(farC.blooms() > nearC.blooms());
    CHECK(nearC.blooms() < 60);
}

// EP por FM: afinado e terminando em silêncio.
TEST(ep_fm_afinado) {
    ZoneBank b;
    CHECK(SourceLibrary::buildEp(kSr, b));
    double worst = 0;
    for (size_t i = 0; i < b.size(); ++i) {
        const Zone& z = *b.zoneAt(i);
        std::vector<float> m(static_cast<size_t>(std::min(z.length, 1 << 15)));
        for (size_t k = 0; k < m.size(); ++k) m[k] = z.l[k + 1] * z.scale;
        const double f = peakFrequency(m, kSr, 0, 1 << 15, z.rootHz * 0.8, z.rootHz * 1.2);
        worst = std::max(worst, std::fabs(1200.0 * std::log2(f / z.rootHz)));
        CHECK(z.oneShot);
        CHECK(std::fabs(z.l[static_cast<size_t>(z.length)] * z.scale) < 1e-3f);
    }
    report("%zu zonas de EP, maior desvio de afinação %.2f cents, %.1f MB", b.size(), worst, b.bytes() / 1e6);
    CHECK(b.size() >= 12);
    CHECK(worst < 3.0);
}

// Reverse: o pico fica no fim e o começo é silencioso.
TEST(reverse_cresce_ate_o_fim) {
    ZoneBank in, out;
    std::vector<float> x(static_cast<size_t>(kSr * 4));
    for (size_t i = 0; i < x.size(); ++i) x[i] = std::sin(0.05f * i) * std::exp(-static_cast<float>(i) / (kSr * 0.8f));
    Zone z;
    z.sampleRate = kSr;
    z.setFromFloat(x.data(), nullptr, static_cast<int>(x.size()));
    z.makeOneShotGuards();
    in.add(std::move(z));
    SourceLibrary::buildReversed(in, out, 3.0);
    CHECK(out.size() == 1);
    const Zone& r = *out.zoneAt(0);
    double a = 0, b2 = 0;
    for (int i = 0; i < 4800; ++i) a += std::fabs(r.l[static_cast<size_t>(i + 1)] * r.scale);
    for (int i = r.length - 9600; i < r.length - 4800; ++i) b2 += std::fabs(r.l[static_cast<size_t>(i + 1)] * r.scale);
    report("reverse de 3 s: nível no início %.4f, perto do fim %.4f, pico em %.2f s", a / 4800, b2 / 4800, r.peakFrame / kSr);
    CHECK(b2 > a * 20);
    CHECK(r.peakFrame > r.length - 0.1 * kSr);
}

// Mix completo do preset Modern Worship com swells e trocas rápidas: sem alocação, sem clipping.
TEST(texturas_tempo_real_sem_alocacao_e_sem_clip) {
    PadEngine& e = sharedEngine();
    resetEngine(e, kModern);
    e.setParam(ParamId::Activity, 1.0f);
    e.setParam(ParamId::Bloom, 1.0f);
    e.setParam(ParamId::BloomChance, 1.0f);
    e.setParam(ParamId::LayerMetal, 1.0f);
    e.setParam(ParamId::LayerSparkle, 1.0f);
    e.setParam(ParamId::Evolution, 1.0f);
    e.setParam(ParamId::SwellTime, 2.0f);
    e.waitForSources(60.0);
    render(e, 0.5);
    g_allocationCount = 0;
    int maxTex = 0;
    double nextChord = 0.0, nextSwell = 1.0;
    Rng rng(3);
    auto x = render(e, 30.0, 192, [&](double t) {
        if (t >= nextChord) {
            e.playChord(static_cast<int>(rng.uniform() * 12), static_cast<ChordType>(static_cast<int>(rng.uniform() * 10)));
            nextChord = t + (t < 15.0 ? 4.5 : 0.35); // primeiro trocas normais, depois muito rápidas
        }
        if (t >= nextSwell) { e.swell(); nextSwell = t + 2.5; }
        maxTex = std::max(maxTex, e.texturesForTests().activeCount());
    }, true);
    const auto st = e.readStats();
    report("alocações %ld, pico %.3f, até %d vozes de textura, %u eventos, %u blooms, %u swells, roubos de textura %llu",
           g_allocationCount.load(), peakAbs(x), maxTex, st.textureEvents, st.blooms, st.swells,
           static_cast<unsigned long long>(e.texturesForTests().stealCount()));
    CHECK(allFinite(x));
    CHECK(g_allocationCount.load() == 0);
    CHECK(peakAbs(x) <= kLimit);
    CHECK(maxTex <= TexturePlayer::kVoices + TexturePlayer::kReserve);
    CHECK(st.swells > 3);
}

// As texturas ficam por baixo do pad (≥ 8 dB) no preset Modern Worship.
TEST(texturas_abaixo_do_pad) {
    PadEngine& e = sharedEngine();
    double lv[2];
    for (int k = 0; k < 2; ++k) {
        resetEngine(e, kModern);
        if (k == 0) zeroPadLayers(e);                    // só texturas e transições
        else for (ParamId id : {ParamId::LayerKeys, ParamId::LayerSparkle, ParamId::LayerMetal, ParamId::Bloom})
            e.setParam(id, 0.0f);                        // só o pad
        e.waitForSources(60.0);
        int idx = 0;
        auto x = render(e, 50.0, 192, [&](double t) { progression(e, t, 6.0, 48.0, idx); });
        lv[k] = rmsDb(x, fr(2.0), fr(50.0));
    }
    report("texturas sozinhas %.1f dB, pad sozinho %.1f dB (diferença %.1f dB)", lv[0], lv[1], lv[1] - lv[0]);
    CHECK(lv[1] - lv[0] >= 8.0);
    CHECK(lv[0] > -70.0); // as texturas existem
}

// Pre-swell manual: cresce até o fim do SwellTime, sem pad. Testa um tempo menor e um maior
// que os arquivos de swell/reverse (o caminho com atraso inicial).
TEST(swell_manual_pico_no_fim) {
    PadEngine& e = sharedEngine();
    for (float T : {3.0f, 6.5f}) {
        resetEngine(e, kModern);
        zeroPadLayers(e);
        e.setParam(ParamId::Activity, 0.0f);
        e.setParam(ParamId::Bloom, 0.0f);
        e.setParam(ParamId::SwellTime, T);
        e.setParam(ParamId::Reverb, 0.0f);
        e.waitForSources(60.0);
        auto x = render(e, 2.0 + T + 3.0, 192, [&](double t) {
            if (t == 0.0) e.playChord(9, ChordType::Minor);
            if (t >= 2.0 && t - 0.004 < 2.0) e.swell();
        });
        auto w = windowRmsDb(x, 4800); // 100 ms
        size_t best = 20;
        for (size_t i = 20; i < w.size(); ++i) if (w[i] > w[best]) best = i;
        const double peakT = best * 0.1 - 2.0;
        const size_t early = 20 + static_cast<size_t>(T * 10 * 0.25);
        report("SwellTime %.1f s: pico %.2f s depois do comando; nível a 1/4 do tempo %.1f dB, no pico %.1f dB", T, peakT, w[early], w[best]);
        CHECK(peakT > T - 0.7 && peakT < T + 0.4);
        CHECK(w[best] - w[early] > 6.0);
    }
}

// Compatibilidade mono do mix completo: perda total ≤ 3 dB e nenhuma banda de 1/3 de oitava
// cancelando mais de 6 dB na soma mono.
TEST(mono_v03_sem_cancelamento) {
    PadEngine& e = sharedEngine();
    resetEngine(e, kModern);
    e.waitForSources(60.0);
    int idx = 0;
    auto x = render(e, 36.0, 192, [&](double t) {
        progression(e, t, 6.0, 34.0, idx);
        if (t >= 10.0 && t - 0.004 < 10.0) e.swell();
    });
    const size_t n = 1 << 15;
    double worstBand = 0.0, totSt = 0.0, totMono = 0.0;
    std::vector<double> bandSt(31, 0.0), bandMono(31, 0.0);
    std::vector<float> l(n), r(n), m(n);
    for (size_t start = fr(2.0); start + n < x.size() / 2; start += n / 2) {
        for (size_t i = 0; i < n; ++i) { l[i] = x[2 * (start + i)]; r[i] = x[2 * (start + i) + 1]; m[i] = 0.5f * (l[i] + r[i]); }
        auto sl = magnitudeSpectrumDb(l, 0, n), sr = magnitudeSpectrumDb(r, 0, n), sm = magnitudeSpectrumDb(m, 0, n);
        for (size_t k = 1; k < sl.size(); ++k) {
            const double f = k * kSr / n;
            if (f < 100 || f > 10000) continue;
            const int b = static_cast<int>(std::floor(3.0 * std::log2(f / 100.0)));
            const double pst = 0.5 * (std::pow(10.0, sl[k] / 10) + std::pow(10.0, sr[k] / 10));
            const double pm = std::pow(10.0, sm[k] / 10);
            bandSt[static_cast<size_t>(b)] += pst;
            bandMono[static_cast<size_t>(b)] += pm;
            totSt += pst;
            totMono += pm;
        }
    }
    for (size_t b = 0; b < bandSt.size(); ++b)
        if (bandSt[b] > 0) worstBand = std::min(worstBand, 10 * std::log10(bandMono[b] / bandSt[b]));
    const double loss = 10 * std::log10(totMono / totSt);
    report("perda em mono %.2f dB; pior banda de 1/3 de oitava %.2f dB", loss, worstBand);
    CHECK(loss > -3.0);
    CHECK(worstBand > -6.0);
}

// A fita aplica a mesma modulação nos dois canais: a relação L/R não muda.
TEST(tape_seguro_em_mono) {
    PadEngine& e = sharedEngine();
    double corr[2];
    for (int k = 0; k < 2; ++k) {
        resetEngine(e, kVintage);
        e.setParam(ParamId::Activity, 0.0f);
        e.setParam(ParamId::Bloom, 0.0f);
        e.setParam(ParamId::Tape, k == 0 ? 0.0f : 1.0f);
        e.waitForSources(60.0);
        auto x = render(e, 8.0, 192, [&](double t) { if (t == 0.0) e.playChord(0, ChordType::Major); });
        corr[k] = lrCorrelation(x, fr(3.0), fr(8.0));
    }
    report("correlação L/R sem fita %.3f, com fita %.3f", corr[0], corr[1]);
    CHECK(std::fabs(corr[0] - corr[1]) < 0.08);
}

// Texturas carregadas sob demanda e liberadas quando desligadas.
TEST(texturas_fontes_sob_demanda) {
    PadEngine& e = sharedEngine();
    resetEngine(e, 0); // Warm (v0.2): nenhuma textura
    render(e, 1.0);
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    render(e, 0.5);
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    const size_t off = e.readStats().sourcesMemoryBytes;
    e.setParam(ParamId::LayerKeys, 0.5f);
    e.setParam(ParamId::LayerMetal, 0.5f);
    e.waitForSources(60.0);
    const auto st = e.readStats();
    const bool ready = (st.sourcesReady & sourceBit(SourceId::PianoSoft)) && (st.sourcesReady & sourceBit(SourceId::Cymbal)) &&
                       (st.sourcesReady & sourceBit(SourceId::PianoSoftRev));
    e.setParam(ParamId::LayerKeys, 0.0f);
    e.setParam(ParamId::LayerMetal, 0.0f);
    render(e, 1.0);
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    render(e, 0.5);
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    const size_t back = e.readStats().sourcesMemoryBytes;
    report("memória sem texturas %.1f MB, com piano+pratos %.1f MB, depois de desligar %.1f MB", off / 1e6,
           st.sourcesMemoryBytes / 1e6, back / 1e6);
    CHECK(ready);
    CHECK(st.sourcesMemoryBytes > off + 10'000'000);
    CHECK(back < st.sourcesMemoryBytes - 10'000'000);
}

// 2 minutos do mix completo com swells: nível estável, sem NaN, sem alocação.
TEST(v03_estabilidade_2min) {
    PadEngine& e = sharedEngine();
    resetEngine(e, kModern);
    e.setParam(ParamId::Evolution, 1.0f);
    e.waitForSources(60.0);
    g_allocationCount = 0;
    int idx = 0;
    auto x = render(e, 120.0, 192, [&](double t) {
        progression(e, t, 7.5, 119.0, idx);
        if (std::fmod(t, 20.0) < 0.004 && t > 1.0) e.swell();
    }, true);
    double lo = 1e9, hi = -1e9;
    for (int k = 1; k < 12; ++k) {
        const double v = rmsDb(x, fr(k * 10.0), fr(k * 10.0 + 10.0));
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }
    report("RMS por 10 s entre %.1f e %.1f dB, pico %.3f, alocações %ld", lo, hi, peakAbs(x), g_allocationCount.load());
    CHECK(allFinite(x));
    CHECK(hi - lo < 4.0);
    CHECK(peakAbs(x) <= kLimit);
    CHECK(g_allocationCount.load() == 0);
}
