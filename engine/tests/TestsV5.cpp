// Testes da v0.5: pads de fundo (SOFT PADS), voicing com a quinta por baixo, Abafar e guitarra.
#include "TestFramework.h"
#include "AudioAnalysis.h"
#include <cmath>
#include <cstring>
#include <memory>
#define private public // acesso ao gerador da guitarra (função estática do carregador)
#include "core/Sources.h"
#undef private
#include "core/Conductor.h"
#include "core/TexturePlayer.h"

using namespace pad;
using namespace padtest;

namespace {
constexpr float kSr = 48000.0f;
constexpr float kLimit = 0.8913f;
size_t fr(double s) { return static_cast<size_t>(s * kSr); }

bool isSoftPad(int i) { return presetIsCurrent(i) && std::strcmp(preset(i).category, "SOFT PADS") == 0; }
int presetByName(const char* n) {
    for (int i = 0; i < presetCount(); ++i) if (std::strcmp(preset(i).name, n) == 0) return i;
    return -1;
}
uint16_t pcMaskOf(const Voicing& v) {
    uint16_t m = 0;
    for (int i = 0; i < v.count; ++i) m = static_cast<uint16_t>(m | (1u << (v.notes[i] % 12)));
    return m;
}
// Energia (dB) acima de fLo, relativa ao total, somada em todas as janelas de 'start' até o fim.
double bandShareDb(const Stereo& x, size_t start, double fLo) {
    const size_t n = 1 << 14;
    auto m = mono(x);
    double hi = 0, tot = 0;
    for (size_t w = start; w + n <= m.size(); w += n / 2) {
        auto s = magnitudeSpectrumDb(m, w, n);
        for (size_t k = 1; k < s.size(); ++k) {
            const double p = std::pow(10.0, s[k] / 10), f = k * kSr / n;
            tot += p;
            if (f >= fLo) hi += p;
        }
    }
    return 10 * std::log10(hi / tot);
}
} // namespace

// Quinta por baixo: mesmas classes de altura do voicing padrão, a 5ª abaixo da fundamental
// (quando ela não desce além de Sol2), Pad5 sem terça, notas em ordem e dentro do limite.
TEST(voicing_quinta_por_baixo) {
    int bad = 0, withFifthBelow = 0;
    for (int root = 0; root < 12; ++root) {
        for (int t = 0; t < static_cast<int>(ChordType::Count); ++t) {
            const ChordType ct = static_cast<ChordType>(t);
            const Voicing a = buildVoicing(root, ct, 0), b = buildVoicing(root, ct, 1);
            if (pcMaskOf(a) != pcMaskOf(b)) ++bad;
            if (b.count < 4 || b.count > kMaxChordNotes) ++bad;
            for (int i = 1; i < b.count; ++i) if (b.notes[i] <= b.notes[i - 1]) ++bad;
            if (b.rootNote >= 48) {
                if (b.notes[0] != b.rootNote - 5) ++bad; else ++withFifthBelow;
            }
            if (b.notes[0] < 43) ++bad; // nada abaixo de Sol2 no corpo (o grave é da camada Low)
            if (ct == ChordType::Pad5) {
                for (int i = 0; i < b.count; ++i) {
                    const int iv = ((b.notes[i] - b.rootNote) % 12 + 12) % 12;
                    if (iv == 3 || iv == 4) ++bad;
                }
            }
        }
    }
    report("120 acordes: %d com a quinta por baixo, %d problemas", withFifthBelow, bad);
    CHECK(bad == 0);
    CHECK(withFifthBelow > 60);
}

// Abafar: em 0 o filtro fica desligado (agudo igual entre execuções); em 0,75 o agudo cai muito.
TEST(abafar_desligado_neutro_e_fecha) {
    PadEngine& e = sharedEngine();
    auto take = [&](float tc) {
        resetEngine(e, presetByName("Soft Pad"));
        e.setParam(ParamId::ToneCut, tc);
        e.setSeed(5);
        e.waitForSources(60.0);
        render(e, 0.3);
        int idx = 0;
        return render(e, 12.0, 192, [&](double t) { if (idx == 0 && t >= 0.0) { e.playChord(7, ChordType::Pad5); ++idx; } });
    };
    const Stereo off = take(0.0f), off2 = take(0.0f), on = take(0.75f);
    // (o motor tem moduladores livres, então duas execuções não são idênticas amostra a amostra;
    // com o filtro desligado o espectro agudo tem que ser o mesmo)
    const double hiOff = bandShareDb(off, fr(5.0), 2000.0), hiOff2 = bandShareDb(off2, fr(5.0), 2000.0);
    const double hiOn = bandShareDb(on, fr(5.0), 2000.0);
    report("energia acima de 2 kHz: %.1f / %.1f dB (Abafar 0, duas execuções) -> %.1f dB (Abafar 0,75)", hiOff, hiOff2, hiOn);
    CHECK(std::fabs(hiOff - hiOff2) < 3.0);
    CHECK(hiOn < std::min(hiOff, hiOff2) - 12.0);
    CHECK(allFinite(on));
}

// Guitarra gerada no carregamento: afinada, finita, sem DC, com sustentação para o swell.
TEST(guitarra_afinada_e_sustentada) {
    ZoneBank zb;
    CHECK(SourceLibrary::buildGuitar(kSr, zb));
    double worstCents = 0, worstSustain = 0;
    int bad = 0;
    for (size_t k = 0; k < zb.size(); ++k) {
        const Zone& z = *zb.zoneAt(k);
        std::vector<float> m(static_cast<size_t>(z.length));
        double dc = 0;
        for (int i = 0; i < z.length; ++i) {
            m[static_cast<size_t>(i)] = z.l[static_cast<size_t>(i + 1)] * z.scale;
            dc += m[static_cast<size_t>(i)];
            if (!std::isfinite(m[static_cast<size_t>(i)])) ++bad;
        }
        if (std::fabs(dc / z.length) > 1e-3) ++bad;
        const double f0 = z.rootHz;
        const double f = peakFrequency(m, kSr, fr(0.5), 1 << 16, f0 * 0.9, f0 * 1.1);
        worstCents = std::max(worstCents, std::fabs(1200.0 * std::log2(f / f0)));
        // nível entre 2 e 3 s relativo a 0,2–1,2 s (o swell chega ao pico em ~1–2 s)
        double a = 0, b = 0;
        for (size_t i = fr(0.2); i < fr(1.2); ++i) a += m[i] * m[i];
        for (size_t i = fr(2.0); i < fr(3.0); ++i) b += m[i] * m[i];
        if (z.rootNote <= 79) worstSustain = std::min(worstSustain, 10 * std::log10(b / a));
    }
    report("%zu zonas: pior afinação %.2f cents, pior queda 2–3 s (até G5) %.1f dB, problemas %d", zb.size(), worstCents, worstSustain, bad);
    CHECK(zb.size() == 13);
    CHECK(worstCents < 3.0);
    CHECK(worstSustain > -15.0);
    CHECK(bad == 0);
}

// Notas da guitarra: do acorde, ou 9ª (sempre diatônica) e 6ª maior (só com tonalidade e se
// estiver na escala). Nunca 4ª nem 7ª; no Pad5 nunca terça; registro entre A3 e D6 (+ inclinação).
TEST(guitarra_notas_seguras) {
    long notes = 0, colors = 0, bad = 0;
    struct Ch { int root; ChordType t; };
    const Ch prog[] = {{7, ChordType::Major}, {2, ChordType::Major}, {4, ChordType::Minor}, {0, ChordType::Major},
                       {7, ChordType::Pad5}, {2, ChordType::Sus2}, {9, ChordType::Minor}, {0, ChordType::Sus4}};
    for (int transpose = 0; transpose < 12; transpose += 7) {
        Conductor c;
        RenderContext ctx;
        ctx.activity = 1.0f;
        ctx.layerGuitar = 1.0f;
        ctx.microGuitar = 1.0f;
        ctx.microPerMinute = 30.0f;
        c.prepare(ctx.controlRate, 40 + transpose);
        SourceLibrary lib;
        TexturePlayer tex;
        tex.prepare(kSr);
        for (int rep = 0; rep < 4; ++rep) {
            for (const Ch& ch : prog) {
                const int root = (ch.root + transpose) % 12;
                const Voicing v = buildVoicing(root, ch.t);
                const uint16_t mask = pcMaskOf(v);
                const int before = c.logCount();
                c.onChord(v, ch.t, root);
                for (int i = 0; i < static_cast<int>(10.0 * ctx.controlRate); ++i) c.tick(ctx, lib, tex);
                for (int i = before; i < c.logCount(); ++i) {
                    const NoteLogEntry& n = c.logAt(i);
                    if (n.instrument != 10 + static_cast<int>(MicroType::Guitar)) continue;
                    ++notes;
                    if (n.midi < 62 || n.midi > 91) ++bad;
                    const int pc = n.midi % 12, iv = ((pc - root) % 12 + 12) % 12;
                    if ((mask >> pc) & 1) continue;
                    ++colors;
                    if (iv != 2 && iv != 9) { ++bad; continue; }
                    if (iv == 9 && (n.keyPc < 0 || ch.t == ChordType::Pad5 || ch.t == ChordType::Minor)) ++bad;
                    if (iv == 2 && ch.t == ChordType::Sus4) ++bad;
                }
            }
        }
    }
    report("%ld notas de guitarra, %ld fora do acorde (9ª/6ª), %ld problemas", notes, colors, bad);
    CHECK(notes > 100);
    CHECK(colors > 0);
    CHECK(bad == 0);
}

// Os 5 pads de fundo: sem alocação, sem clipping, mono seguro, nível parecido com a biblioteca
// v0.4 (Warm como referência); Shimmer Pad e Deep Pad escuros de verdade (menos agudo que o Warm).
TEST(presets_v05_pads_de_fundo) {
    PadEngine& e = sharedEngine();
    int count = 0;
    long allocs = 0;
    double worstPeak = 0, worstMono = 0, lo = 1e9, hi = -1e9, warmHigh = 0, deepHigh = 0, shimmerHigh = 0;
    double warmLevel = 0;
    for (int pass = 0; pass < 2; ++pass) {
        for (int i = 0; i < presetCount(); ++i) {
            const bool warm = std::strcmp(preset(i).name, "Warm") == 0 && presetIsCurrent(i);
            if (pass == 0 ? !warm : !isSoftPad(i)) continue;
            resetEngine(e, i);
            if (std::strcmp(preset(i).name, "Guitar Pad") != 0) e.setParam(ParamId::Activity, 0.0f); // só o timbre do pad
            e.setSeed(21);
            e.waitForSources(60.0);
            render(e, 0.3);
            g_allocationCount = 0;
            int idx = 0;
            auto x = render(e, 24.0, 192, [&](double t) {
                static const int roots[3] = {7, 2, 4};
                static const ChordType ty[3] = {ChordType::Pad5, ChordType::Pad5, ChordType::Minor};
                if (idx < 3 && t >= idx * 8.0) { e.playChord(roots[idx], ty[idx]); ++idx; }
            }, true);
            const double lvl = rmsDb(x, fr(3.0), fr(24.0));
            const double high = bandShareDb(x, fr(10.0), 2000.0);
            if (pass == 0) { warmLevel = lvl; warmHigh = high; continue; }
            ++count;
            allocs += g_allocationCount.load();
            worstPeak = std::max(worstPeak, static_cast<double>(peakAbs(x)));
            // perda em mono: energia de (L+R)/2 contra a média de L e R
            double st = 0, mo = 0;
            for (size_t k = fr(3.0); k < x.size() / 2; ++k) {
                const double l = x[2 * k], r = x[2 * k + 1];
                st += 0.5 * (l * l + r * r);
                mo += 0.25 * (l + r) * (l + r);
            }
            worstMono = std::min(worstMono, 10 * std::log10(mo / st));
            lo = std::min(lo, lvl);
            hi = std::max(hi, lvl);
            if (std::strcmp(preset(i).name, "Deep Pad") == 0) deepHigh = high;
            if (std::strcmp(preset(i).name, "Shimmer Pad") == 0) shimmerHigh = high;
            report("  %-12s RMS %.1f dB, acima de 2 kHz %.1f dB", preset(i).name, lvl, high);
        }
    }
    report("%d pads de fundo: alocações %ld, pior pico %.3f, pior perda em mono %.2f dB, RMS %.1f a %.1f dB (Warm %.1f); "
           "acima de 2 kHz: Warm %.1f, Shimmer Pad %.1f, Deep Pad %.1f dB", count, allocs, worstPeak, worstMono, lo, hi, warmLevel,
           warmHigh, shimmerHigh, deepHigh);
    CHECK(count == 5);
    CHECK(allocs == 0);
    CHECK(worstPeak <= kLimit);
    CHECK(worstMono > -3.0);
    CHECK(hi - lo < 5.0);
    CHECK(std::fabs(0.5 * (hi + lo) - warmLevel) < 3.0);
    CHECK(shimmerHigh < warmHigh - 3.0);
    CHECK(deepHigh < warmHigh - 10.0);
}

// v0.6 Aveludado: no mesmo preset, a entrada da nota é mais escura (o filtro abre com o volume),
// os agudos ficam mais no centro (menos lado acima de 3 kHz) e nada aloca no áudio.
TEST(aveludado_entrada_escura_e_agudos_centrados) {
    PadEngine& e = sharedEngine();
    auto take = [&](float v) {
        resetEngine(e, presetByName("Soft Pad"));
        e.setParam(ParamId::Velvet, v);
        e.setParam(ParamId::Activity, 0.0f);
        e.setSeed(8);
        e.waitForSources(60.0);
        render(e, 0.3);
        int idx = 0;
        g_allocationCount = 0;
        return render(e, 14.0, 192, [&](double t) { if (idx == 0 && t >= 0.0) { e.playChord(7, ChordType::Pad5); ++idx; } }, true);
    };
    const Stereo hard = take(0.0f);
    const long allocA = g_allocationCount.load();
    const Stereo soft = take(1.0f);
    const long allocB = g_allocationCount.load();
    auto sideHigh = [&](const Stereo& x) {
        // energia do lado relativa ao meio acima de 3 kHz, de 6 s ao fim
        Stereo hp(x.size());
        double sh = 0, mh = 0;
        const size_t n = 1 << 14;
        std::vector<float> s(n), m(n);
        for (size_t w = fr(6.0); w + n <= x.size() / 2; w += n) {
            for (size_t k = 0; k < n; ++k) { const float l = x[2 * (w + k)], r = x[2 * (w + k) + 1]; m[k] = 0.5f * (l + r); s[k] = 0.5f * (l - r); }
            auto ss = magnitudeSpectrumDb(s, 0, n), sm = magnitudeSpectrumDb(m, 0, n);
            for (size_t k = 1; k < ss.size(); ++k) if (k * kSr / n >= 3000.0) { sh += std::pow(10.0, ss[k] / 10); mh += std::pow(10.0, sm[k] / 10); }
        }
        return 10 * std::log10(sh / mh);
    };
    // agudo na entrada da nota: energia acima de 1,5 kHz entre 0,3 e 1,7 s (relativa ao total)
    auto onsetHigh = [&](const Stereo& x) { Stereo seg(x.begin() + 2 * fr(0.3), x.begin() + 2 * fr(1.7)); return bandShareDb(seg, 0, 1500.0); };
    const double cHard = onsetHigh(hard), cSoft = onsetHigh(soft);
    const double sHard = sideHigh(hard), sSoft = sideHigh(soft);
    report("agudo (>1,5 kHz) na entrada da nota: %.1f -> %.1f dB; lado/meio acima de 3 kHz: %.1f -> %.1f dB; alocações %ld / %ld",
           cHard, cSoft, sHard, sSoft, allocA, allocB);
    CHECK(cSoft < cHard - 3.0);
    CHECK(sSoft < sHard - 3.0);
    CHECK(allocA == 0 && allocB == 0);
    CHECK(allFinite(soft));
}

// Encerramento (v0.6): criar, tocar e destruir o motor várias vezes não deixa threads vivos
// (o carregador de fontes termina) e não trava. Vazamento de memória é verificado pelo
// LeakSanitizer na execução com ASan.
#include <dirent.h>
namespace {
int threadCount() {
    int n = 0;
    if (DIR* d = opendir("/proc/self/task")) {
        while (dirent* e = readdir(d)) if (e->d_name[0] != '.') ++n;
        closedir(d);
    }
    return n;
}
} // namespace

TEST(motor_criar_e_destruir_sem_sobras) {
    const int before = threadCount();
    int worst = 0;
    for (int round = 0; round < 3; ++round) {
        auto e = std::make_unique<PadEngine>();
        e->setAssetProvider(std::make_shared<FileAssetProvider>(PAD_ASSETS_DIR));
        CHECK(e->prepare(kSr));
        e->applyPreset(presetByName(round == 1 ? "Guitar Pad" : "Soft Pad"));
        e->waitForSources(60.0);
        std::vector<float> b(2 * 192);
        e->playChord(7, ChordType::Pad5);
        for (int i = 0; i < 250; ++i) e->process(b.data(), 192);
        e->applyPreset(presetByName("Shimmer Pad")); // troca com fontes ainda carregando
        for (int i = 0; i < 50; ++i) e->process(b.data(), 192);
        worst = std::max(worst, threadCount());
        e.reset();
    }
    const int after = threadCount();
    report("threads: antes %d, no pior momento %d, depois de destruir 3 motores %d", before, worst, after);
    CHECK(after <= before);
}
