#include "SampleBank.h"
#include "../dsp/Random.h"
#include "../dsp/Crossfade.h"
#include <thread>
#include <algorithm>

namespace pad {

namespace {

constexpr int kSinBits = 14;
constexpr int kSinSize = 1 << kSinBits;

struct SinTable {
    std::vector<float> t;
    SinTable() : t(kSinSize + 1) {
        for (int i = 0; i <= kSinSize; ++i) t[static_cast<size_t>(i)] = static_cast<float>(std::sin(2.0 * kPiD * i / kSinSize));
    }
    // phase em ciclos (qualquer valor real)
    inline float sinCycles(double phase) const {
        double p = phase - std::floor(phase);
        const double idx = p * kSinSize;
        const int i = static_cast<int>(idx);
        const float f = static_cast<float>(idx - i);
        return t[static_cast<size_t>(i)] + (t[static_cast<size_t>(i + 1)] - t[static_cast<size_t>(i)]) * f;
    }
};

// Resposta "de corpo" de instrumento de cordas: ressonâncias largas suaves (sem copiar
// nenhum instrumento específico) e queda progressiva de agudos.
float stringSpectrum(int h, float f) {
    const auto bump = [](float freq, float center, float width) {
        const float d = std::log2(freq / center);
        return std::exp(-(d * d) / width);
    };
    float a = 1.0f / std::pow(static_cast<float>(h), 0.85f);
    a *= 1.0f + 0.55f * bump(f, 480.0f, 0.20f) + 0.40f * bump(f, 1650.0f, 0.14f) + 0.22f * bump(f, 3200.0f, 0.10f);
    a *= 1.0f / (1.0f + (f / 3600.0f) * (f / 3600.0f));
    if (f < 90.0f) a *= 0.6f; // fundamental muito grave fica com a camada Low
    return a;
}

struct Partial {
    int64_t k;         // ciclos inteiros no loop -> periodicidade exata
    float amp;
    int ampM;          // ciclos da modulação de amplitude no loop
    float ampDepth;
    float ampPhase;
};

struct Player {
    int driftM; float driftBeta; float driftPhase;   // deriva lenta de afinação (por fundamental)
    int vibM; float vibBeta; float vibPhase;          // vibrato de ensemble
    std::vector<Partial> partials;
};

void renderZone(SampleZone& zone, int note, float sampleRate, float loopSeconds, uint64_t seed) {
    static const SinTable sinTab;
    const int N = static_cast<int>(std::lround(loopSeconds * sampleRate));
    const double T = static_cast<double>(N) / sampleRate; // período exato em segundos
    const double f0 = midiToHz(note);
    Rng rng(seed ^ (static_cast<uint64_t>(note) * 0x9E3779B97F4A7C15ULL));

    constexpr int kPlayers = 4;
    const float detuneCents[kPlayers] = {-6.5f, -2.0f, 2.5f, 7.0f};
    const int H = std::max(4, std::min(40, static_cast<int>(9500.0 / f0)));

    std::vector<Player> players(kPlayers);
    for (int p = 0; p < kPlayers; ++p) {
        Player& pl = players[static_cast<size_t>(p)];
        const double fp = f0 * std::exp2((detuneCents[p] + rng.range(-1.0f, 1.0f)) / 1200.0);
        pl.driftM = 1 + static_cast<int>(rng.uniform() * 3.0f);             // 0,17–0,5 Hz
        const double driftHz = fp * (std::exp2(2.2 / 1200.0) - 1.0);         // ±2,2 cents
        pl.driftBeta = static_cast<float>(driftHz * T / pl.driftM);          // índice de modulação de fase (ciclos)
        pl.driftPhase = rng.uniform();
        pl.vibM = static_cast<int>(std::lround(T * rng.range(4.6f, 5.6f)));  // ~5 Hz
        const double vibHz = fp * (std::exp2(3.0 / 1200.0) - 1.0);           // ±3 cents
        pl.vibBeta = static_cast<float>(vibHz * T / std::max(pl.vibM, 1));
        pl.vibPhase = rng.uniform();
        for (int h = 1; h <= H; ++h) {
            const double fh = fp * h;
            if (fh > 10000.0) break;
            Partial pa;
            pa.k = std::llround(fh * T);
            pa.amp = stringSpectrum(h, static_cast<float>(fh)) * rng.range(0.8f, 1.2f);
            pa.ampM = 1 + static_cast<int>(rng.uniform() * 4.0f);
            pa.ampDepth = rng.range(0.15f, 0.45f);
            pa.ampPhase = rng.uniform();
            pl.partials.push_back(pa);
        }
    }

    std::vector<float> loop(static_cast<size_t>(N), 0.0f);
    constexpr int kCtl = 32; // taxa de controle da modulação de amplitude
    std::vector<float> ampA, ampB;
    for (const Player& pl : players) {
        const size_t np = pl.partials.size();
        ampA.assign(np, 0.0f); ampB.assign(np, 0.0f);
        auto ampAt = [&](const Partial& pa, int n) {
            const double ph = static_cast<double>(pa.ampM) * n / N + pa.ampPhase;
            return pa.amp * (1.0f + pa.ampDepth * sinTab.sinCycles(ph));
        };
        for (size_t j = 0; j < np; ++j) ampA[j] = ampAt(pl.partials[j], 0);
        // Acumuladores inteiros de fase (k*n mod N sem divisão): periodicidade exata.
        std::vector<int64_t> pidx(np, 0);
        const double invN = 1.0 / N;
        for (int n0 = 0; n0 < N; n0 += kCtl) {
            const int n1 = std::min(n0 + kCtl, N);
            for (size_t j = 0; j < np; ++j) ampB[j] = ampAt(pl.partials[j], n1);
            for (int n = n0; n < n1; ++n) {
                const double tn = static_cast<double>(n) / N;
                // modulação de fase compartilhada (em ciclos, por harmônico 1)
                const float pm = pl.driftBeta * sinTab.sinCycles(pl.driftM * tn + pl.driftPhase) +
                                 pl.vibBeta * sinTab.sinCycles(pl.vibM * tn + pl.vibPhase);
                const float a = static_cast<float>(n - n0) / static_cast<float>(n1 - n0);
                float acc = 0.0f;
                for (size_t j = 0; j < np; ++j) {
                    const Partial& pa = pl.partials[j];
                    const double ph = static_cast<double>(pidx[j]) * invN + static_cast<double>(j + 1) * pm;
                    pidx[j] += pa.k;
                    if (pidx[j] >= N) pidx[j] -= N;
                    acc += (ampA[j] + (ampB[j] - ampA[j]) * a) * sinTab.sinCycles(ph);
                }
                loop[static_cast<size_t>(n)] += acc;
            }
            std::swap(ampA, ampB);
        }
    }

    // Normaliza por RMS (volume homogêneo entre zonas): alvo -18 dBFS RMS.
    double e = 0.0;
    for (float v : loop) e += double(v) * v;
    const float rms = static_cast<float>(std::sqrt(e / N));
    const float g = rms > 0.0f ? 0.125f / rms : 1.0f;
    for (float& v : loop) v *= g;

    zone.rootNote = note;
    zone.rootHz = static_cast<float>(f0);
    zone.sampleRate = sampleRate;
    zone.length = N;
    zone.data.assign(static_cast<size_t>(N) + 4, 0.0f);
    std::copy(loop.begin(), loop.end(), zone.data.begin() + 1);
    zone.data[0] = loop[static_cast<size_t>(N - 1)];
    zone.data[static_cast<size_t>(N) + 1] = loop[0];
    zone.data[static_cast<size_t>(N) + 2] = loop[1];
    zone.data[static_cast<size_t>(N) + 3] = loop[2];
}

} // namespace

void SampleBank::bakeStringTexture(float sampleRate, uint64_t seed, int firstNote, int lastNote, int step, float loopSeconds) {
    std::vector<int> notes;
    for (int n = firstNote; n <= lastNote; n += step) notes.push_back(n);
    std::vector<SampleZone> zones(notes.size());

    const unsigned hw = std::max(1u, std::min(8u, std::thread::hardware_concurrency()));
    std::vector<std::thread> workers;
    for (unsigned w = 0; w < hw; ++w) {
        workers.emplace_back([&, w]() {
            for (size_t i = w; i < notes.size(); i += hw) renderZone(zones[i], notes[i], sampleRate, loopSeconds, seed);
        });
    }
    for (auto& t : workers) t.join();
    zones_ = std::move(zones);
}

float SampleBank::makeSeamlessLoop(const std::vector<float>& in, int loopLen, int xfadeLen, std::vector<float>& out) {
    out.assign(in.begin(), in.begin() + loopLen);
    xfadeLen = std::min(xfadeLen, static_cast<int>(in.size()) - loopLen);
    if (xfadeLen <= 0) return 0.0f;
    // Correlação entre o começo do loop e o trecho que continuaria após o fim.
    const float rho = estimateCorrelation(in.data(), in.data() + loopLen, xfadeLen);
    for (int i = 0; i < xfadeLen; ++i) {
        const float t = (static_cast<float>(i) + 0.5f) / static_cast<float>(xfadeLen);
        const CrossfadeGains g = crossfadeGains(t, rho);
        out[static_cast<size_t>(i)] = in[static_cast<size_t>(loopLen + i)] * g.out + in[static_cast<size_t>(i)] * g.in;
    }
    return rho;
}

void SampleBank::finalizeZone(SampleZone& z, const std::vector<float>& loop) {
    const int N = static_cast<int>(loop.size());
    z.length = N;
    z.data.assign(static_cast<size_t>(N) + 4, 0.0f);
    std::copy(loop.begin(), loop.end(), z.data.begin() + 1);
    z.data[0] = loop[static_cast<size_t>(N - 1)];
    for (int i = 0; i < 3; ++i) z.data[static_cast<size_t>(N + 1 + i)] = loop[static_cast<size_t>(i % N)];
}

bool SampleBank::addZoneFromAudio(const std::vector<float>& mono, float sampleRate, int rootNote, float crossfadeSeconds) {
    const int xf = static_cast<int>(crossfadeSeconds * sampleRate);
    const int total = static_cast<int>(mono.size());
    if (total < xf * 3 || total < static_cast<int>(sampleRate)) return false; // curto demais para um loop de pad
    // Remove DC e normaliza por RMS (mesmo alvo do banco gerado).
    std::vector<float> x(mono);
    double mean = 0.0;
    for (float v : x) mean += v;
    mean /= total;
    double e = 0.0;
    for (float& v : x) { v -= static_cast<float>(mean); e += double(v) * v; }
    const float rms = static_cast<float>(std::sqrt(e / total));
    if (rms < 1e-6f) return false;
    for (float& v : x) v *= 0.125f / rms;

    std::vector<float> loop;
    makeSeamlessLoop(x, total - xf, xf, loop);
    SampleZone z;
    z.rootNote = rootNote;
    z.rootHz = static_cast<float>(midiToHz(rootNote));
    z.sampleRate = sampleRate;
    finalizeZone(z, loop);
    zones_.push_back(std::move(z));
    std::sort(zones_.begin(), zones_.end(), [](const SampleZone& a, const SampleZone& b) { return a.rootNote < b.rootNote; });
    return true;
}

const SampleZone* SampleBank::zoneFor(float targetHz, float outputRate, double& ratioOut) const {
    if (zones_.empty()) return nullptr;
    const SampleZone* best = &zones_[0];
    float bestDist = 1e9f;
    for (const SampleZone& z : zones_) {
        const float d = std::fabs(std::log2(targetHz / z.rootHz));
        if (d < bestDist) { bestDist = d; best = &z; }
    }
    ratioOut = (static_cast<double>(targetHz) / best->rootHz) * (static_cast<double>(best->sampleRate) / outputRate);
    return best;
}

size_t SampleBank::memoryBytes() const {
    size_t b = 0;
    for (const auto& z : zones_) b += z.data.size() * sizeof(float);
    return b;
}

} // namespace pad
