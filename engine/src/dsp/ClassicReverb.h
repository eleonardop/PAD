// PAD Engine — o reverb FDN da versão 0.1 ("Clássico"), na interface comum dos reverbs.
// Mantido como opção e para comparação. O shimmer agora é externo (comum aos três reverbs),
// então o shimmer interno do FDN fica desligado.
#pragma once

#include "ReverbBase.h"
#include "Reverb.h"

namespace pad {

class ClassicReverb : public ReverbBase {
public:
    void prepare(float sampleRate) override { fdn_.prepare(sampleRate); }
    void reset() override { fdn_.reset(); }
    void setParams(const ReverbParams& p) override { fdn_.setParams(p.decaySeconds, p.size, p.damping, 0.0f); }
    void process(const float* inL, const float* inR, float* outL, float* outR, int n) override {
        for (int i = 0; i < n; ++i) fdn_.process(inL[i], inR[i], outL[i], outR[i]);
    }
private:
    FdnReverb fdn_;
};

} // namespace pad
