// Testes da v0.4 (refinamento): microeventos (harmonia, cooldown, densidade, sementes, zero,
// trocas de acorde) e biblioteca de presets (identidade, loudness, tempo real, mono).
#include "TestFramework.h"
#include "AudioAnalysis.h"
#include <cmath>
#include <cstring>
#include "core/Conductor.h"
#include "core/TexturePlayer.h"

using namespace pad;
using namespace padtest;

namespace {
constexpr float kSr = 48000.0f;
constexpr float kLimit = 0.8913f;
size_t fr(double s) { return static_cast<size_t>(s * kSr); }

const int kScale[7] = {0, 2, 4, 5, 7, 9, 11};
bool inScale(int key, int pc) {
    for (int d : kScale) if ((key + d) % 12 == pc) return true;
    return false;
}
uint16_t chordMask(int root, ChordType t) {
    const Voicing v = buildVoicing(root, t);
    uint16_t m = 0;
    for (int i = 0; i < v.count; ++i) m = static_cast<uint16_t>(m | (1u << (v.notes[i] % 12)));
    return m;
}

RenderContext microContext(float perMinute, float activity = 0.5f) {
    RenderContext c;
    c.evolution = 0.6f;
    c.activity = activity;
    c.layerKeys = c.layerSparkle = c.layerMetal = c.layerStrings = c.tape = 1.0f;
    c.brightness = 0.4f;
    c.microBell = c.microPiano = c.microPluck = c.microHarmonic = c.microReverse = 1.0f;
    c.microMetal = c.microBow = c.microTapeGust = 1.0f;
    c.microPerMinute = perMinute;
    return c;
}

void run(Conductor& c, const RenderContext& ctx, double seconds) {
    SourceLibrary lib;
    TexturePlayer tex;
    tex.prepare(kSr);
    const int steps = static_cast<int>(seconds * ctx.controlRate);
    for (int i = 0; i < steps; ++i) c.tick(ctx, lib, tex);
}

struct Ch { int root; ChordType t; };
// Progressões em tonalidades diferentes (para a tonalidade estimada se firmar).
const Ch kProg[] = {{7, ChordType::Major}, {2, ChordType::Major}, {4, ChordType::Minor}, {0, ChordType::Major},
                    {9, ChordType::Minor}, {2, ChordType::Major}, {7, ChordType::Major}, {11, ChordType::Minor}};

float cooldownOf(int type) {
    const float cd[] = {5.0f, 4.0f, 5.0f, 9.0f, 12.0f, 22.0f, 14.0f, 16.0f};
    return cd[type];
}
} // namespace

// Toda nota de microevento é do acorde; 7ª e 9ª só no registro alto e só se estiverem na
// tonalidade estimada (que existe quando a progressão a define).
TEST(micro_harmonia_acorde_e_cores) {
    long notes = 0, bad = 0, colors = 0, colorsOutOfKey = 0, colorsLow = 0;
    for (int transpose = 0; transpose < 12; transpose += 5) {
        for (ChordType extra : {ChordType::Major, ChordType::Pad5, ChordType::Sus4, ChordType::Major7, ChordType::Minor7}) {
            Conductor c;
            RenderContext ctx = microContext(20.0f, 1.0f);
            c.prepare(ctx.controlRate, 100 + transpose);
            for (int rep = 0; rep < 3; ++rep) {
                for (const Ch& ch : kProg) {
                    const int root = (ch.root + transpose) % 12;
                    const ChordType t = (rep == 2 && ch.t == ChordType::Major) ? extra : ch.t;
                    const int before = c.logCount();
                    c.onChord(buildVoicing(root, t), t, root);
                    run(c, ctx, 14.0);
                    const uint16_t mask = chordMask(root, t);
                    for (int i = before; i < c.logCount(); ++i) {
                        const NoteLogEntry& n = c.logAt(i);
                        if (n.midi <= 0 || n.instrument < 10) continue; // arco / fita não têm nota
                        ++notes;
                        const int pc = n.midi % 12;
                        if ((mask >> pc) & 1) continue;
                        ++colors;
                        const bool ninth = pc == (root + 2) % 12;
                        const bool minor7 = pc == (root + 10) % 12, major7 = pc == (root + 11) % 12;
                        if (!ninth && !minor7 && !major7) { ++bad; continue; }
                        if (n.keyPc < 0 || !inScale(n.keyPc, pc)) ++colorsOutOfKey;
                        if (n.midi < 76) ++colorsLow;
                        if (t == ChordType::Sus4 && ninth) ++bad;
                        if ((t == ChordType::Pad5 || t == ChordType::Sus4) && (minor7 || major7)) ++bad;
                    }
                }
            }
        }
    }
    report("%ld notas de microevento: %ld cores (7ª/9ª), %ld fora do acorde sem ser cor permitida, %ld cores fora da tonalidade, %ld cores abaixo de Mi5",
           notes, colors, bad, colorsOutOfKey, colorsLow);
    CHECK(notes > 300);
    CHECK(colors > 0);
    CHECK(bad == 0);
    CHECK(colorsOutOfKey == 0);
    CHECK(colorsLow == 0);
}

// Espaçamento global ≥ 2,5 s, cooldown de cada tipo, nada antes de 1,2 s depois da troca.
TEST(micro_cooldown_e_densidade) {
    Conductor c;
    RenderContext ctx = microContext(30.0f, 1.0f); // pedindo muito mais do que os limites deixam
    c.prepare(ctx.controlRate, 7);
    int early = 0;
    for (int k = 0; k < 30; ++k) {
        const Ch& ch = kProg[k % 8];
        c.onChord(buildVoicing(ch.root, ch.t), ch.t, ch.root);
        run(c, ctx, 20.0);
    }
    int gapViol = 0, cdViol = 0;
    double lastAny = -1e9, lastType[8];
    for (double& v : lastType) v = -1e9;
    for (int i = 0; i < c.logCount() && i < Conductor::kLog; ++i) {
        const NoteLogEntry& n = c.logAt(i);
        if (n.instrument < 10) continue;
        const int t = n.instrument - 10;
        if (n.absTime - lastAny < 2.5 - 1e-3) ++gapViol;
        if (n.absTime - lastType[t] < cooldownOf(t) - 1e-3) ++cdViol;
        if (n.time < 1.2f) ++early;
        lastAny = n.absTime;
        lastType[t] = n.absTime;
    }
    const double perMin = c.microTotal() / (30 * 20.0 / 60.0);
    report("%u microeventos em 10 min (%.1f/min com pedido de 60/min): espaçamento < 2,5 s: %d, cooldown violado: %d, antes de 1,2 s: %d",
           c.microTotal(), perMin, gapViol, cdViol, early);
    CHECK(gapViol == 0);
    CHECK(cdViol == 0);
    CHECK(early == 0);
    CHECK(perMin < 24.0); // nunca mais que um a cada 2,5 s
}

// Mesma semente = mesmos eventos; sementes diferentes = eventos diferentes.
TEST(micro_sementes_reproduziveis_e_variadas) {
    auto runSeed = [](uint64_t seed, std::vector<int>& out) {
        Conductor c;
        RenderContext ctx = microContext(6.0f);
        c.prepare(ctx.controlRate, seed);
        for (int k = 0; k < 12; ++k) {
            const Ch& ch = kProg[k % 8];
            c.onChord(buildVoicing(ch.root, ch.t), ch.t, ch.root);
            run(c, ctx, 15.0);
        }
        for (int i = 0; i < c.logCount(); ++i) {
            out.push_back(c.logAt(i).midi);
            out.push_back(static_cast<int>(c.logAt(i).absTime * 100));
            out.push_back(c.logAt(i).instrument);
        }
    };
    std::vector<int> a, b, d;
    runSeed(11, a);
    runSeed(11, b);
    runSeed(12, d);
    report("semente 11: %zu eventos (repetição idêntica: %s); semente 12: %zu eventos (diferente: %s)", a.size() / 3,
           a == b ? "sim" : "não", d.size() / 3, a != d ? "sim" : "não");
    CHECK(!a.empty());
    CHECK(a == b);
    CHECK(a != d);
}

// Atividade zero ou paleta sem densidade = nenhum microevento.
TEST(micro_desligado_sem_eventos) {
    Conductor c1, c2;
    RenderContext z1 = microContext(6.0f, 0.0f), z2 = microContext(0.0f, 1.0f);
    c1.prepare(z1.controlRate, 3);
    c2.prepare(z2.controlRate, 3);
    for (int k = 0; k < 10; ++k) {
        const Ch& ch = kProg[k % 8];
        c1.onChord(buildVoicing(ch.root, ch.t), ch.t, ch.root);
        c2.onChord(buildVoicing(ch.root, ch.t), ch.t, ch.root);
        run(c1, z1, 20.0);
        run(c2, z2, 20.0);
    }
    report("atividade 0: %u eventos; densidade 0: %u eventos", c1.microTotal(), c2.microTotal());
    CHECK(c1.microTotal() == 0);
    CHECK(c2.microTotal() == 0);
}

// Em trocas rápidas (1 s) quase nada acontece; em acordes longos os eventos aparecem.
TEST(micro_trocas_rapidas_quase_nada) {
    Conductor fast, slow;
    RenderContext ctx = microContext(8.0f);
    fast.prepare(ctx.controlRate, 21);
    slow.prepare(ctx.controlRate, 21);
    for (int k = 0; k < 180; ++k) {
        const Ch& ch = kProg[k % 8];
        fast.onChord(buildVoicing(ch.root, ch.t), ch.t, ch.root);
        run(fast, ctx, 1.0);
    }
    for (int k = 0; k < 18; ++k) {
        const Ch& ch = kProg[k % 8];
        slow.onChord(buildVoicing(ch.root, ch.t), ch.t, ch.root);
        run(slow, ctx, 10.0);
    }
    report("3 min com trocas a cada 1 s: %u eventos; 3 min com trocas a cada 10 s: %u eventos", fast.microTotal(), slow.microTotal());
    CHECK(fast.microTotal() * 4 < slow.microTotal());
    CHECK(slow.microTotal() >= 8);
}

// Na troca de acorde, os microeventos tonais cuja nota não pertence ao novo acorde são soltos;
// os que pertencem continuam.
TEST(micro_troca_de_acorde_solta_notas_fora) {
    // Fonte sintética mínima para o TexturePlayer tocar de verdade.
    TexturePlayer tex;
    tex.prepare(kSr);
    SourceLibrary lib;
    lib.configure(kSr, nullptr);
    lib.start();
    lib.setWanted(sourceBit(SourceId::EpTine));
    CHECK(lib.waitReady(sourceBit(SourceId::EpTine), 30.0));
    TexEvent e;
    e.source = SourceId::EpTine;
    e.group = 3;
    e.level = 0.3f;
    e.hold = 3.0f;
    e.midi = 76; // Mi: pertence a C maior
    CHECK(tex.trigger(e, lib));
    e.midi = 78; // Fá#: não pertence
    CHECK(tex.trigger(e, lib));
    std::vector<float> l(32), r(32), m1(32), m2(32);
    for (int i = 0; i < 50; ++i) tex.render(l.data(), r.data(), m1.data(), m2.data(), 32);
    const int before = tex.activeInGroup(3);
    tex.releaseGroupNotInMask(3, chordMask(0, ChordType::Major), 0.6f);
    const int after = tex.activeInGroup(3);
    lib.stop();
    report("microeventos tonais ativos antes da troca %d, depois %d (o Fá# saiu, o Mi ficou)", before, after);
    CHECK(before == 2);
    CHECK(after == 1);
}

// Biblioteca principal: 8 presets em 4 categorias de 2.
TEST(presets_v04_biblioteca) {
    int current = 0;
    const char* cats[4] = {"FOUNDATION", "ATMOSPHERE", "CHARACTER", "TEXTURES"};
    int perCat[4] = {0, 0, 0, 0};
    for (int i = 0; i < presetCount(); ++i) {
        if (!presetIsCurrent(i)) continue;
        ++current;
        for (int k = 0; k < 4; ++k) if (std::strcmp(preset(i).category, cats[k]) == 0) ++perCat[k];
        CHECK(!preset(i).micro.empty());
    }
    report("%d presets na biblioteca principal: FOUNDATION %d, ATMOSPHERE %d, CHARACTER %d, TEXTURES %d; %d versões anteriores fora da tela",
           current, perCat[0], perCat[1], perCat[2], perCat[3], presetCount() - current);
    CHECK(current == 8);
    for (int k = 0; k < 4; ++k) CHECK(perCat[k] == 2);
}

// Os 8 presets, cada um com uma progressão de 24 s: sem alocação, sem clipping, mono seguro,
// nível parecido (trims) e timbre distinto (distância espectral entre o par mais próximo).
TEST(presets_v04_tempo_real_mono_e_identidade) {
    PadEngine& e = sharedEngine();
    std::vector<std::vector<double>> spec;
    std::vector<double> levels;
    long allocs = 0;
    double worstPeak = 0, worstMono = 0;
    for (int i = 0; i < presetCount(); ++i) {
        if (!presetIsCurrent(i)) continue;
        resetEngine(e, i);
        e.setSeed(99);
        e.waitForSources(60.0);
        render(e, 0.3);
        g_allocationCount = 0;
        int idx = 0;
        auto x = render(e, 24.0, 192, [&](double t) {
            static const int roots[3] = {7, 4, 0};
            static const ChordType ty[3] = {ChordType::Major, ChordType::Minor, ChordType::Major};
            if (idx < 3 && t >= idx * 8.0) { e.playChord(roots[idx], ty[idx]); ++idx; }
        }, true);
        allocs += g_allocationCount.load();
        worstPeak = std::max(worstPeak, static_cast<double>(peakAbs(x)));
        // espectro em 1/3 de oitava (nível relativo ao máximo) e perda em mono
        const size_t n = 1 << 14;
        std::vector<double> bands(24, 0.0);
        double st = 0, mo = 0;
        std::vector<float> m(n), l(n), r(n);
        for (size_t s = fr(3.0); s + n < x.size() / 2; s += n) {
            for (size_t k = 0; k < n; ++k) { l[k] = x[2 * (s + k)]; r[k] = x[2 * (s + k) + 1]; m[k] = 0.5f * (l[k] + r[k]); }
            auto sm = magnitudeSpectrumDb(m, 0, n), sl = magnitudeSpectrumDb(l, 0, n), sr = magnitudeSpectrumDb(r, 0, n);
            for (size_t k = 1; k < sm.size(); ++k) {
                const double f = k * kSr / n;
                const double pm = std::pow(10.0, sm[k] / 10), ps = 0.5 * (std::pow(10.0, sl[k] / 10) + std::pow(10.0, sr[k] / 10));
                if (f >= 100 && f <= 10000) { st += ps; mo += pm; }
                if (f < 50) continue;
                const int b = static_cast<int>(std::floor(3.0 * std::log2(f / 50.0)));
                if (b >= 0 && b < 24) bands[static_cast<size_t>(b)] += pm;
            }
        }
        double mx = -1e9;
        for (double& v : bands) { v = 10 * std::log10(v + 1e-20); mx = std::max(mx, v); }
        for (double& v : bands) v -= mx;
        spec.push_back(bands);
        levels.push_back(rmsDb(x, fr(3.0), fr(24.0)));
        worstMono = std::min(worstMono, 10 * std::log10(mo / st));
    }
    double minDist = 1e9, lo = 1e9, hi = -1e9;
    for (size_t a = 0; a < spec.size(); ++a) {
        lo = std::min(lo, levels[a]);
        hi = std::max(hi, levels[a]);
        for (size_t b = a + 1; b < spec.size(); ++b) {
            double d = 0;
            for (size_t k = 0; k < 24; ++k) d += std::fabs(spec[a][k] - spec[b][k]);
            minDist = std::min(minDist, d / 24.0);
        }
    }
    report("8 presets: alocações %ld, pior pico %.3f, pior perda em mono %.2f dB, RMS entre %.1f e %.1f dB, menor distância de timbre entre dois presets %.2f dB",
           allocs, worstPeak, worstMono, lo, hi, minDist);
    CHECK(allocs == 0);
    CHECK(worstPeak <= kLimit);
    CHECK(worstMono > -3.0);
    CHECK(hi - lo < 4.0);
    CHECK(minDist > 1.2);
}
