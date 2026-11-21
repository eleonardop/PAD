#include "CloudReverb.h"
#include "DspMath.h"
#include "../../third_party/cloudseed/DSP/ReverbController.h"

namespace pad {

namespace P = Cloudseed::Parameter;

namespace {
// Inversas das curvas de escala do Cloud Seed (Parameters.h / Utils.h), para podermos
// pedir valores em unidades físicas.
double invResp(double y, double decades) { // Resp1dec/2dec/3dec: (10^(d*x) - 1)/(10^d - 1)
    const double m = std::pow(10.0, decades) - 1.0;
    return clampf(static_cast<float>(std::log10(1.0 + y * m) / decades), 0.0f, 1.0f);
}
double invOct(double y, double octaves) { // RespNoct: (2^(n*x) - 1)/(2^n - 1)
    const double m = std::pow(2.0, octaves) - 1.0;
    return clampf(static_cast<float>(std::log2(1.0 + y * m) / octaves), 0.0f, 1.0f);
}
}

CloudReverb::CloudReverb() = default;
CloudReverb::~CloudReverb() = default;

void CloudReverb::apply(int id, double v) { rc_->SetParameter(id, v); }

void CloudReverb::prepare(float sampleRate) {
    rc_ = std::make_unique<Cloudseed::ReverbController>(static_cast<int>(sampleRate));
    // Programa base fixo
    apply(P::Interpolation, 1.0);
    apply(P::LowCutEnabled, 1.0);
    apply(P::LowCut, invOct((140.0 - 20.0) / 980.0, 4));      // corta graves embolados na cauda
    apply(P::HighCutEnabled, 1.0);
    apply(P::InputMix, 0.25);
    apply(P::DryOut, 0.0);                                       // só molhado
    apply(P::TapEnabled, 1.0);
    apply(P::TapCount, 0.12);                                    // ~31 reflexões
    apply(P::TapDecay, 0.85);
    apply(P::EarlyDiffuseEnabled, 1.0);
    apply(P::EarlyDiffuseCount, 0.45);
    apply(P::EarlyDiffuseDelay, 0.35);
    apply(P::EarlyDiffuseModRate, 0.30);
    apply(P::LateMode, 1.0);
    apply(P::LateLineCount, 0.65);                               // 8 linhas
    apply(P::LateDiffuseEnabled, 1.0);
    apply(P::LateDiffuseCount, 0.55);
    apply(P::LateDiffuseDelay, 0.30);
    apply(P::LateDiffuseFeedback, 0.80);
    apply(P::LateDiffuseModRate, 0.20);
    apply(P::LateLineModRate, 0.22);
    apply(P::EqLowShelfEnabled, 1.0);
    apply(P::EqLowFreq, invOct((220.0 - 20.0) / 980.0, 3));
    apply(P::EqLowGain, (-3.0 + 20.0) / 20.0);                  // graves decaem mais rápido
    apply(P::EqHighShelfEnabled, 1.0);
    apply(P::EqHighFreq, invOct((3000.0 - 400.0) / 19600.0, 4));
    apply(P::EqLowpassEnabled, 1.0);
    apply(P::EqCrossSeed, 0.6);                                  // L e R descorrelacionados
    apply(P::SeedTap, 0.334);
    apply(P::SeedDiffusion, 0.185);
    apply(P::SeedDelay, 0.218);
    apply(P::SeedPostDiffusion, 0.365);
    first_ = true;
    setParams(p_);
    rc_->ClearBuffers();
}

void CloudReverb::reset() { if (rc_) rc_->ClearBuffers(); }

void CloudReverb::setParams(const ReverbParams& p) {
    if (!rc_) return;
    auto changed = [&](float a, float b, float eps) { return first_ || std::fabs(a - b) > eps; };
    if (changed(p.decaySeconds, p_.decaySeconds, 0.02f))
    {
        // Calibração medida (tools/reverblab, banda de 1 kHz): os difusores dentro das linhas
        // alongam a cauda; o RT60 real ficava ≈ 1,12·T + 0,9 s. Aqui inverte-se essa relação.
        const double want = clampf(p.decaySeconds, 0.3f, 20.0f);
        const double line = std::max(0.3, (want - 0.9) / 1.12);
        apply(P::LateLineDecay, invResp((line - 0.05) / 59.95, 3));
    }
    if (changed(p.size, p_.size, 0.01f))
        apply(P::LateLineSize, invResp((40.0 + 220.0 * p.size - 20.0) / 980.0, 2)); // 40–260 ms
    if (changed(p.damping, p_.damping, 0.005f)) {
        const double cutoff = expMap(1.0f - p.damping, 2200.0f, 12000.0f);
        apply(P::EqCutoff, invOct((cutoff - 400.0) / 19600.0, 4));
        apply(P::HighCut, invOct((expMap(1.0f - p.damping, 4500.0f, 16000.0f) - 400.0) / 19600.0, 4));
        apply(P::EqHighGain, (-2.0 - 6.0 * p.damping + 20.0) / 20.0);
    }
    if (changed(p.diffusion, p_.diffusion, 0.005f))
        apply(P::EarlyDiffuseFeedback, 0.45 + 0.35 * clampf(p.diffusion, 0.0f, 1.0f));
    if (changed(p.preDelaySeconds, p_.preDelaySeconds, 0.0005f))
        apply(P::TapPredelay, invResp(clampf(p.preDelaySeconds, 0.0f, 0.2f) * 1000.0 / 500.0, 1));
    if (changed(p.early, p_.early, 0.005f)) {
        apply(P::EarlyOut, p.early <= 0.01f ? 0.0 : (20.0 * std::log10(p.early * 0.7) + 30.0) / 30.0);
        apply(P::TapLength, (40.0 + 120.0 * p.size - 10.0) / 990.0);
    }
    if (changed(p.modulation, p_.modulation, 0.01f)) {
        apply(P::LateLineModAmount, 0.10 + 0.25 * p.modulation);
        apply(P::LateDiffuseModAmount, 0.08 + 0.20 * p.modulation);
        apply(P::EarlyDiffuseModAmount, 0.10 + 0.20 * p.modulation);
    }
    apply(P::LateOut, (-4.0 + 30.0) / 30.0);
    p_ = p;
    first_ = false;
}

void CloudReverb::process(const float* inL, const float* inR, float* outL, float* outR, int n) {
    // A API do Cloud Seed não marca a entrada como const, mas não a modifica.
    rc_->Process(const_cast<float*>(inL), const_cast<float*>(inR), outL, outR, n);
}

} // namespace pad
