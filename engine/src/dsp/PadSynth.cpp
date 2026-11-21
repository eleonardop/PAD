#include "PadSynth.h"
#include "Fft.h"
#include "Random.h"
#include <algorithm>
#include <cmath>

namespace pad {

void generatePadSynth(const PadSynthSpec& spec, float f0, float sampleRate, int n, uint64_t seed, std::vector<float>& out) {
    const int half = n / 2;
    std::vector<double> amp(static_cast<size_t>(half), 0.0);
    const double binHz = static_cast<double>(sampleRate) / n;
    const double nyq = std::min<double>(spec.maxFreqHz, sampleRate * 0.45);

    for (int h = spec.firstHarmonic; h <= spec.maxHarmonics; ++h) {
        const double fh = static_cast<double>(f0) * h;
        if (fh >= nyq) break;
        const double a = spec.amplitude(h, static_cast<float>(fh));
        if (a <= 0.0) continue;
        // Largura (Hz) da faixa deste harmônico.
        const double bwHz = (std::exp2(spec.bandwidthCents / 1200.0) - 1.0) * f0 * std::pow(static_cast<double>(h), spec.bandwidthScale);
        const double sigma = std::max(bwHz * 0.5, binHz * 0.75); // em Hz
        const int k0 = std::max(1, static_cast<int>((fh - 4.0 * sigma) / binHz));
        const int k1 = std::min(half - 1, static_cast<int>((fh + 4.0 * sigma) / binHz) + 1);
        // Perfil gaussiano normalizado pela largura: a energia do harmônico independe da largura.
        const double norm = 1.0 / std::sqrt(sigma);
        for (int k = k0; k <= k1; ++k) {
            const double x = (k * binHz - fh) / sigma;
            amp[static_cast<size_t>(k)] += a * std::exp(-x * x) * norm;
        }
    }

    std::vector<std::complex<double>> bins(static_cast<size_t>(n));
    Rng rng(seed);
    for (int k = 1; k < half; ++k) {
        const double ph = rng.uniform() * 2.0 * 3.14159265358979323846;
        const std::complex<double> c = std::polar(amp[static_cast<size_t>(k)], ph);
        bins[static_cast<size_t>(k)] = c;
        bins[static_cast<size_t>(n - k)] = std::conj(c);
    }
    fftInPlace(bins, true);

    out.resize(static_cast<size_t>(n));
    double e = 0.0;
    for (int i = 0; i < n; ++i) {
        const double v = bins[static_cast<size_t>(i)].real();
        out[static_cast<size_t>(i)] = static_cast<float>(v);
        e += v * v;
    }
    const double rms = std::sqrt(e / n);
    const float g = rms > 0.0 ? static_cast<float>(0.1 / rms) : 0.0f;
    for (float& v : out) v *= g;
}

} // namespace pad
