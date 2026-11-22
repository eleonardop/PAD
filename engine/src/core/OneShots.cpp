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

// Guitarra limpa, desenho próprio (Karplus-Strong estendido; nenhuma gravação de terceiros):
//  - excitação: ruído suave passado por um passa-baixa (palheta macia) e por um pente que
//    imita o ponto de ataque a ~1/5 da corda (tira harmônicos múltiplos de 5);
//  - laço da corda com filtro de média (perda natural de agudos) e ganho que dá ~12 s de
//    sustentação no grave e ~6 s no agudo; leitura fracionária para afinar;
//  - compressor de sustentação (3:1), como o pedal usado antes do volume nos swells;
//  - captador do braço: passa-baixa de 2 polos em ~4,8 kHz e um leve realce em ~180 Hz (corpo).
// Uma zona a cada 3 semitons de Mi3 a Mi6, 7 s cada, mono. Nos presets ela aparece quase sempre
// com volume crescendo (swell), então o ataque quase não se ouve.
bool SourceLibrary::buildGuitar(float sr, ZoneBank& out) {
    Rng rng(0x6017A12u);
    for (int note = 52; note <= 88; note += 3) {
        const double f = 440.0 * std::pow(2.0, (note - 69) / 12.0);
        const double seconds = 7.0;
        const int n = static_cast<int>(seconds * sr);
        const double period = sr / f;
        const int len = static_cast<int>(period) + 4;
        // Excitação: um período de ruído filtrado, com o pente do ponto de ataque.
        std::vector<double> exc(static_cast<size_t>(len), 0.0);
        double lp = 0.0;
        const double aExc = std::exp(-6.283185307179586 * 2500.0 / sr);
        const int pickOff = std::max(1, static_cast<int>(period * 0.2));
        for (int i = 0; i < static_cast<int>(period); ++i) {
            lp = (1.0 - aExc) * (rng.uniform() * 2.0 - 1.0) + aExc * lp;
            exc[static_cast<size_t>(i)] = lp;
        }
        for (int i = static_cast<int>(period) - 1; i >= pickOff; --i) exc[static_cast<size_t>(i)] -= 0.8 * exc[static_cast<size_t>(i - pickOff)];
        double mean = 0.0;
        for (int i = 0; i < static_cast<int>(period); ++i) mean += exc[static_cast<size_t>(i)];
        mean /= std::floor(period);
        const double reg = (note - 52) / 36.0;
        const double t60 = 12.0 - 6.0 * reg;
        // Laço: atraso inteiro N + passa-tudo fracionário (Thiran de 1ª ordem, ganho 1) + filtro
        // de um zero (perda de agudos; mais escuro no grave). Ganho por volta < 1 e |H| ≤ 1 em
        // todas as frequências: estável, e a fundamental cai 60 dB em t60.
        const double rho = 0.32 - 0.22 * reg;
        const double om = 6.283185307179586 * f / sr;
        const double hoz = std::sqrt((1 - rho) * (1 - rho) + rho * rho + 2 * (1 - rho) * rho * std::cos(om));
        const double g = std::min(0.9999, std::pow(10.0, -3.0 / (t60 * f)) / hoz);
        int N = static_cast<int>(std::floor(period - rho - 0.1));
        const double dl = period - rho - N;                       // 0,1 .. 1,1
        const double eta = (1.0 - dl) / (1.0 + dl);
        std::vector<double> dline(static_cast<size_t>(N + 2), 0.0);
        std::vector<float> buf(static_cast<size_t>(n));
        int w = 0;
        double apX = 0.0, apY = 0.0, ozPrev = 0.0;
        // Captador: biquads simples (passa-baixa RBJ e bell), calculados aqui mesmo.
        auto rbjLp = [&](double fc, double q, double* c) {
            const double w0 = 6.283185307179586 * fc / sr, al = std::sin(w0) / (2 * q), cw = std::cos(w0);
            const double a0 = 1 + al;
            c[0] = (1 - cw) / 2 / a0; c[1] = (1 - cw) / a0; c[2] = c[0]; c[3] = -2 * cw / a0; c[4] = (1 - al) / a0;
        };
        auto rbjBell = [&](double fc, double q, double db, double* c) {
            const double A = std::pow(10.0, db / 40), w0 = 6.283185307179586 * fc / sr, al = std::sin(w0) / (2 * q), cw = std::cos(w0);
            const double a0 = 1 + al / A;
            c[0] = (1 + al * A) / a0; c[1] = -2 * cw / a0; c[2] = (1 - al * A) / a0; c[3] = -2 * cw / a0; c[4] = (1 - al / A) / a0;
        };
        double c1[5], c2[5], z1[2] = {0, 0}, z2[2] = {0, 0};
        rbjLp(4800.0, 0.6, c1);
        rbjBell(180.0, 0.8, 2.5, c2);
        auto bq = [](const double* c, double* z, double x) {
            const double y = c[0] * x + z[0];
            z[0] = c[1] * x - c[3] * y + z[1];
            z[1] = c[2] * x - c[4] * y;
            return y;
        };
        const int L = N + 2;
        for (int i = 0; i < n; ++i) {
            int rd = w - N;
            if (rd < 0) rd += L;
            const double y = dline[static_cast<size_t>(rd)];
            const double ap = eta * y + apX - eta * apY;
            apX = y; apY = ap;
            const double oz = (1.0 - rho) * ap + rho * ozPrev;
            ozPrev = ap;
            double x = g * oz;
            if (i < static_cast<int>(period)) x += exc[static_cast<size_t>(i)] - mean;
            dline[static_cast<size_t>(w)] = x;
            w = (w + 1) % L;
            const double o = bq(c2, z2, bq(c1, z1, y));
            buf[static_cast<size_t>(i)] = static_cast<float>(o);
        }
        // Compressor de sustentação (como o pedal que todo guitarrista de ambient usa antes do
        // volume): envelope RMS de ~60 ms, razão ~3:1 acima do nível de 1 s depois do ataque.
        // Assim o swell encontra a nota ainda cheia, e não já morrendo.
        {
            std::vector<float> env(static_cast<size_t>(n));
            double e2 = 0.0;
            const double ae = std::exp(-1.0 / (0.06 * sr));
            for (int i = 0; i < n; ++i) {
                e2 = ae * e2 + (1.0 - ae) * static_cast<double>(buf[static_cast<size_t>(i)]) * buf[static_cast<size_t>(i)];
                env[static_cast<size_t>(i)] = static_cast<float>(std::sqrt(e2));
            }
            const double ref = std::max(1e-6, static_cast<double>(env[static_cast<size_t>(std::min(n - 1, static_cast<int>(sr)))]));
            double gs = 1.0;
            const double ag = std::exp(-1.0 / (0.15 * sr));
            for (int i = 0; i < n; ++i) {
                const double lv = std::max(1e-7, static_cast<double>(env[static_cast<size_t>(i)]));
                double g = std::pow(lv / ref, -2.0 / 3.0);          // 3:1 em torno do nível de referência
                g = std::min(g, 8.0);                                // no máximo +18 dB na cauda
                gs = ag * gs + (1.0 - ag) * g;
                double o = buf[static_cast<size_t>(i)] * gs;
                const double t = i / static_cast<double>(sr);
                const double rem = seconds - t;
                if (rem < 0.8) o *= 0.5 - 0.5 * std::cos(3.141592653589793 * rem / 0.8);
                buf[static_cast<size_t>(i)] = static_cast<float>(o);
            }
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
