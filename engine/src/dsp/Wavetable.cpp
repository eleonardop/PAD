#include "Wavetable.h"
#include "Random.h"

namespace pad {

void Wavetable::build(const std::function<float(int)>& amplitude, float sampleRate, uint32_t phaseSeed, bool randomPhases) {
    data_.assign(static_cast<size_t>(kLevels) * (kSize + 3), 0.0f);

    // Fases por harmônico (iguais em todos os níveis, para o timbre não "pular" entre níveis).
    std::vector<double> phases(kMaxHarmonics + 1, 0.0);
    Rng rng(phaseSeed);
    for (int h = 1; h <= kMaxHarmonics; ++h) phases[h] = randomPhases ? rng.uniform() * 2.0 * kPiD : 0.0;

    // Tabela de seno de alta precisão para a soma aditiva.
    std::vector<double> sinTab(kSize);
    for (int i = 0; i < kSize; ++i) sinTab[i] = std::sin(2.0 * kPiD * i / kSize);

    std::vector<double> acc(kSize);
    double norm = 0.0;
    float top = kBaseTopHz;
    for (int level = 0; level < kLevels; ++level, top *= 2.0f) {
        const int maxH = std::min(kMaxHarmonics, static_cast<int>((0.45f * sampleRate) / top));
        std::fill(acc.begin(), acc.end(), 0.0);
        for (int h = 1; h <= std::max(maxH, 1); ++h) {
            const double a = amplitude(h);
            if (a == 0.0) continue;
            const int off = static_cast<int>(phases[h] / (2.0 * kPiD) * kSize);
            for (int i = 0; i < kSize; ++i) acc[i] += a * sinTab[(static_cast<int64_t>(h) * i + off) & kMask];
        }
        if (level == 0) { // normaliza todos os níveis pelo pico do nível mais completo
            double peak = 0.0;
            for (double v : acc) peak = std::max(peak, std::fabs(v));
            norm = peak > 0.0 ? 1.0 / peak : 1.0;
        }
        float* t = data_.data() + static_cast<size_t>(level) * (kSize + 3) + 1;
        for (int i = 0; i < kSize; ++i) t[i] = static_cast<float>(acc[i] * norm);
        t[-1] = t[kSize - 1];
        t[kSize] = t[0];
        t[kSize + 1] = t[1];
    }
}

} // namespace pad
