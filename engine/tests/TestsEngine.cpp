// Testes do motor completo (equivalentes ao uso real no app).
#include "TestFramework.h"
#include "AudioAnalysis.h"
#include "dsp/Random.h"
#include <chrono>
#include <cmath>
#include <algorithm>
#include <functional>

using namespace pad;
using namespace padtest;

namespace {
constexpr float kSr = 48000.0f;
constexpr float kLimit = 0.8913f; // -1 dBFS (limiar do limitador) + tolerância de arredondamento

size_t fr(double s) { return static_cast<size_t>(s * kSr); }

// Configuração "seca" para medições precisas: sem reverb, sem movimento aleatório.
void dryConfig(PadEngine& e) {
    e.setParam(ParamId::Reverb, 0.0f);
    e.setParam(ParamId::Shimmer, 0.0f);
    e.setParam(ParamId::Movement, 0.0f);
}

void soloLayer(PadEngine& e, ParamId layer) {
    for (ParamId id : {ParamId::LayerWarm, ParamId::LayerAir, ParamId::LayerStrings, ParamId::LayerShimmer, ParamId::LayerLow,
                       ParamId::LayerVox, ParamId::LayerTexture})
        e.setParam(id, id == layer ? 1.0f : 0.0f);
    e.waitForSources(60.0);
}

struct LevelRange { double min, max; };
LevelRange rangeDb(const std::vector<double>& w, size_t b, size_t e) {
    LevelRange r{1e9, -1e9};
    for (size_t i = b; i < e && i < w.size(); ++i) { r.min = std::min(r.min, w[i]); r.max = std::max(r.max, w[i]); }
    return r;
}
} // namespace

// (1) Inicialização e encerramento.
TEST(motor_inicializacao_e_encerramento) {
    {
        PadEngine e;
        std::vector<float> buf(2 * 256, 1.0f);
        e.process(buf.data(), 256); // antes do prepare: silêncio, sem travar
        CHECK(peakAbs(buf) == 0.0f);
        CHECK(!e.prepare(1000.0f));  // taxa inválida recusada
        CHECK(e.prepare(44100.0f));  // taxa comum em fones Bluetooth / alguns aparelhos
        CHECK(e.isPrepared());
        e.setParam(ParamId::Attack, 0.2f);
        e.playChord(0, ChordType::Major);
        auto x = render(e, 2.0);
        CHECK(allFinite(x));
        CHECK(rmsDb(x, 44100, 88200) > -40.0); // quadros a 44,1 kHz
    } // destrutor com vozes tocando
    PadEngine& e = sharedEngine();
    CHECK(e.isPrepared());
    CHECK(e.sampleRate() == kSr);
    const auto st = e.readStats();
    report("fontes carregadas (bits %x), %.1f MB; falhas: %x", st.sourcesReady, st.sourcesMemoryBytes / 1e6, st.sourcesFailed);
    CHECK(st.sourcesFailed == 0);
}

// (2) Reprodução contínua: nível estável, sem cliques, sem NaN.
TEST(motor_reproducao_continua) {
    PadEngine& e = sharedEngine();
    resetEngine(e, 0);
    e.playChord(2, ChordType::Pad5);
    auto x = render(e, 30.0);
    CHECK(allFinite(x));
    // Janelas de 500 ms: mede o volume percebido. Abaixo disso aparece o batimento natural
    // entre osciladores desafinados (o "respirar" do pad), que é desejado.
    auto w = windowRmsDb(x, 24000);
    auto r = rangeDb(w, 12, w.size()); // depois de 6 s (attack de 2,5 s + reverb)
    report("nível 6–30 s (janelas de 500 ms): %.1f a %.1f dB RMS (variação %.1f dB)", r.min, r.max, r.max - r.min);
    CHECK(r.min > -35.0);
    CHECK_MSG(r.max - r.min < 4.0, "variação %.1f dB", r.max - r.min);
    const double clicks = clickScore(x, fr(6), fr(30));
    report("índice de clique (máximo/mediana): %.2f", clicks);
    CHECK(clicks < 3.0);
    CHECK(peakAbs(x) <= kLimit);
}

// Afinação: a fundamental (camada Low) cai exatamente na frequência teórica em todos os tons.
TEST(motor_afinacao_12_tons) {
    PadEngine& e = sharedEngine();
    resetEngine(e);
    dryConfig(e);
    soloLayer(e, ParamId::LayerLow);
    e.setParam(ParamId::Attack, 0.05f);
    e.setParam(ParamId::CrossfadeTime, 0.3f);
    double worst = 0;
    for (int k = 0; k < 12; ++k) {
        e.playChord(k, ChordType::Pad5);
        auto x = render(e, 2.0);
        const Voicing v = buildVoicing(k, ChordType::Pad5);
        const double expected = midiToHz(v.lowNote);
        const double f = peakFrequency(mono(x), kSr, fr(0.6), 65536, expected * 0.8, expected * 1.2);
        const double cents = 1200.0 * std::log2(f / expected);
        worst = std::max(worst, std::fabs(cents));
    }
    report("maior erro de afinação nos 12 tons: %.2f cents", worst);
    CHECK(worst < 1.0);
}

// (3) Alteração de tonalidade: 12 trocas sem queda/pico de nível e sem cliques.
TEST(motor_troca_de_tonalidade) {
    PadEngine& e = sharedEngine();
    resetEngine(e);
    dryConfig(e);
    e.setParam(ParamId::Attack, 0.5f);
    e.setParam(ParamId::CrossfadeTime, 1.5f);
    const double period = 3.0;
    int key = 0;
    auto x = render(e, period * 13, 192, [&](double t) {
        if (t >= key * period && key < 13) { e.setKey((key * 7) % 12); ++key; } // ciclo de quintas
    });
    CHECK(allFinite(x));
    // Janelas de 300 ms: as fontes PADsynth e os naipes reais têm flutuação natural de
    // ±1,5 dB em 100 ms (batimentos dentro de cada harmônico), presente também com o acorde
    // parado; a média alinhada das 12 trocas é plana (±0,7 dB). 300 ms ainda pegam um
    // buraco ou um "inchaço" de crossfade, que dura da ordem do tempo de crossfade (1,5 s).
    auto w = windowRmsDb(x, 14400);
    double worstDip = 0, worstBump = 0, worstClick = 0;
    for (int k = 1; k < 13; ++k) {
        // A transição (2 s a partir da troca) é comparada com o acorde parado de antes e o de
        // depois: cada tom tem seu próprio nível (registro do voicing), o que não é defeito
        // do crossfade. Uma queda/pico real fica fora da faixa entre os dois.
        const size_t b = static_cast<size_t>(k * period * 10 / 3), en = b + 6;
        const LevelRange before = rangeDb(w, b - 3, b);
        const LevelRange after = rangeDb(w, b + 7, b + 10);
        const LevelRange t = rangeDb(w, b, en);
        worstDip = std::max(worstDip, std::min(before.min, after.min) - t.min);
        worstBump = std::max(worstBump, t.max - std::max(before.max, after.max));
        worstClick = std::max(worstClick, clickScore(x, fr(k * period), fr(k * period + 2.0)));
    }
    report("pior queda %.2f dB, pior pico %.2f dB, índice de clique %.2f", worstDip, worstBump, worstClick);
    CHECK(worstDip < 2.0);
    CHECK(worstBump < 2.0);
    CHECK(worstClick < 3.0);
}

// (4) Mudança de acordes com notas comuns mantidas (voice leading).
TEST(motor_troca_de_acordes_voice_leading) {
    PadEngine& e = sharedEngine();
    resetEngine(e);
    dryConfig(e);
    e.setParam(ParamId::Attack, 0.2f);
    e.setParam(ParamId::CrossfadeTime, 1.0f);
    e.playChord(0, ChordType::Major); // C: 48 55 60 64 67
    render(e, 1.0);
    const VoiceManager& vm = e.voiceManagerForTests();
    uint64_t ageE = 0;
    int idxE = -1;
    for (int i = 0; i < VoiceManager::kMaxVoices; ++i)
        if (!vm.voice(i).isFree() && vm.voice(i).note() == 64) { ageE = vm.voice(i).age(); idxE = i; }
    CHECK(idxE >= 0);
    e.playChord(9, ChordType::Minor); // Am: 57 64 69 72 76 -> E4 (64) é comum
    render(e, 0.2);
    CHECK(vm.voice(idxE).note() == 64 && vm.voice(idxE).age() == ageE);
    CHECK(vm.voice(idxE).state() == Voice::State::Active);
    int active = 0, fading = 0;
    for (int i = 0; i < VoiceManager::kMaxVoices; ++i) {
        if (vm.voice(i).state() == Voice::State::Active) ++active;
        if (vm.voice(i).state() == Voice::State::FadingOut) ++fading;
    }
    report("após C -> Am: %d vozes ativas (5 notas), %d saindo (4 notas de C não comuns)", active, fading);
    CHECK(active == 5);
    CHECK(fading == 4);
    render(e, 1.2);
    CHECK(vm.activeCount() == 5);
}

// (5) e (12) Crossfade entre acordes sem notas comuns: nível e picos controlados.
TEST(motor_crossfade_nivel_e_picos) {
    PadEngine& e = sharedEngine();
    resetEngine(e);
    dryConfig(e);
    e.setParam(ParamId::Attack, 0.3f);
    for (float xf : {0.25f, 1.0f, 3.0f}) {
        e.setParam(ParamId::CrossfadeTime, xf);
        e.playChord(0, ChordType::Pad5);   // 48 55 60 67
        bool changed = false;
        auto x = render(e, 4.0 + xf + 2.0, 192, [&](double t) {
            if (!changed && t >= 4.0) { e.playChord(6, ChordType::Pad5); changed = true; } // F#: 54 61 66 73 (nenhuma comum)
        });
        auto w = windowRmsDb(x, 2400); // 50 ms
        const LevelRange st = rangeDb(w, 40, 80);
        const LevelRange tr = rangeDb(w, 80, static_cast<size_t>((4.0 + xf + 0.5) * 20));
        const LevelRange af = rangeDb(w, static_cast<size_t>((4.0 + xf + 0.5) * 20), w.size());
        float pkSteady = 0, pkTrans = 0;
        for (size_t i = fr(2.0); i < fr(4.0); ++i) pkSteady = std::max({pkSteady, std::fabs(x[2 * i]), std::fabs(x[2 * i + 1])});
        for (size_t i = fr(4.0); i < fr(4.0 + xf); ++i) pkTrans = std::max({pkTrans, std::fabs(x[2 * i]), std::fabs(x[2 * i + 1])});
        const double refMin = std::min(st.min, af.min), refMax = std::max(st.max, af.max);
        const double pkDb = 20 * std::log10(pkTrans / pkSteady);
        report("crossfade %.2f s: nível na transição %.1f..%.1f dB (referência %.1f..%.1f), pico %+.2f dB",
               xf, tr.min, tr.max, refMin, refMax, pkDb);
        CHECK(tr.min > refMin - 1.5);
        CHECK(tr.max < refMax + 1.5);
        CHECK(pkDb < 2.0);
        CHECK(clickScore(x, fr(3.9), fr(4.0 + xf + 0.2)) < 3.0);
        e.panic();
        render(e, 0.5);
    }
}

// (6) Camadas individuais e simultâneas.
TEST(motor_camadas_individuais_e_combinadas) {
    PadEngine& e = sharedEngine();
    struct L { ParamId id; const char* name; double centroid; double rms; double hf; };
    L layers[] = {{ParamId::LayerLow, "Low", 0, 0, 0},       {ParamId::LayerWarm, "Warm", 0, 0, 0},
                  {ParamId::LayerStrings, "Strings", 0, 0, 0}, {ParamId::LayerShimmer, "Shimmer", 0, 0, 0},
                  {ParamId::LayerAir, "Air", 0, 0, 0},        {ParamId::LayerVox, "Vox", 0, 0, 0},
                  {ParamId::LayerTexture, "Texture", 0, 0, 0}};
    for (L& l : layers) {
        resetEngine(e);
        dryConfig(e);
        soloLayer(e, l.id);
        e.setParam(ParamId::Attack, 0.1f);
        e.playChord(9, ChordType::Pad5);
        auto x = render(e, 2.5);
        auto m = mono(x);
        l.centroid = spectralCentroid(m, kSr, fr(1.0), 65536);
        l.rms = rmsDb(x, fr(1.0), fr(2.5));
        auto db = magnitudeSpectrumDb(m, fr(1.0), 65536);
        double hi = 0, tot = 0;
        for (size_t k = 1; k < db.size(); ++k) { const double pw = std::pow(10.0, db[k] / 10); tot += pw; if (k * kSr / 65536 > 10000) hi += pw; }
        l.hf = 10 * std::log10(hi / tot + 1e-30);
        report("%-8s RMS %.1f dB, centroide %.0f Hz, energia acima de 10 kHz %.1f dB", l.name, l.rms, l.centroid, l.hf);
        CHECK(l.rms > -50.0);
        CHECK(peakAbs(x) <= kLimit);
    }
    CHECK(layers[0].centroid < 250.0);                   // Low: só graves
    CHECK(layers[0].centroid < layers[1].centroid);      // Low < Warm
    CHECK(layers[3].centroid > layers[1].centroid);      // Shimmer acima do Warm
    CHECK(layers[4].centroid > 1500.0);                  // Air: região aérea
    // Air corrigido na origem: no mesmo volume de camada fica >= 13 dB abaixo do corpo e sem chiado.
    report("Air em relação ao Warm (mesmo volume de camada): %.1f dB", layers[4].rms - layers[1].rms);
    CHECK(layers[4].rms < layers[1].rms - 13.0);
    CHECK(layers[4].hf < -40.0);
    for (const L& l : layers) if (l.id != ParamId::LayerStrings) CHECK(l.hf < -40.0); // nenhuma fonte com agudos ásperos
    // Todas juntas
    resetEngine(e);
    dryConfig(e);
    for (const L& l : layers) e.setParam(l.id, 1.0f);
    e.waitForSources(60.0);
    e.setParam(ParamId::Attack, 0.1f);
    e.playChord(9, ChordType::Pad5);
    auto x = render(e, 2.5);
    const double all = rmsDb(x, fr(1.0), fr(2.5));
    double maxSolo = -1e9;
    for (const L& l : layers) maxSolo = std::max(maxSolo, l.rms);
    report("todas as camadas: RMS %.1f dB (maior camada isolada %.1f dB)", all, maxSolo);
    CHECK(all > maxSolo);
    CHECK(peakAbs(x) <= kLimit);
}

// (7) Gerenciamento de vozes: contagem correta e liberação após o release.
TEST(motor_gerenciamento_de_vozes) {
    PadEngine& e = sharedEngine();
    resetEngine(e);
    e.setParam(ParamId::Release, 1.0f);
    e.playChord(4, ChordType::Add9); // 6 notas
    render(e, 1.0);
    CHECK(e.readStats().activeVoices == 6);
    CHECK(e.readStats().playing);
    e.stop();
    render(e, 0.5);
    CHECK(e.readStats().activeVoices == 6); // ainda em release
    render(e, 0.7);
    auto st = e.readStats();
    CHECK(st.activeVoices == 0);
    CHECK(!st.playing);
    // Retomar durante o release reaproveita as mesmas vozes (sem dobrar notas).
    e.playChord(4, ChordType::Add9);
    render(e, 0.3);
    e.stop();
    render(e, 0.2);
    e.playChord(4, ChordType::Add9);
    render(e, 0.3);
    CHECK(e.readStats().activeVoices == 6);
}

// (8) Voice stealing. No motor, notas repetidas reaproveitam a mesma voz e vozes que mal
// entraram saem rápido (o fade de saída parte do ganho atual), então o uso real fica bem
// abaixo de 48. Para forçar o roubo, o teste usa o VoiceManager com acordes aleatórios
// em 6 oitavas e ciclos tocar/parar com Release longo (cada parada deixa 6 vozes soando).
namespace {
Stereo renderVoices(VoiceManager& vm, const RenderContext& ctx, double seconds, const std::function<void(double)>& ev) {
    const size_t frames = fr(seconds);
    Stereo out(frames * 2, 0.0f);
    float pl[kSubBlock], pr[kSubBlock], al[kSubBlock], ar[kSubBlock], sl[kSubBlock], sr[kSubBlock], ll[kSubBlock], lr[kSubBlock];
    for (size_t done = 0; done < frames; done += kSubBlock) {
        if (ev) ev(static_cast<double>(done) / kSr);
        const int n = static_cast<int>(std::min<size_t>(kSubBlock, frames - done));
        for (int i = 0; i < n; ++i) pl[i] = pr[i] = al[i] = ar[i] = sl[i] = sr[i] = ll[i] = lr[i] = 0.0f;
        Buses b{pl, pr, al, ar, sl, sr, ll, lr};
        g_trackAllocations = true;
        vm.render(ctx, b, n);
        g_trackAllocations = false;
        for (int i = 0; i < n; ++i) {
            out[2 * (done + i)] = (pl[i] + al[i] + sl[i] + ll[i]) * 0.3f;
            out[2 * (done + i) + 1] = (pr[i] + ar[i] + sr[i] + lr[i]) * 0.3f;
        }
    }
    return out;
}
RenderContext testContext() {
    RenderContext ctx = sharedEngine().contextForTests();
    ctx.layerWarm = 0.8f; ctx.layerAir = 0.3f; ctx.layerStrings = 0.6f; ctx.layerShimmer = 0.3f; ctx.layerLow = 0.5f;
    ctx.layerVox = 0.4f; ctx.layerTexture = 0.4f; ctx.evolution = 0.5f; ctx.intensity = 0.5f; ctx.warmth = 0.5f;
    ctx.brightness = 0.5f; ctx.air = 0.4f; ctx.movement = 0.4f; ctx.detune = 0.4f;
    return ctx;
}
Voicing randomWideVoicing(Rng& rng) {
    Voicing v;
    v.count = 6;
    for (int i = 0; i < 6; ++i) v.notes[i] = 30 + static_cast<int>(rng.uniform() * 70);
    v.rootNote = v.notes[0];
    v.lowNote = 33 + (v.rootNote % 12);
    return v;
}
} // namespace

TEST(motor_voice_stealing) {
    VoiceManager vm;
    vm.prepare(kSr);
    const RenderContext ctx = testContext();
    Rng rng(11);
    Voicing last = randomWideVoicing(rng);
    vm.playChord(last, 0.2f, 1.0f, 20.0f, ctx);
    int maxVoices = 0;
    bool currentAlwaysAlive = true, playing = true;
    double next = 1.0;
    g_allocationCount = 0;
    auto x = renderVoices(vm, ctx, 7.0, [&](double t) {
        if (t >= next && t < 5.0) {
            if (playing) vm.stopAll(20.0f);
            else { last = randomWideVoicing(rng); vm.playChord(last, 0.05f, 1.0f, 20.0f, ctx); }
            playing = !playing;
            next = t + 0.06;
        }
        maxVoices = std::max(maxVoices, vm.activeCount());
        // As notas do acorde atual nunca podem ser roubadas.
        for (int k = 0; playing && k < last.count; ++k) {
            bool alive = false;
            for (int i = 0; i < VoiceManager::kMaxVoices; ++i)
                alive |= vm.voice(i).note() == last.notes[k] && vm.voice(i).state() == Voice::State::Active;
            currentAlwaysAlive &= alive;
        }
    });
    report("máximo de vozes %d/%d, roubos %llu, roubos forçados %llu, alocações %ld", maxVoices, VoiceManager::kMaxVoices,
           static_cast<unsigned long long>(vm.stealCount()), static_cast<unsigned long long>(vm.hardStealCount()), g_allocationCount.load());
    CHECK(maxVoices <= VoiceManager::kMaxVoices);
    CHECK(vm.stealCount() > 20);
    CHECK(vm.hardStealCount() == 0);
    CHECK(currentAlwaysAlive);
    CHECK(g_allocationCount.load() == 0);
    CHECK(allFinite(x));
    const double clicks = clickScore(x, fr(1.0), fr(5.0));
    report("índice de clique durante os roubos: %.2f", clicks);
    CHECK(clicks < 3.0);

    // No motor (voicings reais), trocas aleatórias rápidas não chegam ao limite.
    PadEngine& e = sharedEngine();
    resetEngine(e);
    e.setParam(ParamId::CrossfadeTime, 10.0f);
    int engMax = 0;
    double n2 = 0.0;
    render(e, 6.0, 192, [&](double t) {
        if (t >= n2) { e.playChord(static_cast<int>(rng.uniform() * 12), static_cast<ChordType>(static_cast<int>(rng.uniform() * 10))); n2 = t + 0.05; }
        engMax = std::max(engMax, e.readStats().activeVoices);
    });
    report("motor com voicings reais, 120 trocas em 6 s, crossfade 10 s: máximo de %d vozes", engMax);
    CHECK(engMax <= VoiceManager::kMaxVoices - VoiceManager::kReserveVoices);
}

// (9) Alterações rápidas e sucessivas (pior caso de operação ao vivo).
TEST(motor_trocas_rapidas_sucessivas) {
    PadEngine& e = sharedEngine();
    resetEngine(e, 3); // Shimmer: reverb longo + realimentação
    Rng rng(21);
    e.playChord(0, ChordType::Pad5);
    double next = 4.0;
    int changes = 0;
    auto x = render(e, 24.0, 192, [&](double t) {
        if (t >= next && t < 20.0) {
            e.setParam(ParamId::CrossfadeTime, rng.range(0.1f, 2.0f));
            e.playChord(static_cast<int>(rng.uniform() * 12), static_cast<ChordType>(static_cast<int>(rng.uniform() * 10)));
            next = t + rng.range(0.015f, 0.25f);
            ++changes;
        }
    });
    auto w = windowRmsDb(x, 4800);
    const LevelRange steady = rangeDb(w, 25, 40);
    const LevelRange busy = rangeDb(w, 40, 200);
    report("%d trocas: nível %.1f..%.1f dB (parado %.1f..%.1f dB), pico %.3f",
           changes, busy.min, busy.max, steady.min, steady.max, peakAbs(x));
    CHECK(allFinite(x));
    CHECK(busy.max < steady.max + 3.0);
    CHECK(peakAbs(x) <= kLimit);
    CHECK(clickScore(x, fr(4.0), fr(20.0)) < 3.0);
    e.stop();
    render(e, e.getParam(ParamId::Release) + 0.5);
    CHECK(e.readStats().activeVoices == 0);
}

// (11) Ausência de clipping no pior caso (tudo no máximo).
TEST(motor_sem_clipping_pior_caso) {
    PadEngine& e = sharedEngine();
    resetEngine(e);
    for (int i = 0; i < kParamCount; ++i) {
        const auto id = static_cast<ParamId>(i);
        if (id == ParamId::Attack || id == ParamId::Release || id == ParamId::CrossfadeTime) continue;
        e.setParam(id, paramInfo(id).maxValue);
    }
    e.setParam(ParamId::Attack, 0.01f);
    e.setParam(ParamId::CrossfadeTime, 0.05f);
    Rng rng(4);
    double next = 0.0;
    auto x = render(e, 20.0, 192, [&](double t) {
        if (t >= next) { e.playChord(static_cast<int>(rng.uniform() * 12), ChordType::Add9); next = t + 0.5; }
    });
    const auto st = e.readStats();
    report("pico %.4f (limite 0,891), amostras >= 0,999: %zu, menor ganho do limitador %.2f (%.1f dB)",
           peakAbs(x), countAbove(x, 0.999f), st.limiterMinGain, 20 * std::log10(st.limiterMinGain));
    CHECK(allFinite(x));
    CHECK(peakAbs(x) <= kLimit);
    CHECK(countAbove(x, 0.999f) == 0);
}

// Presets: todos tocam limpos e com volume parecido (troca de preset sem susto de volume).
TEST(motor_presets_equilibrio) {
    PadEngine& e = sharedEngine();
    double lo = 1e9, hi = -1e9;
    for (int p = 0; p < presetCount(); ++p) {
        resetEngine(e, p);
        e.setSeed(3); // microeventos (ex.: Guitar Pad) com a mesma sequência em toda execução
        e.waitForSources(60.0); // e com as fontes já prontas (sob ASan o carregamento é lento)
        e.playChord(7, ChordType::Major);
        auto x = render(e, 14.0);
        const double rms = rmsDb(x, fr(8.0), fr(14.0));
        const double c = spectralCentroid(mono(x), kSr, fr(9.0), 131072);
        const auto st = e.readStats();
        report("%-10s RMS %.1f dB, pico %.1f dB, centroide %.0f Hz, correlação L/R %.2f, limitador %.2f",
               preset(p).name, rms, 20 * std::log10(peakAbs(x)), c, lrCorrelation(x, fr(8), fr(14)), st.limiterMinGain);
        lo = std::min(lo, rms); hi = std::max(hi, rms);
        CHECK(peakAbs(x) <= kLimit);
        CHECK(st.limiterMinGain > 0.7f); // presets não devem depender do limitador
        CHECK(lrCorrelation(x, fr(8), fr(14)) > -0.25); // compatível com mono (mesa de som / PA mono)
    }
    report("diferença de volume entre presets: %.1f dB", hi - lo);
    CHECK(hi - lo < 5.0);
}

// (13) Processamento estéreo: largura controlável e compatível com mono.
TEST(motor_processamento_estereo) {
    PadEngine& e = sharedEngine();
    double corr[3];
    const float widths[3] = {0.0f, 1.0f, 1.5f};
    for (int i = 0; i < 3; ++i) {
        resetEngine(e);
        e.setParam(ParamId::Width, widths[i]);
        e.setParam(ParamId::Attack, 0.1f);
        e.playChord(5, ChordType::Sus2);
        auto x = render(e, 6.0);
        corr[i] = lrCorrelation(x, fr(3), fr(6));
        // Compatibilidade mono: perda de nível ao somar L+R.
        double es = 0, em = 0;
        for (size_t k = fr(3); k < fr(6); ++k) {
            const double l = x[2 * k], r = x[2 * k + 1];
            es += 0.5 * (l * l + r * r);
            em += 0.25 * (l + r) * (l + r);
        }
        report("largura %.1f: correlação L/R %.3f, perda ao somar em mono %.1f dB", widths[i], corr[i], 10 * std::log10(em / es));
        CHECK(10 * std::log10(em / es) > -4.5);
    }
    CHECK(corr[0] > 0.999);
    CHECK(corr[1] > 0.1 && corr[1] < 0.95);
    CHECK(corr[2] < corr[1]);
}

// (14) Consumo de recursos: CPU com polifonia alta e zero alocação no callback.
TEST(motor_consumo_de_recursos) {
    PadEngine& e = sharedEngine();
    resetEngine(e, 4); // Cinematic: todas as camadas ativas
    e.setParam(ParamId::CrossfadeTime, 10.0f);
    Rng rng(8);
    double next = 0.0;
    render(e, 4.0, 192, [&](double t) {
        if (t >= next) { e.playChord(static_cast<int>(rng.uniform() * 12), static_cast<ChordType>(static_cast<int>(rng.uniform() * 10))); next = t + 0.08; }
    });
    const int voices = e.readStats().activeVoices;
    g_allocationCount = 0;
    const auto t0 = std::chrono::steady_clock::now();
    next = 0.0;
    auto x = render(e, 10.0, 192, [&](double t) {
        if (t >= next) { e.playChord(static_cast<int>(rng.uniform() * 12), static_cast<ChordType>(static_cast<int>(rng.uniform() * 10))); next = t + 0.08; }
    }, true);
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    report("motor completo com ~%d vozes: 10 s de áudio em %.2f s -> %.1fx tempo real (%.1f%% de um núcleo)",
           voices, secs, 10.0 / secs, 10.0 * secs);
    report("alocações de memória durante process(): %ld", g_allocationCount.load());
    CHECK(voices >= 10);
    CHECK(g_allocationCount.load() == 0);
    if (!kSanitizerBuild) CHECK(10.0 / secs > 3.0); // com sanitizers o código fica 5–10x mais lento

    // Custo por voz (todas as camadas) e projeção para 48 vozes.
    VoiceManager vm;
    vm.prepare(kSr);
    const RenderContext ctx = testContext();
    double n3 = 0.0;
    bool on = false;
    renderVoices(vm, ctx, 2.0, [&](double t) {
        if (t >= n3) {
            if (on) vm.stopAll(30.0f); else vm.playChord(randomWideVoicing(rng), 0.05f, 1.0f, 30.0f, ctx);
            on = !on;
            n3 = t + 0.05;
        }
    });
    const int v48 = vm.activeCount();
    const auto t2 = std::chrono::steady_clock::now();
    renderVoices(vm, ctx, 5.0, nullptr);
    const double s2 = std::chrono::duration<double>(std::chrono::steady_clock::now() - t2).count();
    report("%d vozes (só vozes, sem efeitos): %.1fx tempo real; custo por voz %.2f%% de um núcleo",
           v48, 5.0 / s2, 100.0 * s2 / 5.0 / std::max(v48, 1));
    CHECK(v48 >= 40);
    if (!kSanitizerBuild) CHECK(5.0 / s2 > 2.0);

    // Efeitos sem vozes (custo fixo da cadeia master).
    resetEngine(e, 4);
    const auto t3 = std::chrono::steady_clock::now();
    render(e, 10.0);
    const double s3 = std::chrono::duration<double>(std::chrono::steady_clock::now() - t3).count();
    report("cadeia de efeitos sozinha (reverb FDN + shimmer + filtros + limitador): %.2f%% de um núcleo", 10.0 * s3);

    // Caso típico em culto: um acorde de 5 notas.
    resetEngine(e, 0);
    e.playChord(0, ChordType::Major);
    render(e, 1.0);
    const auto t1 = std::chrono::steady_clock::now();
    render(e, 20.0);
    const double s1 = std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count();
    report("acorde de 5 notas, preset Warm: %.0fx tempo real (%.2f%% de um núcleo)", 20.0 / s1, 100.0 * s1 / 20.0);
    const size_t mem = e.readStats().sourcesMemoryBytes;
    report("memória das fontes carregadas (preset Warm): %.1f MB", mem / 1e6);
    CHECK(mem < 200u * 1024u * 1024u);
}

// (15) Estabilidade em reprodução prolongada (10 minutos simulados, com trocas e presets).
TEST(motor_reproducao_prolongada_10min) {
    PadEngine& e = sharedEngine();
    resetEngine(e, 0);
    Rng rng(99);
    double nextChord = 0.0, nextPreset = 60.0;
    int preset = 0;
    std::vector<double> minuteRms;
    double acc = 0, dc = 0;
    size_t n = 0;
    bool finite = true;
    float peak = 0;
    const size_t total = fr(600.0);
    std::vector<float> buf(2 * 256);
    for (size_t done = 0; done < total; done += 256) {
        const double t = static_cast<double>(done) / kSr;
        if (t >= nextChord && t < 570.0) {
            e.playChord(static_cast<int>(rng.uniform() * 12), static_cast<ChordType>(static_cast<int>(rng.uniform() * 10)));
            nextChord = t + rng.range(2.0f, 12.0f);
        }
        if (t >= nextPreset) { preset = (preset + 1) % presetCount(); e.applyPreset(preset); nextPreset += 60.0; }
        if (t >= 570.0 && t - 256.0 / kSr < 570.0) e.stop();
        e.process(buf.data(), 256);
        for (float v : buf) { if (!std::isfinite(v)) finite = false; acc += double(v) * v; dc += v; peak = std::max(peak, std::fabs(v)); }
        n += 512;
        if ((done + 256) % fr(60.0) < 256) { minuteRms.push_back(10 * std::log10(acc / n + 1e-20)); acc = 0; n = 0; }
    }
    const auto st = e.readStats();
    std::string s;
    for (double r : minuteRms) { char b[16]; std::snprintf(b, sizeof b, " %.1f", r); s += b; }
    report("RMS por minuto (dB):%s", s.c_str());
    report("pico %.3f, DC médio %.2e, roubos forçados %llu, vozes no fim %d", peak, dc / (total * 2.0),
           static_cast<unsigned long long>(st.hardSteals), st.activeVoices);
    CHECK(finite);
    CHECK(peak <= kLimit);
    CHECK(std::fabs(dc / (total * 2.0)) < 1e-4);
    CHECK(st.activeVoices == 0);
    for (size_t i = 0; i + 1 < minuteRms.size(); ++i) CHECK(minuteRms[i] > -35.0 && minuteRms[i] < -8.0);
}

// Parâmetros: limites seguros contra valores inválidos.
TEST(motor_parametros_limites_seguros) {
    PadEngine& e = sharedEngine();
    resetEngine(e);
    e.setParam(ParamId::Width, 99.0f);
    CHECK(e.getParam(ParamId::Width) == paramInfo(ParamId::Width).maxValue);
    e.setParam(ParamId::Attack, -5.0f);
    CHECK(e.getParam(ParamId::Attack) == paramInfo(ParamId::Attack).minValue);
    const float before = e.getParam(ParamId::Reverb);
    e.setParam(ParamId::Reverb, std::nanf(""));
    CHECK(e.getParam(ParamId::Reverb) == before);
    e.setParam(static_cast<ParamId>(999), 1.0f); // id inválido: ignorado
    // Movimento brusco de todos os controles com som tocando: sem cliques.
    e.setParam(ParamId::Attack, 0.1f);
    e.playChord(0, ChordType::Major);
    Rng rng(2);
    double next = 2.0;
    auto x = render(e, 6.0, 192, [&](double t) {
        if (t >= next) {
            for (ParamId id : {ParamId::Warmth, ParamId::Brightness, ParamId::Air, ParamId::Low, ParamId::Width, ParamId::LayerWarm,
                               ParamId::LayerStrings, ParamId::LayerAir, ParamId::Reverb, ParamId::MasterVolume})
                e.setParam(id, rng.range(paramInfo(id).minValue, paramInfo(id).maxValue));
            next = t + 0.25;
        }
    });
    const double clicks = clickScore(x, fr(2.0), fr(6.0));
    report("controles mudando a cada 250 ms: índice de clique %.2f", clicks);
    CHECK(allFinite(x));
    CHECK(clicks < 3.0);
}

// Validação do próprio detector de cliques: ele precisa acusar um degrau real.
TEST(detector_de_clique_valido) {
    PadEngine& e = sharedEngine();
    resetEngine(e, 0);
    e.playChord(0, ChordType::Major);
    auto x = render(e, 6.0);
    const double clean = clickScore(x, fr(4.0), fr(6.0));
    Stereo y = x;
    // Degrau de -3 dB num ponto de amplitude típica (|x| > 1,5x RMS), como uma voz cortada sem rampa.
    const double rms = std::pow(10.0, rmsDb(x, fr(4.0), fr(6.0)) / 20.0);
    size_t at = fr(5.0);
    while (at < fr(5.5) && std::fabs(x[2 * at]) < 1.5 * rms) ++at;
    for (size_t i = at; i < y.size() / 2; ++i) { y[2 * i] *= 0.7f; y[2 * i + 1] *= 0.7f; }
    const double stepped = clickScore(y, fr(4.0), fr(6.0));
    report("sinal limpo: %.2f; com degrau de ganho de -3 dB: %.2f", clean, stepped);
    CHECK(clean < 3.0);
    CHECK(stepped > 3.0);
}
