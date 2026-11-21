// ReverbLab — gera respostas ao impulso e medidas de CPU dos três reverbs do motor com os
// mesmos parâmetros, para comparação objetiva (análise em tools/reverblab/analyze_ir.py).
// Uso: pad_reverblab <pasta_saida>
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>
#include "core/WavIO.h"
#include "dsp/ClassicReverb.h"
#include "dsp/CloudReverb.h"
#include "dsp/DattorroPlate.h"
#include "dsp/DspMath.h"

using namespace pad;

static std::unique_ptr<ReverbBase> make(int k) {
    if (k == 0) return std::make_unique<ClassicReverb>();
    if (k == 1) return std::make_unique<DattorroPlate>();
    return std::make_unique<CloudReverb>();
}

int main(int argc, char** argv) {
    ScopedNoDenormals nd;
    const std::string out = argc > 1 ? argv[1] : ".";
    const char* names[3] = {"classico", "plate", "cloud"};
    const float sr = 48000.0f;
    const int bs = 64;
    for (int k = 0; k < 3; ++k) {
        for (float T : {3.0f, 6.0f}) {
            for (float early : {0.0f, 0.4f}) {
                auto r = make(k);
                r->prepare(sr);
                ReverbParams p;
                p.decaySeconds = T;
                p.early = early;
                p.preDelaySeconds = 0.0f;
                p.modulation = 0.0f;   // IR sem modulação: mede a estrutura da cauda
                r->setParams(p);
                const int frames = static_cast<int>(sr * (T * 1.3f + 0.5f));
                std::vector<float> il(bs), ir(bs), ol(bs), orr(bs), wav(static_cast<size_t>(frames) * 2);
                for (int b = 0; b * bs < frames; ++b) {
                    for (int i = 0; i < bs; ++i) il[i] = ir[i] = (b == 0 && i == 0) ? 1.0f : 0.0f;
                    r->process(il.data(), ir.data(), ol.data(), orr.data(), bs);
                    for (int i = 0; i < bs && b * bs + i < frames; ++i) {
                        wav[static_cast<size_t>(b * bs + i) * 2] = ol[i];
                        wav[static_cast<size_t>(b * bs + i) * 2 + 1] = orr[i];
                    }
                }
                char fn[256];
                std::snprintf(fn, sizeof fn, "%s/ir_%s_T%.0f_er%.0f.wav", out.c_str(), names[k], T, early * 10);
                writeWav24(fn, wav, static_cast<int>(sr));
            }
        }
        // CPU: 20 s de ruído rosa aproximado, blocos de 192 (como no aparelho), parâmetros padrão.
        auto r = make(k);
        r->prepare(sr);
        ReverbParams p;
        r->setParams(p);
        std::vector<float> il(192), ir(192), ol(192), orr(192);
        uint32_t s = 1;
        float b0 = 0;
        const auto t0 = std::chrono::steady_clock::now();
        const int blocks = static_cast<int>(sr * 20 / 192);
        for (int b = 0; b < blocks; ++b) {
            for (int i = 0; i < 192; ++i) {
                s = s * 1664525u + 1013904223u;
                b0 = 0.99f * b0 + 0.1f * (static_cast<float>(s >> 8) / 16777216.0f - 0.5f);
                il[i] = b0; ir[i] = -b0;
            }
            r->process(il.data(), ir.data(), ol.data(), orr.data(), 192);
        }
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::printf("cpu %s %.2f\n", names[k], 100.0 * secs / 20.0);
    }
    return 0;
}
