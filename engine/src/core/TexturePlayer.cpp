#include "TexturePlayer.h"
#include <cmath>
#include <cstring>

namespace pad {

void TexturePlayer::prepare(float sampleRate) {
    sr_ = sampleRate;
    for (auto& v : v_) v = V{};
}

int TexturePlayer::activeCount() const {
    int n = 0;
    for (const auto& v : v_) n += v.stage != Stage::Free;
    return n;
}

void TexturePlayer::countSourceUsage(uint16_t* counts) const {
    for (const auto& v : v_)
        if (v.stage != Stage::Free) ++counts[static_cast<int>(v.source)];
}

void TexturePlayer::startRelease(V& v, float seconds) {
    if (v.stage == Stage::Free) return;
    if (v.stage == Stage::Pending) { v.stage = Stage::Free; return; } // nem começou
    v.stage = Stage::Release;
    // Exponencial até −60 dB em `seconds`.
    v.relMul = std::exp(-6.9078f / std::max(0.005f, seconds) / sr_);
}

void TexturePlayer::releaseGroup(uint8_t group, float seconds) {
    for (auto& v : v_) if (v.stage != Stage::Free && v.stage != Stage::Release && v.group == group) startRelease(v, seconds);
}

void TexturePlayer::releaseAll(float seconds) {
    for (auto& v : v_) if (v.stage != Stage::Free && v.stage != Stage::Release) startRelease(v, seconds);
}

void TexturePlayer::panic() { releaseAll(0.05f); }

int TexturePlayer::allocate(uint8_t priority) {
    int active = 0;
    for (int i = 0; i < kVoices + kReserve; ++i) {
        if (v_[static_cast<size_t>(i)].stage == Stage::Free) continue;
        // Vozes em release já roubadas contam só até sumirem.
        ++active;
    }
    int freeIdx = -1;
    for (int i = 0; i < kVoices + kReserve; ++i)
        if (v_[static_cast<size_t>(i)].stage == Stage::Free) { freeIdx = i; break; }
    if (active < kVoices && freeIdx >= 0) return freeIdx;
    // Roubo: menor prioridade e, entre iguais, menor ganho e mais antiga.
    int victim = -1;
    float best = 1e9f;
    for (int i = 0; i < kVoices + kReserve; ++i) {
        const V& v = v_[static_cast<size_t>(i)];
        if (v.stage == Stage::Free) continue;
        const float score = v.priority * 10.0f + v.env * v.level + (v.stage == Stage::Release ? -5.0f : 0.0f);
        if (v.priority > priority && v.stage != Stage::Release) continue;
        if (score < best) { best = score; victim = i; }
    }
    if (victim < 0) return -1;
    startRelease(v_[static_cast<size_t>(victim)], 0.03f);
    ++steals_;
    return freeIdx; // pode ser -1 se nem a reserva tiver vaga: o evento é descartado
}

bool TexturePlayer::trigger(const TexEvent& e, const SourceLibrary& lib) {
    const ZoneBank* bank = lib.get(e.source);
    if (!bank || bank->empty()) return false;
    const Zone* z = nullptr;
    double ratio = 1.0;
    if (e.kind) {
        z = bank->kindAt(e.kind, e.kindIndex);
        if (!z) return false;
        ratio = z->sampleRate / sr_;
    } else {
        z = bank->nearest(static_cast<float>(440.0 * std::pow(2.0, (e.midi - 69) / 12.0)), sr_, ratio);
        if (!z) return false;
    }
    const int idx = allocate(e.priority);
    if (idx < 0) return false;
    V& v = v_[static_cast<size_t>(idx)];
    v = V{};
    v.delayLeft = static_cast<int>(std::max(0.0f, e.delay) * sr_);
    v.stage = v.delayLeft > 0 ? Stage::Pending : Stage::Attack;
    v.source = e.source;
    v.zone = z;
    v.loop = !z->oneShot;
    v.len = static_cast<uint64_t>(z->length) << 32;
    v.inc = static_cast<uint64_t>(ratio * 4294967296.0);
    const double off = std::max(0.0, static_cast<double>(e.startOffset)) * z->sampleRate;
    v.pos = static_cast<uint64_t>(std::min(off, static_cast<double>(z->length - 1)) * 4294967296.0);
    v.level = e.level;
    const float th = (clampf(e.pan, -1.0f, 1.0f) + 1.0f) * 0.25f * kPi;
    v.gl = std::cos(th) * 1.41421356f;
    v.gr = std::sin(th) * 1.41421356f;
    v.attackStep = 1.0f / std::max(1.0f, e.attack * sr_);
    v.curve = std::max(0.5f, e.attackCurve);
    v.holdLeft = e.hold < 0.0f ? -1 : static_cast<int>(e.hold * sr_);
    v.priority = e.priority;
    v.group = e.group;
    v.bus = e.bus;
    v.lp.c.lowpass(clampf(e.lowpassHz, 200.0f, 20000.0f), 0.707f, sr_);
    v.hp.c.highpass(clampf(e.highpassHz, 10.0f, 4000.0f), 0.707f, sr_);
    v.age = ++ageCounter_;
    // Release do evento guardado no relMul só quando o release começar (ver render).
    v.relMul = std::exp(-6.9078f / std::max(0.005f, e.release) / sr_);
    return true;
}

void TexturePlayer::render(float* tL, float* tR, float* mL, float* mR, int n) {
    for (auto& v : v_) {
        if (v.stage == Stage::Free) continue;
        int i0 = 0;
        if (v.stage == Stage::Pending) {
            if (v.delayLeft >= n) { v.delayLeft -= n; continue; }
            i0 = v.delayLeft;
            v.delayLeft = 0;
            v.stage = Stage::Attack;
        }
        const Zone& z = *v.zone;
        const int16_t* pl = z.l.data() + 1;
        const int16_t* pr = (z.channels == 2 ? z.r.data() : z.l.data()) + 1;
        const float sc = z.scale * v.level;
        float* oL = v.bus == TexBus::Tonal ? tL : mL;
        float* oR = v.bus == TexBus::Tonal ? tR : mR;
        const float relMulRelease = v.relMul;
        for (int i = i0; i < n; ++i) {
            // Envelope por amostra (barato e sem degraus).
            switch (v.stage) {
                case Stage::Attack:
                    v.t += v.attackStep;
                    if (v.t >= 1.0f) { v.t = 1.0f; v.stage = Stage::Hold; }
                    v.env = v.curve == 1.0f ? v.t : std::pow(v.t, v.curve);
                    break;
                case Stage::Hold:
                    v.env = 1.0f;
                    if (v.holdLeft > 0 && --v.holdLeft == 0) v.stage = Stage::Release;
                    break;
                case Stage::Release:
                    v.env *= relMulRelease;
                    break;
                default: break;
            }
            const uint32_t ip = static_cast<uint32_t>(v.pos >> 32);
            const float fr = static_cast<float>(static_cast<uint32_t>(v.pos)) * (1.0f / 4294967296.0f);
            float l = Zone::hermite16(pl + ip, fr) * sc * v.env;
            float r = Zone::hermite16(pr + ip, fr) * sc * v.env;
            v.hp.process(l, r);
            v.lp.process(l, r);
            oL[i] += l * v.gl;
            oR[i] += r * v.gr;
            v.pos += v.inc;
            if (v.pos >= v.len) {
                if (v.loop) v.pos -= v.len;
                else { v.stage = Stage::Free; break; }
            }
            if (v.stage == Stage::Release && v.env < 1e-4f) { v.stage = Stage::Free; break; }
        }
    }
}

} // namespace pad
