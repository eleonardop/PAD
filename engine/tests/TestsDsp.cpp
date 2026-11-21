// Testes dos blocos de DSP isolados.
#include "TestFramework.h"
#include "AudioAnalysis.h"
#include "core/RingBuffer.h"
#include "core/SampleBank.h"
#include "core/Zone.h"
#include "dsp/Wavetable.h"
#include "dsp/Crossfade.h"
#include "dsp/Envelope.h"
#include "dsp/Limiter.h"
#include "dsp/Reverb.h"
#include "dsp/Svf.h"
#include "dsp/Random.h"
#include <thread>
#include <cmath>

using namespace pad;
using namespace padtest;

// (10) Estabilidade do Ring Buffer: produtor e consumidor em threads diferentes.
TEST(ring_buffer_spsc_estabilidade) {
    struct Item { uint32_t seq; uint32_t check; };
    static SpscRingBuffer<Item, 256> rb;
    constexpr uint32_t kCount = 2000000;
    std::atomic<bool> orderOk{true};
    std::thread consumer([&] {
        uint32_t expect = 0;
        Item it;
        while (expect < kCount) {
            if (rb.pop(it)) {
                if (it.seq != expect || it.check != (it.seq ^ 0xA5A5A5A5u)) orderOk = false;
                ++expect;
            }
        }
    });
    uint32_t full = 0;
    for (uint32_t i = 0; i < kCount;) {
        if (rb.push({i, i ^ 0xA5A5A5A5u})) ++i; else ++full;
    }
    consumer.join();
    CHECK(orderOk.load());
    CHECK(rb.size() == 0);
    report("2.000.000 itens transferidos em ordem; fila cheia %u vezes (produtor não bloqueou)", full);

    SpscRingBuffer<int, 4> small;
    CHECK(small.push(1) && small.push(2) && small.push(3) && small.push(4));
    CHECK(!small.push(5)); // cheio: recusa sem sobrescrever
    int v = 0;
    CHECK(small.pop(v) && v == 1);
    CHECK(small.push(5));
    int expect[] = {2, 3, 4, 5};
    for (int e : expect) { CHECK(small.pop(v) && v == e); }
    CHECK(!small.pop(v)); // vazio
}

// (5) Lei de crossfade: potência constante para sinais independentes, amplitude constante para correlacionados.
TEST(crossfade_lei_correlacao) {
    double worstP0 = 0, worstA1 = 0;
    for (int i = 0; i <= 100; ++i) {
        const float t = i / 100.0f;
        const auto g0 = crossfadeGains(t, 0.0f);
        worstP0 = std::max(worstP0, std::fabs(double(g0.out) * g0.out + double(g0.in) * g0.in - 1.0));
        const auto g1 = crossfadeGains(t, 1.0f);
        worstA1 = std::max(worstA1, std::fabs(double(g1.out) + g1.in - 1.0));
    }
    CHECK_MSG(worstP0 < 1e-4, "erro potência %g", worstP0);
    CHECK_MSG(worstA1 < 1e-4, "erro amplitude %g", worstA1);

    // Medição em sinais reais: ruído independente (rho~0) e sinal idêntico (rho=1).
    Rng rng(7);
    const int n = 48000;
    std::vector<float> a(n), b(n);
    for (int i = 0; i < n; ++i) { a[i] = rng.bipolar(); b[i] = rng.bipolar(); }
    for (int pass = 0; pass < 2; ++pass) {
        const std::vector<float>& other = pass == 0 ? b : a;
        const float rho = estimateCorrelation(a.data(), other.data(), n);
        double maxDev = 0;
        for (int seg = 0; seg < 10; ++seg) {
            double e = 0, ea = 0;
            for (int i = seg * 4800; i < (seg + 1) * 4800; ++i) {
                const auto g = crossfadeGains(static_cast<float>(i) / n, rho);
                const double y = a[i] * g.out + other[i] * g.in;
                e += y * y; ea += double(a[i]) * a[i];
            }
            maxDev = std::max(maxDev, std::fabs(10 * std::log10(e / ea)));
        }
        report("rho=%.3f: maior desvio de nível durante o crossfade = %.2f dB", rho, maxDev);
        CHECK_MSG(maxDev < 0.35, "desvio %.2f dB", maxDev);
    }
}

// Interpolação: Hermite vs linear (justificativa da escolha) ao reamostrar um seno.
TEST(interpolacao_hermite_vs_linear) {
    const int N = 64; // seno amostrado com 16 amostras por ciclo (conteúdo agudo, caso difícil)
    std::vector<float> tab(N + 3);
    auto f = [&](double x) { return std::sin(2 * M_PI * x / 16.0); };
    for (int i = -1; i < N + 2; ++i) tab[static_cast<size_t>(i + 1)] = static_cast<float>(f(i));
    double eL = 0, eH = 0, s = 0;
    for (int k = 0; k < 10000; ++k) {
        const double x = 1.0 + (N - 4) * (k / 10000.0);
        const int i = static_cast<int>(x);
        const float t = static_cast<float>(x - i);
        const float* p = tab.data() + 1 + i;
        const double ref = f(x);
        const double lin = p[0] + (p[1] - p[0]) * t;
        const double her = hermite4(p[-1], p[0], p[1], p[2], t);
        eL += (lin - ref) * (lin - ref); eH += (her - ref) * (her - ref); s += ref * ref;
    }
    const double snrL = 10 * std::log10(s / eL), snrH = 10 * std::log10(s / eH);
    report("SNR com 16 amostras/ciclo: linear %.1f dB, Hermite %.1f dB", snrL, snrH);
    CHECK(snrH > snrL + 12.0);
}

// Wavetable: sem aliasing (nenhuma componente fora da série harmônica acima do piso).
TEST(wavetable_sem_aliasing) {
    Wavetable wt;
    wt.build([](int h) { return 1.0f / h; }, 48000.0f, 3, true);
    for (float f0 : {110.0f, 1046.5f, 3520.0f}) {
        WavetableOsc o;
        const int level = wt.levelFor(f0);
        std::vector<float> x(65536);
        for (auto& v : x) v = o.tick(wt, level, f0 / 48000.0f);
        auto db = magnitudeSpectrumDb(x, 0, 65536);
        double harm = -300, inharm = -300;
        for (size_t k = 5; k < db.size(); ++k) {
            const double fk = k * 48000.0 / 65536;
            const double r = fk / f0;
            const bool nearHarm = std::fabs(r - std::round(r)) * f0 < 12.0 && std::round(r) >= 1;
            if (nearHarm) harm = std::max(harm, db[k]); else inharm = std::max(inharm, db[k]);
        }
        report("f0 %.0f Hz: harmônico mais forte %.1f dB, maior componente não harmônica %.1f dB", f0, harm, inharm);
        CHECK_MSG(harm - inharm > 90.0, "f0 %.1f: relação %.1f dB", f0, harm - inharm);
    }
}

// Envelope: contínuo, termina em zero, retrigger e release no meio sem saltos.
TEST(envelope_suave) {
    PadEnvelope env;
    env.setSampleRate(48000.0f);
    env.startAttack(0.5f);
    float prev = 0.0f, maxStep = 0.0f;
    for (int i = 0; i < 12000; ++i) { const float v = env.advance(1); maxStep = std::max(maxStep, std::fabs(v - prev)); prev = v; }
    env.startRelease(1.0f); // release no meio do attack
    for (int i = 0; i < 48000 + 10; ++i) { const float v = env.advance(1); maxStep = std::max(maxStep, std::fabs(v - prev)); prev = v; }
    CHECK(env.isIdle());
    CHECK(prev == 0.0f);
    CHECK_MSG(maxStep < 1.0f / 48000.0f * 4.0f, "maior degrau %g", maxStep);
}

// Limitador: nunca ultrapassa o limiar, mesmo com transientes e +12 dB de excesso.
TEST(limitador_sem_overshoot) {
    LookaheadLimiter lim;
    lim.prepare(48000.0f, 3.0f, 150.0f);
    lim.setThreshold(0.891f);
    Rng rng(9);
    float peak = 0.0f;
    for (int i = 0; i < 48000 * 4; ++i) {
        float a = (i % 9600 < 200) ? 4.0f : 0.3f; // rajadas de +13 dB
        float l = rng.bipolar() * a, r = std::sin(i * 0.05f) * a;
        lim.process(l, r);
        peak = std::max(peak, std::max(std::fabs(l), std::fabs(r)));
    }
    report("pico máximo após o limitador: %.4f (limiar 0,891)", peak);
    CHECK(peak <= 0.8911f);
}

// Reverb: decaimento coerente com RT60, cauda sem explosão com shimmer máximo.
TEST(reverb_decaimento_e_shimmer_estavel) {
    for (float shimmer : {0.0f, 1.0f}) {
        FdnReverb rv;
        rv.prepare(48000.0f);
        rv.setParams(3.0f, 0.7f, 0.4f, shimmer);
        float l, r;
        std::vector<double> e(12, 0.0); // energia por 0,5 s
        for (int i = 0; i < 48000 * 6; ++i) {
            const float in = (i < 480) ? 0.5f : 0.0f;
            rv.process(in, in, l, r);
            CHECK(std::isfinite(l) && std::isfinite(r));
            e[static_cast<size_t>(i / 24000)] += double(l) * l + double(r) * r;
        }
        const double d1 = 10 * std::log10(e[2] / e[8]); // de 1-1,5 s até 4-4,5 s
        const double rate = d1 / 3.0;                     // dB por segundo
        report("shimmer %.0f: queda %.1f dB/s (esperado ~20 dB/s para RT60 = 3 s)", shimmer, rate);
        if (shimmer == 0.0f) CHECK_MSG(rate > 14.0 && rate < 28.0, "taxa %.1f", rate);
        else CHECK_MSG(rate > 5.0, "shimmer deve decair (taxa %.1f)", rate);
    }
}

// Cordas sintéticas (reserva da v0.1): loop sem emenda (a junção não difere do resto do sinal).
TEST(samples_loop_sem_emenda) {
    ZoneBank bank;
    bakeSynthStrings(bank, 48000.0f, 77, 48, 60, 12, 2.0f);
    CHECK(bank.size() == 2);
    for (size_t zi = 0; zi < bank.size(); ++zi) {
        const Zone& z = *bank.zoneAt(zi);
        auto at = [&](int i) { return z.l[static_cast<size_t>(i + 1)] * z.scale; };
        double maxD = 0, sumD = 0;
        for (int i = 1; i < z.length; ++i) { const double d = std::fabs(at(i) - at(i - 1)); maxD = std::max(maxD, d); sumD += d; }
        const double joint = std::fabs(at(0) - at(z.length - 1));
        const double avg = sumD / (z.length - 1);
        report("zona %d: salto na junção %.5f, média %.5f, máximo interno %.5f", z.rootNote, joint, avg, maxD);
        CHECK(joint < maxD);
        CHECK(joint < avg * 6.0);
    }

    // Loop de material externo: crossfade com correlação medida mantém o nível.
    Rng rng(3);
    std::vector<float> noise(48000 * 3);
    float lp = 0;
    for (auto& v : noise) { lp = 0.9f * lp + 0.1f * rng.bipolar(); v = lp; }
    std::vector<float> loop;
    const float rho = makeSeamlessLoop(noise, 48000 * 2, 24000, loop);
    double eX = 0, eRef = 0;
    for (int i = 0; i < 24000; ++i) { eX += double(loop[i]) * loop[i]; eRef += double(noise[24000 + i]) * noise[24000 + i]; }
    report("WAV externo (ruído): correlação %.3f, nível na região de crossfade %.2f dB", rho, 10 * std::log10(eX / eRef));
    CHECK(std::fabs(10 * std::log10(eX / eRef)) < 1.0);
}

// Filtros: resposta correta e sem instabilidade sob modulação rápida.
TEST(filtros_svf_estaveis) {
    SvfCoeffs c;
    SvfState s;
    Rng rng(5);
    float maxOut = 0;
    for (int i = 0; i < 48000 * 2; ++i) {
        if (i % 32 == 0) c.lowpass(expMap(0.5f + 0.5f * std::sin(i * 0.001f), 40.0f, 18000.0f), 0.7f, 48000.0f);
        const float y = s.process(c, rng.bipolar());
        CHECK(std::isfinite(y));
        maxOut = std::max(maxOut, std::fabs(y));
    }
    CHECK(maxOut < 3.0f);
    // Ganho DC do passa-baixa = 1; do shelf de graves +6 dB = 2.
    SvfCoeffs lpc; lpc.lowpass(1000.0f, 0.707f, 48000.0f);
    SvfState ls; float y = 0; for (int i = 0; i < 20000; ++i) y = ls.process(lpc, 1.0f);
    CHECK(std::fabs(y - 1.0f) < 1e-3f);
    SvfCoeffs sh; sh.lowShelf(200.0f, 0.7f, 6.0206f, 48000.0f);
    SvfState ss; for (int i = 0; i < 40000; ++i) y = ss.process(sh, 1.0f);
    CHECK(std::fabs(y - 2.0f) < 1e-2f);
}
