#include "Reverb.h"

namespace pad {

namespace {
// Comprimentos-base (ms) para size = 1. Escolhidos sem razões simples entre si.
constexpr float kLineMs[FdnReverb::kLines] = {37.13f, 43.71f, 50.29f, 57.07f, 64.61f, 72.43f, 83.17f, 97.31f};
constexpr float kDiffMs[4] = {4.771f, 3.595f, 12.73f, 9.307f};
constexpr float kMaxSizeScale = 1.6f;
constexpr float kShimmerFeedback = 0.35f; // +10 dB em 1–6 kHz no máximo, sem acionar o limitador
}

void FdnReverb::prepare(float sampleRate) {
    sr_ = sampleRate;
    Rng rng(0x5eed1234);
    for (int i = 0; i < kLines; ++i) {
        const int maxSamples = static_cast<int>(kLineMs[i] * 0.001f * sr_ * kMaxSizeScale) + 64;
        lines_[i].prepare(maxSamples);
        modPhase_[i] = rng.uniform();
        modInc_[i] = rng.range(0.07f, 0.31f) / sr_;          // 0,07–0,31 Hz
        modDepth_[i] = rng.range(0.35f, 0.9f) * 0.001f * sr_; // 0,35–0,9 ms
    }
    for (int i = 0; i < 4; ++i) {
        diffL_[i].prepare(static_cast<int>(kDiffMs[i] * 0.001f * sr_), 0.62f);
        diffR_[i].prepare(static_cast<int>(kDiffMs[i] * 1.071f * 0.001f * sr_), 0.62f);
    }
    preDelay_ = static_cast<int>(0.022f * sr_);
    preDelayL_.prepare(preDelay_ + 2);
    preDelayR_.prepare(preDelay_ + 2);
    shiftL_.prepare(sr_, 82.0f);
    shiftR_.prepare(sr_, 77.0f); // janelas diferentes descorrelacionam os canais
    shiftL_.setRatio(2.0f);
    shiftR_.setRatio(2.0f);
    shimmerHp_.c.highpass(700.0f, 0.707f, sr_);
    shimmerLp_.c.lowpass(5200.0f, 0.6f, sr_);
    dirty_ = true;
    reset();
}

void FdnReverb::reset() {
    for (auto& l : lines_) l.reset();
    for (auto& d : damp_) d.reset();
    for (auto& d : diffL_) d.reset();
    for (auto& d : diffR_) d.reset();
    preDelayL_.reset(); preDelayR_.reset();
    shiftL_.reset(); shiftR_.reset();
    shimmerHp_.reset(); shimmerLp_.reset();
    fbL_ = fbR_ = 0.0f;
}

void FdnReverb::setParams(float decaySeconds, float size, float damping, float shimmer) {
    decaySeconds = clampf(decaySeconds, 0.3f, 20.0f);
    size = clampf(size, 0.0f, 1.0f);
    damping = clampf(damping, 0.0f, 1.0f);
    if (std::fabs(decaySeconds - decay_) > 1e-3f || std::fabs(size - size_) > 1e-4f || std::fabs(damping - damping_) > 1e-4f) {
        decay_ = decaySeconds; size_ = size; damping_ = damping; dirty_ = true;
    }
    shimmer_ = clampf(shimmer, 0.0f, 1.0f);
    if (dirty_) updateGains();
}

void FdnReverb::updateGains() {
    const float scale = 0.55f + size_ * (kMaxSizeScale - 0.55f);
    // Corte do damping: 12 kHz (claro) até 2,2 kHz (escuro).
    const float dampHz = expMap(1.0f - damping_, 2200.0f, 12000.0f);
    for (int i = 0; i < kLines; ++i) {
        baseDelay_[i] = kLineMs[i] * 0.001f * sr_ * scale;
        // Ganho por volta para atingir -60 dB em decay_ segundos.
        gain_[i] = std::pow(10.0f, -3.0f * baseDelay_[i] / (decay_ * sr_));
        damp_[i].setLowpass(dampHz, sr_);
    }
    dirty_ = false;
}

void FdnReverb::process(float inL, float inR, float& outL, float& outR) {
    // Realimentação do shimmer (amostra anterior) somada à entrada.
    float sL = 0.0f, sR = 0.0f;
    if (shimmer_ > 0.0001f) {
        sL = shiftL_.process(fbL_);
        sR = shiftR_.process(fbR_);
        shimmerHp_.process(sL, sR);
        shimmerLp_.process(sL, sR);
        const float g = shimmer_ * kShimmerFeedback;
        sL *= g; sR *= g;
    }

    preDelayL_.push(inL);
    preDelayR_.push(inR);
    float xL = preDelayL_.readInt(preDelay_) + sL;
    float xR = preDelayR_.readInt(preDelay_) + sR;
    for (int i = 0; i < 4; ++i) { xL = diffL_[i].process(xL); xR = diffR_[i].process(xR); }

    // Leitura das linhas com modulação lenta.
    float y[kLines];
    for (int i = 0; i < kLines; ++i) {
        modPhase_[i] += modInc_[i];
        if (modPhase_[i] >= 1.0f) modPhase_[i] -= 1.0f;
        // triângulo suavizado -> seno aproximado barato
        const float tri = 4.0f * std::fabs(modPhase_[i] - 0.5f) - 1.0f; // [-1,1]
        const float mod = tri * (1.5f - 0.5f * tri * tri);              // forma senoidal
        const float d = baseDelay_[i] + modDepth_[i] * (1.0f + mod);
        y[i] = damp_[i].lp(lines_[i].readFrac(d)) * gain_[i];
    }

    // Matriz de Householder: z = y - (2/N) * soma(y)
    float sum = 0.0f;
    for (int i = 0; i < kLines; ++i) sum += y[i];
    const float h = sum * (2.0f / kLines);

    // Injeção estéreo com sinais alternados (descorrelação L/R).
    for (int i = 0; i < kLines; ++i) {
        const float in = (i & 1) ? xR : xL;
        const float sign = (i & 2) ? -1.0f : 1.0f;
        lines_[i].push(y[i] - h + in * sign * 0.5f);
    }

    const float rawL = y[0] - y[2] + y[4] - y[6] + 0.7f * (y[1] + y[5]);
    const float rawR = y[1] - y[3] + y[5] - y[7] + 0.7f * (y[2] + y[6]);
    outL = rawL * 0.42f;
    outR = rawR * 0.42f;
    // O shimmer realimenta a cauda bruta. Mesmo com ganho de laço > 1 numa faixa, não há
    // instabilidade: cada passagem dobra a frequência e, em no máximo ~3 passagens
    // (700 Hz -> 5,2 kHz), o conteúdo sai da banda do filtro do shimmer.
    fbL_ = rawL;
    fbR_ = rawR;
}

} // namespace pad
