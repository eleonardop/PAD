#include "AudioAnalysis.h"
#include "TestFramework.h"
#include <algorithm>
#include <cmath>

namespace padtest {

pad::PadEngine& sharedEngine() {
    static pad::PadEngine* e = [] {
        auto* p = new pad::PadEngine();
        p->setAssetProvider(std::make_shared<pad::FileAssetProvider>(PAD_ASSETS_DIR));
        p->prepare(48000.0f);
        return p;
    }();
    return *e;
}

void resetEngine(pad::PadEngine& e, int presetIndex) {
    e.panic();
    for (int i = 0; i < pad::kParamCount; ++i) {
        const auto id = static_cast<pad::ParamId>(i);
        e.setParam(id, pad::paramInfo(id).defaultValue);
    }
    if (presetIndex >= 0) e.applyPreset(presetIndex);
    e.waitForSources(60.0);
    // Silêncio longo o bastante para esvaziar vozes, cauda do reverb e suavizadores.
    std::vector<float> buf(2 * 512);
    e.setParam(pad::ParamId::Reverb, 0.0f);
    for (int i = 0; i < 48000 * 3 / 512; ++i) e.process(buf.data(), 512);
    e.setParam(pad::ParamId::Reverb, presetIndex >= 0 ? pad::preset(presetIndex).values[static_cast<int>(pad::ParamId::Reverb)]
                                                      : pad::paramInfo(pad::ParamId::Reverb).defaultValue);
    for (int i = 0; i < 48000 / 2 / 512; ++i) e.process(buf.data(), 512);
    e.readStats();
}

Stereo render(pad::PadEngine& e, double seconds, int block, const std::function<void(double)>& events, bool track) {
    const size_t frames = static_cast<size_t>(seconds * e.sampleRate());
    Stereo out(frames * 2);
    size_t done = 0;
    while (done < frames) {
        const int n = static_cast<int>(std::min<size_t>(static_cast<size_t>(block), frames - done));
        if (events) events(static_cast<double>(done) / e.sampleRate());
        if (track) g_trackAllocations = true;
        e.process(out.data() + 2 * done, n);
        if (track) g_trackAllocations = false;
        done += static_cast<size_t>(n);
    }
    return out;
}

double rmsDb(const Stereo& x, size_t b, size_t e) {
    double s = 0;
    for (size_t i = b; i < e; ++i) s += 0.5 * (double(x[2 * i]) * x[2 * i] + double(x[2 * i + 1]) * x[2 * i + 1]);
    return 10.0 * std::log10(s / std::max<size_t>(e - b, 1) + 1e-20);
}

std::vector<double> windowRmsDb(const Stereo& x, int w) {
    std::vector<double> r;
    const size_t frames = x.size() / 2;
    for (size_t i = 0; i + static_cast<size_t>(w) <= frames; i += static_cast<size_t>(w)) r.push_back(rmsDb(x, i, i + static_cast<size_t>(w)));
    return r;
}

float peakAbs(const Stereo& x) { float p = 0; for (float v : x) p = std::max(p, std::fabs(v)); return p; }
size_t countAbove(const Stereo& x, float t) { size_t c = 0; for (float v : x) c += std::fabs(v) >= t ? 1 : 0; return c; }
bool allFinite(const Stereo& x) { for (float v : x) if (!std::isfinite(v)) return false; return true; }

double lrCorrelation(const Stereo& x, size_t b, size_t e) {
    double lr = 0, ll = 0, rr = 0;
    for (size_t i = b; i < e; ++i) { const double l = x[2 * i], r = x[2 * i + 1]; lr += l * r; ll += l * l; rr += r * r; }
    return lr / std::sqrt(ll * rr + 1e-30);
}

double clickScore(const Stereo& x, size_t tb, size_t te) {
    // Clique = descontinuidade isolada. Para cada janela de 2,5 ms, compara o maior
    // |segunda diferença| com a mediana das janelas vizinhas (+-60 ms, sem ela mesma).
    // Mudanças graduais de timbre (filtro, brilho) afetam as vizinhas igualmente e não
    // pontuam; um degrau de ganho ou de fase aparece como pico isolado.
    const size_t w = 120;
    const size_t frames = x.size() / 2;
    std::vector<double> m;
    for (size_t s = 2; s + w <= frames; s += w) {
        double mx = 0;
        for (size_t i = s; i < s + w; ++i)
            for (int c = 0; c < 2; ++c) {
                const double d = double(x[2 * i + c]) - 2.0 * x[2 * (i - 1) + c] + x[2 * (i - 2) + c];
                mx = std::max(mx, std::fabs(d));
            }
        m.push_back(mx);
    }
    const long half = 24;
    double worst = 0;
    std::vector<double> nb;
    for (long i = static_cast<long>(tb / w); i < static_cast<long>(std::min(te / w, m.size())); ++i) {
        nb.clear();
        for (long j = i - half; j <= i + half; ++j)
            if (j >= 0 && j < static_cast<long>(m.size()) && j != i) nb.push_back(m[static_cast<size_t>(j)]);
        if (nb.size() < 8) continue;
        std::nth_element(nb.begin(), nb.begin() + nb.size() / 2, nb.end());
        const double med = nb[nb.size() / 2];
        if (med < 1e-7) continue; // silêncio
        worst = std::max(worst, m[static_cast<size_t>(i)] / med);
    }
    return worst;
}

void fft(std::vector<std::complex<double>>& a) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = -2.0 * M_PI / static_cast<double>(len);
        const std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0);
            for (size_t j = 0; j < len / 2; ++j) {
                const auto u = a[i + j], v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

std::vector<float> mono(const Stereo& x) {
    std::vector<float> m(x.size() / 2);
    for (size_t i = 0; i < m.size(); ++i) m[i] = 0.5f * (x[2 * i] + x[2 * i + 1]);
    return m;
}

std::vector<double> magnitudeSpectrumDb(const std::vector<float>& m, size_t start, size_t n) {
    std::vector<std::complex<double>> a(n);
    double wsum = 0;
    for (size_t i = 0; i < n; ++i) {
        const double t = 2.0 * M_PI * i / (n - 1);
        const double w = 0.35875 - 0.48829 * std::cos(t) + 0.14128 * std::cos(2 * t) - 0.01168 * std::cos(3 * t);
        wsum += w;
        a[i] = (start + i < m.size() ? m[start + i] : 0.0f) * w;
    }
    fft(a);
    std::vector<double> db(n / 2);
    for (size_t k = 0; k < n / 2; ++k) db[k] = 20.0 * std::log10(2.0 * std::abs(a[k]) / wsum + 1e-15);
    return db;
}

double spectralCentroid(const std::vector<float>& m, double sr, size_t start, size_t n) {
    auto db = magnitudeSpectrumDb(m, start, n);
    double num = 0, den = 0;
    for (size_t k = 1; k < db.size(); ++k) {
        const double p = std::pow(10.0, db[k] / 10.0);
        num += p * k * sr / n;
        den += p;
    }
    return num / (den + 1e-30);
}

double peakFrequency(const std::vector<float>& m, double sr, size_t start, size_t n, double fLo, double fHi) {
    auto db = magnitudeSpectrumDb(m, start, n);
    const size_t k0 = static_cast<size_t>(fLo * n / sr), k1 = std::min(db.size() - 2, static_cast<size_t>(fHi * n / sr));
    size_t best = k0;
    for (size_t k = k0; k <= k1; ++k) if (db[k] > db[best]) best = k;
    const double a = db[best - 1], b = db[best], c = db[best + 1];
    const double off = 0.5 * (a - c) / (a - 2 * b + c);
    return (best + off) * sr / n;
}

} // namespace padtest
