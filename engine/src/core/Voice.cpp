#include "Voice.h"

namespace pad {

namespace {
inline float perSample(float seconds, float sr) { return 1.0f / std::max(seconds * sr, 1.0f); }
}

void Voice::prepare(float sampleRate, uint32_t index) {
    sr_ = sampleRate;
    index_ = index;
    env_.setSampleRate(sampleRate);
    noise_.setSeed(0xA11CE000ULL + index * 7919ULL);
    free();
}

void Voice::free() {
    state_ = State::Free;
    zone_ = nullptr;
    lastGain_ = 0.0f;
    lastLowGain_ = 0.0f;
    env_.reset();
}

void Voice::start(int note, bool isRoot, int lowNote, float noteGain, bool useAttack, float attackSec, float fadeSec,
                  uint64_t age, const RenderContext& ctx) {
    note_ = note;
    f0_ = static_cast<float>(midiToHz(note));
    age_ = age;
    state_ = State::Active;
    noteGain_ = noteGainTarget_ = noteGain;
    stealGain_ = 1.0f;
    stealRate_ = 0.0f;
    lastGain_ = 0.0f;

    env_.reset();
    if (useAttack) {
        env_.startAttack(attackSec);
        fadePos_ = 1.0f;
        fadeRate_ = 0.0f;
    } else {
        env_.startSustain();
        fadePos_ = 0.0f;
        fadeRate_ = perSample(fadeSec, sr_);
    }

    // Camada Low
    isRoot_ = isRoot;
    lowNote_ = lowNote;
    lowF0_ = static_cast<float>(midiToHz(lowNote));
    lowPos_ = isRoot ? 1.0f : 0.0f;
    lowRate_ = 0.0f;
    lastLowGain_ = eqPower(lowPos_) * ctx.layerLow;
    lowOsc_.phase = noise_.uniform();
    if (ctx.tables) lowLevel_ = ctx.tables->low.levelFor(lowF0_);

    // Osciladores com fase aleatória: notas repetidas nunca soam idênticas.
    for (auto& o : warm_) o.phase = noise_.uniform();
    for (auto& o : shim_) o.phase = noise_.uniform();
    if (ctx.tables) {
        warmLevel_ = ctx.tables->warmA.levelFor(f0_ * 1.02f);
        shimLevel_[0] = ctx.tables->shimmer.levelFor(f0_ * 2.0f);
        shimLevel_[1] = ctx.tables->shimmer.levelFor(f0_ * 3.0f);
    }
    warmFilter_.reset();
    lastCutoff_ = -1.0f;

    // Strings: zona mais próxima; os dois canais leem pontos distantes do loop
    // (estéreo descorrelacionado a partir do mesmo material).
    zone_ = ctx.bank ? ctx.bank->zoneFor(f0_, ctx.sampleRate, sRatio_) : nullptr;
    if (zone_) {
        sPosL_ = noise_.uniform() * zone_->length;
        sPosR_ = sPosL_ + zone_->length * 0.5;
        if (sPosR_ >= zone_->length) sPosR_ -= zone_->length;
    }

    for (int i = 0; i < 2; ++i) { airL_[i].reset(); airR_[i].reset(); }
    airCenterKey_ = -1.0f;

    // Moduladores com sementes diferentes a cada nota.
    const uint64_t seed = (static_cast<uint64_t>(index_) << 32) ^ (age * 0x9E3779B97F4A7C15ULL) ^ static_cast<uint64_t>(note);
    modPitch_.init(seed + 1, 0.11f + 0.09f * noise_.uniform(), ctx.controlRate);
    modAmp_.init(seed + 2, 0.07f + 0.08f * noise_.uniform(), ctx.controlRate);
    modCut_.init(seed + 3, 0.05f + 0.07f * noise_.uniform(), ctx.controlRate);
    modPan_.init(seed + 4, 0.04f + 0.05f * noise_.uniform(), ctx.controlRate);
    modAir_.init(seed + 5, 0.15f + 0.20f * noise_.uniform(), ctx.controlRate);

    // Pan determinístico por nota (a mesma nota mantém o lugar no panorama).
    const float pan = 0.32f * std::sin(static_cast<float>(note) * 2.399f); // ângulo áureo
    panL_ = eqPower(0.5f * (1.0f - pan)) * 1.41421356f;
    panR_ = eqPower(0.5f * (1.0f + pan)) * 1.41421356f;
}

void Voice::fadeOut(float seconds) {
    if (state_ == State::Free || state_ == State::Stealing) return;
    fadeRate_ = -perSample(seconds, sr_);
    if (state_ == State::Active) state_ = State::FadingOut;
}

void Voice::reviveFade(float seconds) {
    if (state_ != State::FadingOut) return;
    fadeRate_ = perSample(seconds, sr_);
    state_ = State::Active;
}

void Voice::reAttack(float attackSec) {
    if (state_ != State::Releasing) return;
    env_.startAttack(attackSec);
    if (fadePos_ < 1.0f) fadeRate_ = perSample(attackSec, sr_);
    state_ = State::Active;
}

void Voice::release(float seconds) {
    if (state_ == State::Free || state_ == State::Stealing) return;
    env_.startRelease(seconds);
    state_ = State::Releasing;
}

void Voice::steal(float seconds) {
    if (state_ == State::Free) return;
    state_ = State::Stealing;
    stealRate_ = perSample(seconds, sr_);
}

void Voice::setRole(bool isRoot, int lowNote, float noteGain, float fadeSec) {
    noteGainTarget_ = noteGain;
    if (isRoot == isRoot_) return;
    isRoot_ = isRoot;
    if (isRoot) {
        if (lowPos_ <= 0.0f) {
            lowNote_ = lowNote;
            lowF0_ = static_cast<float>(midiToHz(lowNote));
            lowOsc_.phase = noise_.uniform();
        }
        lowRate_ = perSample(fadeSec, sr_);
    } else {
        lowRate_ = -perSample(fadeSec, sr_);
    }
}

float Voice::computeGain(int n) {
    const float e = env_.advance(n);
    if (fadeRate_ != 0.0f) {
        fadePos_ += fadeRate_ * n;
        if (fadePos_ >= 1.0f) { fadePos_ = 1.0f; fadeRate_ = 0.0f; }
        if (fadePos_ <= 0.0f) { fadePos_ = 0.0f; }
    }
    if (stealRate_ != 0.0f) stealGain_ = std::max(0.0f, stealGain_ - stealRate_ * n);
    noteGain_ += (noteGainTarget_ - noteGain_) * 0.03f;
    return e * eqPower(fadePos_) * stealGain_ * noteGain_;
}

void Voice::render(const RenderContext& ctx, Buses& out, int n) {
    if (state_ == State::Free || !ctx.tables) return;

    const float g0 = lastGain_;
    const float g1 = computeGain(n);
    const float mov = ctx.movement;

    // Movimento: deriva de afinação (até ±2,2 cents), volume (até ±1,2 dB), filtro (até ±0,5 oitava), pan.
    const float pitchRatio = centsToRatio(mov * 2.2f * modPitch_.tick());
    const float ampMod = 1.0f + mov * 0.14f * modAmp_.tick();
    const float cutMod = std::exp2(mov * 0.5f * modCut_.tick());
    const float panMod = mov * 0.22f * modPan_.tick();
    const float breath = 1.0f + mov * 0.35f * modAir_.tick();

    float tL[kSubBlock], tR[kSubBlock], sL[kSubBlock], sR[kSubBlock];
    for (int i = 0; i < n; ++i) { tL[i] = tR[i] = sL[i] = sR[i] = 0.0f; }
    bool anyPad = false, anyShim = false;

    // ---- Warm: 3 osciladores (centro + par desafinado), filtro passa-baixa com key tracking.
    if (ctx.layerWarm > 1e-4f) {
        anyPad = true;
        const float dc = 1.0f + ctx.detune * 13.0f; // cents
        const float base = f0_ * pitchRatio / sr_;
        const float inc0 = base;
        const float inc1 = base * centsToRatio(-dc);
        const float inc2 = base * centsToRatio(dc * 0.87f);
        const float b = ctx.brightness;
        float cutoff = (f0_ * (2.0f + 12.0f * b * b) + 250.0f + 2600.0f * b) * cutMod;
        cutoff = clampf(cutoff, 120.0f, 16000.0f);
        if (std::fabs(cutoff - lastCutoff_) > lastCutoff_ * 0.004f) {
            warmFilter_.c.lowpass(cutoff, 0.55f, sr_);
            lastCutoff_ = cutoff;
        }
        const LayerTables& t = *ctx.tables;
        const float gw = ctx.layerWarm * 0.55f;
        for (int i = 0; i < n; ++i) {
            const float o0 = warm_[0].tick(t.warmA, warmLevel_, inc0);
            const float o1 = warm_[1].tick(t.warmB, warmLevel_, inc1);
            const float o2 = warm_[2].tick(t.warmA, warmLevel_, inc2);
            float l = 0.55f * o0 + 0.75f * o1 + 0.30f * o2;
            float r = 0.55f * o0 + 0.30f * o1 + 0.75f * o2;
            warmFilter_.process(l, r);
            tL[i] += l * gw;
            tR[i] += r * gw;
        }
    }

    // ---- Strings / Texture: sample com interpolação Hermite, duas cabeças de leitura.
    if (ctx.layerStrings > 1e-4f && zone_) {
        anyPad = true;
        const double inc = sRatio_ * pitchRatio;
        const double len = zone_->length;
        const float gs = ctx.layerStrings * 1.8f;
        const float gl = gs * (1.0f - panMod), gr = gs * (1.0f + panMod);
        for (int i = 0; i < n; ++i) {
            tL[i] += zone_->read(sPosL_) * gl;
            tR[i] += zone_->read(sPosR_) * gr;
            sPosL_ += inc; if (sPosL_ >= len) sPosL_ -= len;
            sPosR_ += inc; if (sPosR_ >= len) sPosR_ -= len;
        }
    }

    // ---- Air: ruído estéreo filtrado em dois harmônicos da nota (ar afinado, não chiado).
    if (ctx.layerAir > 1e-4f) {
        anyPad = true;
        const float targetHz = 1600.0f + 3800.0f * ctx.air;
        float h1 = std::max(2.0f, std::round(targetHz / f0_));
        if (h1 != airCenterKey_) {
            const float h2 = std::max(h1 + 1.0f, std::round(h1 * 1.5f));
            const float c1 = std::min(h1 * f0_, sr_ * 0.4f);
            const float c2 = std::min(h2 * f0_, sr_ * 0.42f);
            airC_[0].bandpass(c1, 9.0f, sr_);
            airC_[1].bandpass(c2, 7.0f, sr_);
            airCenterKey_ = h1;
        }
        const float ga = ctx.layerAir * 2.6f * breath;
        for (int i = 0; i < n; ++i) {
            const float nl = noise_.bipolar();
            const float nr = noise_.bipolar();
            const float l = airL_[0].process(airC_[0], nl) + 0.6f * airL_[1].process(airC_[1], nl);
            const float r = airR_[0].process(airC_[0], nr) + 0.6f * airR_[1].process(airC_[1], nr);
            tL[i] += l * ga;
            tR[i] += r * ga;
        }
    }

    // ---- Shimmer: oitava e 12ª (harmônicos 2 e 3) quase senoidais, levemente desafinados.
    if (ctx.layerShimmer > 1e-4f) {
        anyShim = true;
        const float inc0 = 2.0f * f0_ * pitchRatio * centsToRatio(1.5f) / sr_;
        const float inc1 = 3.0f * f0_ * pitchRatio * centsToRatio(-1.2f) / sr_;
        const float g1s = (3.0f * f0_ < 7000.0f) ? 1.0f : 0.0f;
        const float gsh = ctx.layerShimmer * 0.32f * breath;
        const LayerTables& t = *ctx.tables;
        for (int i = 0; i < n; ++i) {
            const float a = shim_[0].tick(t.shimmer, shimLevel_[0], inc0);
            const float c = shim_[1].tick(t.shimmer, shimLevel_[1], inc1) * g1s;
            sL[i] = (0.80f * a + 0.30f * c) * gsh;
            sR[i] = (0.45f * a + 0.70f * c) * gsh;
        }
    }

    // ---- Soma nos barramentos com a rampa de ganho do sub-bloco.
    const float invN = 1.0f / static_cast<float>(n);
    if (anyPad || anyShim) {
        const float pl = panL_ * ampMod, pr = panR_ * ampMod;
        for (int i = 0; i < n; ++i) {
            const float g = g0 + (g1 - g0) * (static_cast<float>(i + 1) * invN);
            const float l = (tL[i] + sL[i]) * g * pl;
            const float r = (tR[i] + sR[i]) * g * pr;
            out.padL[i] += l;
            out.padR[i] += r;
            if (anyShim) { out.shimL[i] += sL[i] * g * pl; out.shimR[i] += sR[i] * g * pr; }
        }
    }

    // ---- Low: só a fundamental do acorde, mono, rampa própria para mudanças de papel.
    if (lowRate_ != 0.0f) {
        lowPos_ += lowRate_ * n;
        if (lowPos_ >= 1.0f) { lowPos_ = 1.0f; lowRate_ = 0.0f; }
        if (lowPos_ <= 0.0f) { lowPos_ = 0.0f; lowRate_ = 0.0f; }
    }
    const float lg0 = lastLowGain_;
    const float lg1 = eqPower(lowPos_) * ctx.layerLow;
    if (lg0 > 1e-6f || lg1 > 1e-6f) {
        const float inc = lowF0_ * std::sqrt(pitchRatio) / sr_;
        const LayerTables& t = *ctx.tables;
        for (int i = 0; i < n; ++i) {
            const float k = static_cast<float>(i + 1) * invN;
            const float g = (g0 + (g1 - g0) * k) * (lg0 + (lg1 - lg0) * k) * 0.75f;
            const float y = lowOsc_.tick(t.low, lowLevel_, inc) * g;
            out.lowL[i] += y;
            out.lowR[i] += y;
        }
    }
    lastLowGain_ = lg1;
    lastGain_ = g1;

    // Libera a voz só depois que a rampa chegou a zero dentro deste sub-bloco.
    const bool done = (state_ == State::Releasing && env_.isIdle()) ||
                      (state_ == State::FadingOut && fadePos_ <= 0.0f) ||
                      (state_ == State::Stealing && stealGain_ <= 0.0f) ||
                      (g1 <= 0.0f && g0 <= 0.0f && state_ != State::Active);
    if (done) free();
}

} // namespace pad
