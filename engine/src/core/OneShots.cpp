// PAD Engine — fontes one-shot geradas no carregamento (v0.3): EP por FM e reverses.
// Roda só no thread carregador (aloca memória à vontade); o callback nunca chama nada daqui.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include "Sources.h"
#include "../dsp/Random.h"

namespace pad {

int ZoneBank::countKind(const char* kind) const {
    int n = 0;
    for (const Zone& z : zones_) if (std::strcmp(z.kind, kind) == 0) ++n;
    return n;
}

const Zone* ZoneBank::kindAt(const char* kind, int n) const {
    for (const Zone& z : zones_) {
        if (std::strcmp(z.kind, kind) != 0) continue;
        if (n-- == 0) return &z;
    }
    return nullptr;
}

// Piano elétrico por FM, desenho próprio (dois pares de operadores, como nos EPs FM clássicos):
//  - corpo: portadora 1:1 modulada por 1:1, índice que cai de ~1,4 para 0,25 (o ataque é mais
//    rico e o som "assenta");
//  - tine: portadora 1:1 modulada por 14:1 com índice e nível de decaimento muito rápido — o
//    "plim" metálico do ataque;
//  - um pouco de 2º harmônico no corpo e um passa-baixa de 1 polo em 5,5 kHz.
// Notas mais agudas decaem mais rápido (como num EP de verdade). Uma zona a cada 3 semitons
// de Mi3 a Mi6, 5 s cada, mono (o pan é dado por evento no TexturePlayer).
bool SourceLibrary::buildEp(float sr, ZoneBank& out) {
    const double twoPi = 6.283185307179586;
    for (int note = 52; note <= 88; note += 3) {
        const double f = 440.0 * std::pow(2.0, (note - 69) / 12.0);
        const double seconds = 5.0;
        const int n = static_cast<int>(seconds * sr);
        std::vector<float> buf(static_cast<size_t>(n));
        const double reg = (note - 52) / 36.0;                 // 0 (grave) .. 1 (agudo)
        const double tauBody = 2.6 - 1.5 * reg;                // decaimento do corpo (s)
        const double tauIdx = 0.9 - 0.4 * reg;                 // o índice cai mais rápido
        double pc = 0, pm = 0, pt = 0, lp = 0;
        const double a = std::exp(-twoPi * 5500.0 / sr);
        for (int i = 0; i < n; ++i) {
            const double t = i / static_cast<double>(sr);
            const double att = std::min(1.0, t / 0.002);
            const double ampBody = att * std::exp(-t / tauBody);
            const double idxBody = 0.25 + 1.15 * std::exp(-t / tauIdx);
            const double ampTine = att * std::exp(-t / 0.18);
            const double idxTine = 0.9 * std::exp(-t / 0.035);
            pc += twoPi * f / sr;
            pm += twoPi * f / sr;
            pt += twoPi * 14.0 * f / sr;
            const double body = std::sin(pc + idxBody * std::sin(pm)) + 0.12 * std::sin(2.0 * pc);
            const double tine = std::sin(pc + idxTine * std::sin(pt));
            double y = ampBody * body + 0.22 * ampTine * tine;
            // Fade final de 0,5 s (one-shot termina em silêncio).
            const double rem = seconds - t;
            if (rem < 0.5) y *= 0.5 - 0.5 * std::cos(3.141592653589793 * rem / 0.5);
            lp = (1.0 - a) * y + a * lp;
            buf[static_cast<size_t>(i)] = static_cast<float>(lp);
        }
        Zone z;
        z.rootNote = note;
        z.rootHz = static_cast<float>(f);
        z.sampleRate = sr;
        z.setFromFloat(buf.data(), nullptr, n);
        z.makeOneShotGuards();
        z.computePeak();
        out.add(std::move(z));
    }
    return !out.empty();
}

void SourceLibrary::buildReversed(const ZoneBank& in, ZoneBank& out, double seconds, const char* kind) {
    for (size_t k = 0; k < in.size(); ++k) {
        const Zone& src = *in.zoneAt(k);
        if (kind && std::strcmp(src.kind, kind) != 0) continue;
        const int n = std::min(src.length, static_cast<int>(seconds * src.sampleRate));
        if (n < 64) continue;
        std::vector<float> l(static_cast<size_t>(n)), r(static_cast<size_t>(n));
        // Fade de entrada de 0,3 s (a cauda cortada não começa do silêncio) e de saída de 25 ms
        // depois do ataque invertido, para o fim não estalar.
        const int fin = std::min(n / 2, static_cast<int>(0.3 * src.sampleRate));
        const int fout = static_cast<int>(0.025 * src.sampleRate);
        for (int i = 0; i < n; ++i) {
            const int j = n - 1 - i;
            float g = 1.0f;
            if (i < fin) g *= 0.5f - 0.5f * std::cos(3.14159265f * i / fin);
            if (n - 1 - i < fout) g *= 0.5f - 0.5f * std::cos(3.14159265f * (n - 1 - i) / fout);
            l[static_cast<size_t>(i)] = src.l[static_cast<size_t>(j + 1)] * src.scale * g;
            r[static_cast<size_t>(i)] = (src.channels == 2 ? src.r[static_cast<size_t>(j + 1)] : src.l[static_cast<size_t>(j + 1)]) * src.scale * g;
        }
        Zone z;
        z.rootNote = src.rootNote;
        z.rootHz = src.rootHz;
        z.sampleRate = src.sampleRate;
        z.setFromFloat(l.data(), src.channels == 2 ? r.data() : nullptr, n);
        z.makeOneShotGuards();
        std::snprintf(z.kind, sizeof z.kind, "%s", kind ? "rev" : "");
        z.peakFrame = n - 1 - fout;
        out.add(std::move(z));
    }
}

} // namespace pad
