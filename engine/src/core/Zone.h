// PAD Engine — zona de áudio em loop (tabela PADsynth ou sample real).
//
// Armazenamento em int16 com fator de escala: metade da memória do float e ruído de
// quantização ~96 dB abaixo do pico (inaudível num pad a -20 dBFS RMS). A conversão
// para float custa uma multiplicação por amostra lida.
// O buffer inteiro é o loop; há amostras de guarda para a interpolação Hermite:
// [ultima] + loop + [0, 1, 2].
#pragma once

#include <cstdint>
#include <vector>
#include "../dsp/DspMath.h"

namespace pad {

struct Zone {
    int rootNote = 60;
    float rootHz = 261.63f;
    float sampleRate = 48000.0f;
    int length = 0;          // amostras no loop
    int channels = 1;        // 1 = mono (PADsynth: o canal direito é lido em outra posição)
    float scale = 1.0f / 32767.0f;
    std::vector<int16_t> l, r;

    // Constrói a partir de float (normaliza para o pico e cria as guardas).
    void setFromFloat(const float* left, const float* right, int n) {
        float peak = 1e-9f;
        for (int i = 0; i < n; ++i) {
            peak = std::max(peak, std::fabs(left[i]));
            if (right) peak = std::max(peak, std::fabs(right[i]));
        }
        scale = peak / 32767.0f;
        const float inv = 32767.0f / peak;
        auto fill = [&](std::vector<int16_t>& dst, const float* src) {
            dst.assign(static_cast<size_t>(n) + 4, 0);
            for (int i = 0; i < n; ++i) dst[static_cast<size_t>(i + 1)] = static_cast<int16_t>(std::lrint(src[i] * inv));
            dst[0] = dst[static_cast<size_t>(n)];
            for (int k = 0; k < 3; ++k) dst[static_cast<size_t>(n + 1 + k)] = dst[static_cast<size_t>(1 + (k % n))];
        };
        length = n;
        channels = right ? 2 : 1;
        fill(l, left);
        if (right) fill(r, right); else r.clear();
    }

    // Constrói a partir de int16 intercalado (decodificação FLAC), sem reescalar.
    void setFromInterleaved16(const int16_t* pcm, int frames, int ch) {
        length = frames;
        channels = ch >= 2 ? 2 : 1;
        scale = 1.0f / 32767.0f;
        l.assign(static_cast<size_t>(frames) + 4, 0);
        if (channels == 2) r.assign(static_cast<size_t>(frames) + 4, 0); else r.clear();
        for (int i = 0; i < frames; ++i) {
            l[static_cast<size_t>(i + 1)] = pcm[static_cast<size_t>(i) * static_cast<size_t>(ch)];
            if (channels == 2) r[static_cast<size_t>(i + 1)] = pcm[static_cast<size_t>(i) * static_cast<size_t>(ch) + 1];
        }
        auto guards = [&](std::vector<int16_t>& d) {
            d[0] = d[static_cast<size_t>(frames)];
            for (int k = 0; k < 3; ++k) d[static_cast<size_t>(frames + 1 + k)] = d[static_cast<size_t>(1 + (k % frames))];
        };
        guards(l);
        if (channels == 2) guards(r);
    }

    static inline float hermite16(const int16_t* p, float f) {
        return hermite4(static_cast<float>(p[-1]), static_cast<float>(p[0]), static_cast<float>(p[1]), static_cast<float>(p[2]), f);
    }
    // pos em [0, length)
    inline float readL(double pos) const {
        const int i = static_cast<int>(pos);
        return hermite16(l.data() + 1 + i, static_cast<float>(pos - i)) * scale;
    }
    inline float readR(double pos) const {
        const int i = static_cast<int>(pos);
        return hermite16((channels == 2 ? r.data() : l.data()) + 1 + i, static_cast<float>(pos - i)) * scale;
    }
    size_t bytes() const { return (l.size() + r.size()) * sizeof(int16_t); }
};

// Conjunto de zonas ordenadas pela nota raiz.
class ZoneBank {
public:
    void add(Zone&& z);
    // Zona mais próxima (em semitons) e razão de leitura para soar em targetHz.
    const Zone* nearest(float targetHz, float outputRate, double& ratioOut) const;
    // Mesma zona por índice (para tabelas A/B de morph que compartilham as raízes).
    const Zone* zoneAt(size_t i) const { return i < zones_.size() ? &zones_[i] : nullptr; }
    size_t indexOf(const Zone* z) const { return z ? static_cast<size_t>(z - zones_.data()) : 0; }
    size_t size() const { return zones_.size(); }
    bool empty() const { return zones_.empty(); }
    size_t bytes() const;
    void clear() { zones_.clear(); }

private:
    std::vector<Zone> zones_;
};

} // namespace pad
