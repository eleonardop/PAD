#include "DattorroPlate.h"

namespace pad {

namespace {
// Tempos do artigo (amostras a 29.761 Hz)
constexpr float kInDiff[4] = {142.0f, 107.0f, 379.0f, 277.0f};
constexpr float kAp1[2] = {672.0f, 908.0f};
constexpr float kD1[2] = {4453.0f, 4217.0f};
constexpr float kAp2[2] = {1800.0f, 2656.0f};
constexpr float kD2[2] = {3720.0f, 3163.0f};
constexpr float kExcursion = 16.0f;
constexpr float kMaxScale = 1.5f;
}

void DattorroPlate::prepare(float sampleRate) {
    sr_ = sampleRate;
    const float k = sr_ / 29761.0f * kMaxScale;
    for (int i = 0; i < 4; ++i) inDiff_[i].line.prepare(static_cast<int>(kInDiff[i] * k) + 8);
    for (int b = 0; b < 2; ++b) {
        tankAp1_[b].line.prepare(static_cast<int>((kAp1[b] + kExcursion * 2) * k) + 8);
        tankAp2_[b].line.prepare(static_cast<int>((kAp2[b] + kExcursion) * k) + 8);
        tankD1_[b].prepare(static_cast<int>(kD1[b] * k) + 8);
        tankD2_[b].prepare(static_cast<int>(kD2[b] * k) + 8);
        tankAp1_[b].inc = (b == 0 ? 0.53f : 0.71f) / sr_;
        tankAp1_[b].phase = b == 0 ? 0.0f : 0.37f;
        tankAp2_[b].inc = (b == 0 ? 0.29f : 0.37f) / sr_;
        tankAp2_[b].phase = b == 0 ? 0.6f : 0.15f;
    }
    early_.prepare(sr_);
    bandwidth_.setLowpass(9000.0f, sr_);
    dirty_ = true;
    reset();
    updateLengths();
}

void DattorroPlate::reset() {
    for (auto& a : inDiff_) a.line.reset();
    for (int b = 0; b < 2; ++b) {
        tankAp1_[b].line.reset(); tankAp2_[b].line.reset();
        tankD1_[b].reset(); tankD2_[b].reset();
        damp_[b].reset();
        fb_[b] = 0.0f;
    }
    early_.reset();
    bandwidth_.reset();
}

void DattorroPlate::updateLengths() {
    sizeScale_ = 0.55f + 0.95f * clampf(p_.size, 0.0f, 1.0f); // 0,55..1,5
    const float diff = clampf(p_.diffusion, 0.0f, 1.0f);
    for (int i = 0; i < 4; ++i) {
        inDiff_[i].base = scaled(kInDiff[i]);
        inDiff_[i].g = (i < 2 ? 0.75f : 0.625f) * (0.55f + 0.45f * diff);
    }
    for (int b = 0; b < 2; ++b) {
        tankAp1_[b].base = scaled(kAp1[b]);
        tankAp1_[b].depth = kExcursion * (sr_ / 29761.0f) * (0.4f + 1.2f * clampf(p_.modulation, 0.0f, 1.0f));
        tankAp1_[b].g = -0.70f;
        tankAp2_[b].base = scaled(kAp2[b]);
        tankAp2_[b].depth = 0.35f * tankAp1_[b].depth;
        tankAp2_[b].g = 0.50f;
        len1_[b] = scaled(kD1[b]);
        len2_[b] = scaled(kD2[b]);
    }
    // Uma volta completa do tanque passa por 4 multiplicações por decayGain (2 por ramo), então
    // cada uma corresponde a 1/4 do tempo da volta. Ganho calculado para o RT60 pedido.
    const float quarterLoop = (scaled(kAp1[0]) + scaled(kD1[0]) + scaled(kAp2[0]) + scaled(kD2[0]) +
                               scaled(kAp1[1]) + scaled(kD1[1]) + scaled(kAp2[1]) + scaled(kD2[1])) * 0.25f;
    decayGain_ = clampf(std::pow(10.0f, -3.0f * quarterLoop / (std::max(p_.decaySeconds, 0.3f) * sr_)), 0.0f, 0.985f);
    const float dampHz = expMap(1.0f - p_.damping, 1800.0f, 11000.0f);
    damp_[0].setLowpass(dampHz, sr_);
    damp_[1].setLowpass(dampHz, sr_);
    early_.set(p_.preDelaySeconds, p_.size, p_.early);
    erLevel_ = p_.early;
    dirty_ = false;
}

void DattorroPlate::setParams(const ReverbParams& p) {
    if (std::fabs(p.decaySeconds - p_.decaySeconds) > 1e-3f || std::fabs(p.size - p_.size) > 1e-4f ||
        std::fabs(p.damping - p_.damping) > 1e-4f || std::fabs(p.diffusion - p_.diffusion) > 1e-4f ||
        std::fabs(p.preDelaySeconds - p_.preDelaySeconds) > 1e-5f || std::fabs(p.early - p_.early) > 1e-4f ||
        std::fabs(p.modulation - p_.modulation) > 1e-4f) {
        p_ = p;
        dirty_ = true;
    }
    if (dirty_) updateLengths();
}

void DattorroPlate::process(const float* inL, const float* inR, float* outL, float* outR, int n) {
    const float k = sr_ / 29761.0f * sizeScale_;
    // Pontos de saída do artigo (amostras a 29.761 Hz)
    const int a266 = static_cast<int>(266 * k), a2974 = static_cast<int>(2974 * k), a1913 = static_cast<int>(1913 * k);
    const int a1996 = static_cast<int>(1996 * k), a1990 = static_cast<int>(1990 * k), a187 = static_cast<int>(187 * k);
    const int a1066 = static_cast<int>(1066 * k), a353 = static_cast<int>(353 * k), a3627 = static_cast<int>(3627 * k);
    const int a1228 = static_cast<int>(1228 * k), a2673 = static_cast<int>(2673 * k), a2111 = static_cast<int>(2111 * k);
    const int a335 = static_cast<int>(335 * k), a121 = static_cast<int>(121 * k);

    for (int i = 0; i < n; ++i) {
        float pre = 0.0f, erL = 0.0f, erR = 0.0f;
        early_.process(0.5f * (inL[i] + inR[i]), pre, erL, erR);
        float x = bandwidth_.lp(pre);
        for (auto& d : inDiff_) x = d.process(x, false);

        // Ramo 0 recebe a realimentação do fim do ramo 1, e vice-versa.
        float y[2];
        for (int b = 0; b < 2; ++b) {
            float v = x + fb_[1 - b];
            v = tankAp1_[b].process(v, true);
            tankD1_[b].push(v);
            float w = tankD1_[b].readFrac(std::max(2.0f, len1_[b]));
            w = damp_[b].lp(w) * decayGain_;
            w = tankAp2_[b].process(w, true);
            tankD2_[b].push(w);
            y[b] = tankD2_[b].readFrac(std::max(2.0f, len2_[b])) * decayGain_;
        }
        fb_[0] = y[0];
        fb_[1] = y[1];

        // Saídas (tabela 2 do artigo). Ramo 0 = "esquerdo" (24-39), ramo 1 = "direito" (48-63).
        const float l = tankD1_[1].readInt(a266) + tankD1_[1].readInt(a2974) - tankAp2_[1].tap(a1913) + tankD2_[1].readInt(a1996) -
                        tankD1_[0].readInt(a1990) - tankAp2_[0].tap(a187) - tankD2_[0].readInt(a1066);
        const float r = tankD1_[0].readInt(a353) + tankD1_[0].readInt(a3627) - tankAp2_[0].tap(a1228) + tankD2_[0].readInt(a2673) -
                        tankD1_[1].readInt(a2111) - tankAp2_[1].tap(a335) - tankD2_[1].readInt(a121);
        outL[i] = 0.45f * l + erL;
        outR[i] = 0.45f * r + erR;
    }
}

} // namespace pad
