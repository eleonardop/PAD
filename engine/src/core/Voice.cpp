#include "Voice.h"

namespace pad {

namespace {
inline float perSample(float seconds, float sr) { return 1.0f / std::max(seconds * sr, 1.0f); }

// Equilíbrio entre camadas com o volume de camada em 1,0. Todas as fontes chegam normalizadas
// em -20 dBFS RMS (PADsynth e samples), então estes ganhos definem a mixagem interna.
constexpr float kWarmGain = 1.00f;
constexpr float kVoxGain = 0.85f;
constexpr float kStringsGain = 0.95f;
constexpr float kTextureGain = 0.85f;
constexpr float kAirGain = 0.178f;   // -15 dB em relação ao corpo: o Air nunca domina, por construção
constexpr float kGlassGain = 0.30f;
constexpr float kLowGain = 0.17f;   // a wavetable do grave tem pico 1 (RMS ~0,7); as demais fontes chegam a RMS 0,1
constexpr float kAvailRampSeconds = 0.08f;
}

void Voice::prepare(float sampleRate, uint32_t index) {
    sr_ = sampleRate;
    index_ = index;
    env_.setSampleRate(sampleRate);
    rng_.setSeed(0xA11CE000ULL + index * 7919ULL);
    free();
}

void Voice::free() {
    state_ = State::Free;
    lastGain_ = 0.0f;
    lastLowGain_ = 0.0f;
    env_.reset();
    for (Layer* L : {&warm_, &vox_, &strings_, &texture_, &air_, &glass_}) *L = Layer{};
}

uint32_t Voice::sourcesInUse() const {
    if (state_ == State::Free) return 0;
    uint32_t m = 0;
    for (const Layer* L : {&warm_, &vox_, &strings_, &texture_, &air_, &glass_}) {
        if (L->a && L->srcA != SourceId::Count) m |= sourceBit(L->srcA);
        if (L->b && L->srcB != SourceId::Count) m |= sourceBit(L->srcB);
    }
    return m;
}

bool Voice::acquire(Layer& L, const RenderContext& ctx, SourceId a, SourceId b) {
    if (!ctx.sources || a == SourceId::Count) return false;
    const ZoneBank* bankA = ctx.sources->get(a);
    if (!bankA || bankA->empty()) return false;
    // Glass é gerado uma oitava acima da nota.
    const float target = (a == SourceId::Glass) ? f0_ * 2.0f : f0_;
    double ratio = 1.0;
    const Zone* za = bankA->nearest(target, ctx.sampleRate, ratio);
    if (!za || za->length <= 4) return false;
    const Zone* zb = nullptr;
    if (b != SourceId::Count) {
        const ZoneBank* bankB = ctx.sources->get(b);
        if (bankB) {
            const Zone* cand = bankB->zoneAt(bankA->indexOf(za));
            if (cand && cand->length == za->length && cand->rootNote == za->rootNote) zb = cand;
        }
    }
    L.a = za;
    L.b = zb;
    L.srcA = a;
    L.srcB = zb ? b : SourceId::Count;
    L.ratio = ratio;
    // Ponto de partida aleatório: notas repetidas nunca começam no mesmo lugar do loop.
    const uint64_t len = static_cast<uint64_t>(za->length) << 32;
    L.posL = static_cast<uint64_t>(static_cast<double>(rng_.uniform()) * static_cast<double>(len)) % len;
    L.posR = za->channels == 2 ? L.posL : (L.posL + len / 2) % len;
    return true;
}

void Voice::start(int note, bool isRoot, int lowNote, float noteGain, bool useAttack, float attackSec, float fadeSec,
                  uint64_t age, const RenderContext& ctx) {
    free();
    note_ = note;
    f0_ = static_cast<float>(midiToHz(note));
    age_ = age;
    state_ = State::Active;
    noteGain_ = noteGainTarget_ = noteGain;
    stealGain_ = 1.0f;
    stealRate_ = 0.0f;
    lastGain_ = 0.0f;

    if (useAttack) {
        env_.startAttack(attackSec);
        fadePos_ = 1.0f;
        fadeRate_ = 0.0f;
    } else {
        env_.startSustain();
        fadePos_ = 0.0f;
        fadeRate_ = perSample(fadeSec, sr_);
    }

    isRoot_ = isRoot;
    lowNote_ = lowNote;
    lowF0_ = static_cast<float>(midiToHz(lowNote));
    lowPos_ = isRoot ? 1.0f : 0.0f;
    lowRate_ = 0.0f;
    lastLowGain_ = eqPower(lowPos_) * ctx.layerLow;
    lowOsc_.phase = rng_.uniform();
    if (ctx.tables) lowLevel_ = ctx.tables->low.levelFor(lowF0_);

    // Fontes disponíveis agora entram com a nota (a rampa da própria nota cuida da entrada).
    // As que ainda estão carregando entram depois com rampa própria (acquireMissing).
    acquireMissing(ctx);
    for (Layer* L : {&warm_, &vox_, &strings_, &texture_, &air_, &glass_}) { L->avail = 1.0f; L->lastGain = 0.0f; }

    ladL_.reset(); ladR_.reset(); sampleTone_.reset(); airTone_.reset();
    lastCutoff_ = lastSampleCut_ = lastAirCut_ = lastDrive_ = -1.0f;

    const uint64_t seed = (static_cast<uint64_t>(index_) << 32) ^ (age * 0x9E3779B97F4A7C15ULL) ^ static_cast<uint64_t>(note);
    const float cr = ctx.controlRate;
    modPitch_.init(seed + 1, 0.10f + 0.08f * rng_.uniform(), cr);
    modAmp_.init(seed + 2, 0.06f + 0.07f * rng_.uniform(), cr);
    modCut_.init(seed + 3, 0.05f + 0.06f * rng_.uniform(), cr);
    modPan_.init(seed + 4, 0.04f + 0.05f * rng_.uniform(), cr);
    modBreath_.init(seed + 5, 0.06f + 0.10f * rng_.uniform(), cr);
    morphWarm_.init(seed + 6, 0.03f, cr);
    morphVox_.init(seed + 7, 0.03f, cr);
    morphAir_.init(seed + 8, 0.04f, cr);
    evoCut_.init(seed + 9, 0.025f, cr);

    // Pan determinístico por nota (a mesma nota mantém o lugar no panorama).
    const float pan = 0.30f * std::sin(static_cast<float>(note) * 2.399f);
    panL_ = eqPower(0.5f * (1.0f - pan)) * 1.41421356f;
    panR_ = eqPower(0.5f * (1.0f + pan)) * 1.41421356f;
}

void Voice::acquireMissing(const RenderContext& ctx) {
    const bool late = lastGain_ > 0.0f;
    auto tryLayer = [&](Layer& L, float level, SourceId a, SourceId b) {
        if (L.a || level <= 1e-4f) return;
        if (acquire(L, ctx, a, b) && late) { L.avail = 0.0f; L.lastGain = 0.0f; }
    };
    tryLayer(warm_, ctx.layerWarm, SourceId::WarmDark, SourceId::WarmOpen);
    tryLayer(vox_, ctx.layerVox, SourceId::VoxOh, SourceId::VoxAh);
    tryLayer(air_, ctx.layerAir, SourceId::AirA, SourceId::AirB);
    tryLayer(glass_, ctx.layerShimmer, SourceId::Glass, SourceId::Count);
    if (!strings_.a && ctx.layerStrings > 1e-4f && ctx.sources) {
        SourceId pref = ctx.stringsSource == 0 ? SourceId::StringsReal : SourceId::StringsSynth;
        // Reserva automática: se os samples reais não puderem ser carregados, usa as sintéticas.
        if (pref == SourceId::StringsReal && ctx.sources->state(pref) == SourceLibrary::State::Failed) pref = SourceId::StringsSynth;
        tryLayer(strings_, ctx.layerStrings, pref, SourceId::Count);
    }
    if (!texture_.a && ctx.layerTexture > 1e-4f)
        tryLayer(texture_, ctx.layerTexture, ctx.textureType == 0 ? SourceId::PianoCathedral : SourceId::Organ, SourceId::Count);
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
            lowOsc_.phase = rng_.uniform();
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

void Voice::renderLayer(Layer& L, float* oL, float* oR, int n, float pitchRatio, float detuneCents, float gain, float morph) {
    if (!L.a) return;
    // Rampa de disponibilidade (fonte que chegou depois do início da nota).
    if (L.avail < 1.0f) L.avail = std::min(1.0f, L.avail + static_cast<float>(n) / (kAvailRampSeconds * sr_));
    const float g0 = L.lastGain, g1 = gain * L.avail;
    const float m0 = L.lastMorph, m1 = L.b ? morph : 0.0f;
    L.lastGain = g1;
    L.lastMorph = m1;
    const uint64_t len = static_cast<uint64_t>(L.a->length) << 32;
    const double scale = 4294967296.0;
    const uint64_t incL = static_cast<uint64_t>(L.ratio * pitchRatio * static_cast<double>(centsToRatio(-0.5f * detuneCents)) * scale);
    const uint64_t incR = static_cast<uint64_t>(L.ratio * pitchRatio * static_cast<double>(centsToRatio(0.5f * detuneCents)) * scale);
    if (g0 <= 1e-7f && g1 <= 1e-7f) {
        // Mantém a fase andando (sem custo de leitura) para a camada voltar sem salto.
        L.posL = (L.posL + incL * static_cast<uint64_t>(n)) % len;
        L.posR = (L.posR + incR * static_cast<uint64_t>(n)) % len;
        return;
    }
    const Zone& A = *L.a;
    const int16_t* aL = A.l.data() + 1;
    const int16_t* aR = (A.channels == 2 ? A.r.data() : A.l.data()) + 1;
    const float sc = A.scale;
    // Leitura da tabela B só quando ela contribui; nos extremos do morph lê só uma.
    const bool onlyB = L.b && m0 >= 0.999f && m1 >= 0.999f;
    const bool useB = L.b && !onlyB && (m0 > 1e-3f || m1 > 1e-3f);
    const Zone* src = onlyB ? L.b : &A;
    const int16_t* pL = onlyB ? L.b->l.data() + 1 : aL;
    const int16_t* pR = onlyB ? (L.b->channels == 2 ? L.b->r.data() : L.b->l.data()) + 1 : aR;
    const float pScale = src->scale;
    const int16_t* bL = useB ? L.b->l.data() + 1 : nullptr;
    const int16_t* bR = useB ? (L.b->channels == 2 ? L.b->r.data() : L.b->l.data()) + 1 : nullptr;
    const float bScale = useB ? L.b->scale : 0.0f;
    constexpr float kFrac = 1.0f / 4294967296.0f;
    const float invN = 1.0f / static_cast<float>(n);
    uint64_t posL = L.posL, posR = L.posR;
    for (int i = 0; i < n; ++i) {
        const float k = static_cast<float>(i + 1) * invN;
        const float g = g0 + (g1 - g0) * k;
        const int iL = static_cast<int>(posL >> 32), iR = static_cast<int>(posR >> 32);
        const float fL = static_cast<float>(posL & 0xffffffffu) * kFrac;
        const float fR = static_cast<float>(posR & 0xffffffffu) * kFrac;
        float l = Zone::hermite16(pL + iL, fL) * pScale;
        float r = Zone::hermite16(pR + iR, fR) * pScale;
        if (useB) {
            const float m = m0 + (m1 - m0) * k;
            l += (Zone::hermite16(bL + iL, fL) * bScale - l) * m;
            r += (Zone::hermite16(bR + iR, fR) * bScale - r) * m;
        }
        (void)sc;
        oL[i] += l * g;
        oR[i] += r * g;
        posL += incL; if (posL >= len) posL -= len;
        posR += incR; if (posR >= len) posR -= len;
    }
    L.posL = posL;
    L.posR = posR;
}

void Voice::render(const RenderContext& ctx, Buses& out, int n) {
    if (state_ == State::Free) return;
    acquireMissing(ctx);

    const float g0 = lastGain_;
    const float g1 = computeGain(n);
    const float mov = ctx.movement;
    const float evo = ctx.evolution;
    const float inten = ctx.intensity;

    // ---- Moduladores (taxa de controle)
    const float pitchRatio = centsToRatio(mov * 2.0f * modPitch_.tick());
    const float ampMod = 1.0f + mov * 0.10f * modAmp_.tick();
    const float panMod = mov * 0.20f * modPan_.tick();
    const float breathRaw = modBreath_.tick();                // -1..1
    const float breath = 1.0f + evo * 0.16f * breathRaw;     // até ±1,3 dB
    const float morphRate = 0.017f + 0.05f * evo;            // ciclos de ~60 s (calmo) a ~15 s (vivo)
    morphWarm_.setRate(morphRate, ctx.controlRate);
    morphVox_.setRate(morphRate * 0.8f, ctx.controlRate);
    morphAir_.setRate(morphRate * 1.3f, ctx.controlRate);
    evoCut_.setRate(morphRate * 0.7f, ctx.controlRate);
    // O modulador suave fica tipicamente em ±0,5; o fator 1,1 leva o morph a percorrer quase
    // toda a faixa entre as duas tabelas com Evolução no máximo.
    // 70% comum ao conjunto + 30% próprio da voz (cada nota ainda respira um pouco diferente).
    const float gW = 0.7f * ctx.evoMorph + 0.3f * morphWarm_.tick();
    const float gV = 0.7f * ctx.evoMorph2 + 0.3f * morphVox_.tick();
    const float gA = 0.7f * ctx.evoMorph2 + 0.3f * morphAir_.tick();
    const float mWarm = clampf(0.10f + 0.65f * inten + 0.6f * evo * gW, 0.0f, 1.0f);
    const float mVox = clampf(0.45f + 0.25f * (inten - 0.5f) + 0.6f * evo * gV, 0.0f, 1.0f);
    const float mAir = clampf(0.5f + 0.6f * evo * gA, 0.0f, 1.0f);
    const float intensityGain = dbToGain((inten - 0.5f) * 6.0f);
    const float detuneCents = ctx.detune * 9.0f;

    // ---- Filtros (recalculados só quando mudam)
    const float b = clampf(ctx.brightness + 0.3f * (inten - 0.5f), 0.0f, 1.0f);
    float cutoff = (f0_ * (3.0f + 10.0f * b * b) + 600.0f + 5200.0f * b) *
                   std::exp2(1.0f * evo * (0.7f * ctx.evoCut + 0.3f * evoCut_.tick()) + 0.35f * mov * modCut_.tick() +
                             1.4f * (inten - 0.5f)); // Intensidade abre o filtro (±0,7 oitava), como tocar mais forte
    cutoff = clampf(cutoff, 120.0f, 15000.0f);
    const float drive = 0.4f + 1.0f * ctx.warmth;
    if (std::fabs(cutoff - lastCutoff_) > lastCutoff_ * 0.003f || std::fabs(drive - lastDrive_) > 1e-3f) {
        const float kRes = 0.2f + 0.5f * ctx.warmth;
        ladL_.setCoefficients(cutoff, kRes, sr_); ladR_.setCoefficients(cutoff, kRes, sr_);
        ladL_.setDrive(drive); ladR_.setDrive(drive);
        lastCutoff_ = cutoff;
        lastDrive_ = drive;
    }
    const float sampleCut = clampf(cutoff * 2.5f + 1500.0f, 1500.0f, 18000.0f);
    if (std::fabs(sampleCut - lastSampleCut_) > lastSampleCut_ * 0.003f) {
        sampleTone_.c.lowpass(sampleCut, 0.6f, sr_);
        lastSampleCut_ = sampleCut;
    }
    const float airCut = 5500.0f + 3500.0f * ctx.air;
    if (std::fabs(airCut - lastAirCut_) > 1.0f) {
        airTone_.c.lowpass(airCut, 0.6f, sr_);
        lastAirCut_ = airCut;
    }

    float bL[kSubBlock], bR[kSubBlock], sL[kSubBlock], sR[kSubBlock], aL[kSubBlock], aR[kSubBlock], gL[kSubBlock], gR[kSubBlock];
    for (int i = 0; i < n; ++i) bL[i] = bR[i] = sL[i] = sR[i] = aL[i] = aR[i] = gL[i] = gR[i] = 0.0f;

    // ---- Corpo sintetizado (Warm + Vox) -> ladder
    renderLayer(warm_, bL, bR, n, pitchRatio, detuneCents, ctx.layerWarm * kWarmGain * breath, mWarm);
    renderLayer(vox_, bL, bR, n, pitchRatio, detuneCents, ctx.layerVox * kVoxGain * (2.0f - breath), mVox);
    const bool anyBody = warm_.a || vox_.a;
    if (anyBody) for (int i = 0; i < n; ++i) { bL[i] = ladL_.process(bL[i]); bR[i] = ladR_.process(bR[i]); }

    // ---- Samples reais (Strings + Texture) -> passa-baixa suave
    renderLayer(strings_, sL, sR, n, pitchRatio, detuneCents, ctx.layerStrings * kStringsGain, 0.0f);
    renderLayer(texture_, sL, sR, n, pitchRatio, detuneCents, ctx.layerTexture * kTextureGain * breath, 0.0f);
    const bool anySample = strings_.a || texture_.a;
    if (anySample) for (int i = 0; i < n; ++i) sampleTone_.process(sL[i], sR[i]);

    // ---- Air harmônico -> passa-baixa 12 dB/oitava. Respira mais que o corpo (±2,4 dB).
    renderLayer(air_, aL, aR, n, pitchRatio, detuneCents * 1.5f, ctx.layerAir * kAirGain * (1.0f + evo * 0.3f * breathRaw), mAir);
    if (air_.a) for (int i = 0; i < n; ++i) airTone_.process(aL[i], aR[i]);

    // ---- Glass (shimmer)
    renderLayer(glass_, gL, gR, n, pitchRatio, detuneCents, ctx.layerShimmer * kGlassGain * breath, 0.0f);

    // ---- Soma nos barramentos com a rampa de ganho da voz
    const float invN = 1.0f / static_cast<float>(n);
    const float pl = panL_ * ampMod * intensityGain * (1.0f - panMod), pr = panR_ * ampMod * intensityGain * (1.0f + panMod);
    if (anyBody || anySample || air_.a || glass_.a) {
        for (int i = 0; i < n; ++i) {
            const float g = g0 + (g1 - g0) * (static_cast<float>(i + 1) * invN);
            const float gl = g * pl, gr = g * pr;
            out.padL[i] += (bL[i] + sL[i]) * gl;
            out.padR[i] += (bR[i] + sR[i]) * gr;
            out.airL[i] += aL[i] * gl;
            out.airR[i] += aR[i] * gr;
            out.shimL[i] += gL[i] * gl;
            out.shimR[i] += gR[i] * gr;
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
    if ((lg0 > 1e-6f || lg1 > 1e-6f) && ctx.tables) {
        const float inc = lowF0_ * std::sqrt(pitchRatio) / sr_;
        const float li = intensityGain * kLowGain;
        for (int i = 0; i < n; ++i) {
            const float k = static_cast<float>(i + 1) * invN;
            const float g = (g0 + (g1 - g0) * k) * (lg0 + (lg1 - lg0) * k) * li;
            const float y = lowOsc_.tick(ctx.tables->low, lowLevel_, inc) * g;
            out.lowL[i] += y;
            out.lowR[i] += y;
        }
    }
    lastLowGain_ = lg1;
    lastGain_ = g1;

    const bool done = (state_ == State::Releasing && env_.isIdle()) ||
                      (state_ == State::FadingOut && fadePos_ <= 0.0f) ||
                      (state_ == State::Stealing && stealGain_ <= 0.0f) ||
                      (g1 <= 0.0f && g0 <= 0.0f && state_ != State::Active);
    if (done) free();
}

} // namespace pad
