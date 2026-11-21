// PAD Engine — limitador de pico com lookahead (proteção final de headroom).
//
// Algoritmo: ganho necessário por amostra -> mínimo deslizante sobre a janela de
// lookahead -> média móvel (boxcar) do mesmo tamanho. A média do mínimo garante que,
// quando o pico sai do atraso, o ganho aplicado já é <= ao necessário (nenhum overshoot)
// e que a redução chega em rampa suave (sem distorção de "degrau"). O release é
// exponencial e só desacelera subidas, então nunca deixa o ganho acima do seguro.
// Num pad bem ajustado o limitador quase não atua; ele existe para casos extremos
// (muitas camadas no máximo, shimmer longo, trocas rápidas).
#pragma once

#include <vector>
#include "DspMath.h"

namespace pad {

class LookaheadLimiter {
public:
    void prepare(float sampleRate, float lookaheadMs = 3.0f, float releaseMs = 180.0f) {
        la_ = std::max(8, static_cast<int>(lookaheadMs * 0.001f * sampleRate));
        delayL_.assign(static_cast<size_t>(la_), 0.0f);
        delayR_.assign(static_cast<size_t>(la_), 0.0f);
        box_.assign(static_cast<size_t>(la_), 1.0f);
        dqVal_.assign(static_cast<size_t>(la_ + 2), 1.0f);
        dqIdx_.assign(static_cast<size_t>(la_ + 2), 0);
        relCoef_ = std::exp(-1.0f / (releaseMs * 0.001f * sampleRate));
        reset();
    }
    void setThreshold(float linear) { thr_ = linear; }
    void reset() {
        std::fill(delayL_.begin(), delayL_.end(), 0.0f);
        std::fill(delayR_.begin(), delayR_.end(), 0.0f);
        std::fill(box_.begin(), box_.end(), 1.0f);
        boxSum_ = static_cast<double>(la_);
        pos_ = 0; n_ = 0; head_ = 0; tail_ = 0; gain_ = 1.0f; minGain_ = 1.0f;
    }

    inline void process(float& l, float& r) {
        const float peak = std::max(std::fabs(l), std::fabs(r));
        const float need = peak > thr_ ? thr_ / peak : 1.0f;

        // Mínimo deslizante (deque monotônica em buffer circular pré-alocado).
        const int cap = la_ + 2;
        while (head_ != tail_) {
            const int last = (tail_ - 1 + cap) % cap;
            if (dqVal_[static_cast<size_t>(last)] >= need) tail_ = last; else break;
        }
        dqVal_[static_cast<size_t>(tail_)] = need;
        dqIdx_[static_cast<size_t>(tail_)] = n_;
        tail_ = (tail_ + 1) % cap;
        // janela de la_+1 amostras: cobre exatamente o atraso de la_ amostras do áudio
        while (dqIdx_[static_cast<size_t>(head_)] <= n_ - (la_ + 1)) head_ = (head_ + 1) % cap;
        const float windowMin = dqVal_[static_cast<size_t>(head_)];
        ++n_;

        // Média móvel do mínimo.
        boxSum_ += static_cast<double>(windowMin) - box_[static_cast<size_t>(pos_)];
        box_[static_cast<size_t>(pos_)] = windowMin;
        const float target = static_cast<float>(boxSum_ / la_);

        // Release: descidas imediatas, subidas exponenciais.
        gain_ = target < gain_ ? target : target + relCoef_ * (gain_ - target);

        // Atraso do áudio = lookahead.
        const float dl = delayL_[static_cast<size_t>(pos_)];
        const float dr = delayR_[static_cast<size_t>(pos_)];
        delayL_[static_cast<size_t>(pos_)] = l;
        delayR_[static_cast<size_t>(pos_)] = r;
        pos_ = (pos_ + 1) % la_;
        minGain_ = std::min(minGain_, gain_);

        l = clampf(dl * gain_, -1.0f, 1.0f);
        r = clampf(dr * gain_, -1.0f, 1.0f);
    }

    int latency() const { return la_; }
    float currentGain() const { return gain_; }
    // Menor ganho desde a última leitura (telemetria para testes).
    float takeMinGain() { const float g = minGain_; minGain_ = 1.0f; return g; }

private:
    int la_ = 144;
    std::vector<float> delayL_, delayR_, box_, dqVal_;
    std::vector<int64_t> dqIdx_;
    double boxSum_ = 0.0;
    int pos_ = 0, head_ = 0, tail_ = 0;
    int64_t n_ = 0;
    float thr_ = 0.89f, gain_ = 1.0f, relCoef_ = 0.999f, minGain_ = 1.0f;
};

} // namespace pad
