#include "PadEngine.h"
#include "dsp/ClassicReverb.h"
#include "dsp/DattorroPlate.h"
#include "dsp/CloudReverb.h"
#include <chrono>
#include <thread>

namespace pad {

namespace {
constexpr float kHeadroom = 1.70f;          // ganho interno antes do compressor e do limitador
constexpr float kLimiterThreshold = 0.891f; // -1 dBFS
constexpr float kAirDirect = 0.6f;          // parte do Air que vai direto; o resto aparece só no espaço
constexpr float kShimmerFeedback = 0.30f;   // realimentação máxima do shimmer (Shimmer = 1)
constexpr float kReverbSwitchSeconds = 0.35f;
// v0.3: texturas e pratos ficam mais no espaço do que à frente.
constexpr float kTexDry = 0.60f;       // parte seca das texturas tonais (entra no corpo)
constexpr float kTexSendExtra = 0.55f; // envio extra das texturas (além do que já vai com o corpo)
constexpr float kMetalDry = 0.35f;
constexpr float kMetalSend = 1.20f;

void atomicMax(std::atomic<float>& a, float v) {
    float cur = a.load(std::memory_order_relaxed);
    while (v > cur && !a.compare_exchange_weak(cur, v, std::memory_order_relaxed)) {}
}
void atomicMin(std::atomic<float>& a, float v) {
    float cur = a.load(std::memory_order_relaxed);
    while (v < cur && !a.compare_exchange_weak(cur, v, std::memory_order_relaxed)) {}
}
}

PadEngine::PadEngine() {
    for (int i = 0; i < kParamCount; ++i) params_[static_cast<size_t>(i)].store(paramInfo(static_cast<ParamId>(i)).defaultValue);
}

PadEngine::~PadEngine() { sources_.stop(); }

bool PadEngine::prepare(float sampleRate, double waitSeconds) {
    if (sampleRate < 8000.0f || sampleRate > 192000.0f) return false;
    prepared_.store(false, std::memory_order_release);
    sr_ = sampleRate;

    sources_.configure(sr_, assets_);
    sources_.start();

    auto tables = std::make_unique<LayerTables>();
    tables->low.build([](int h) { return h == 1 ? 1.0f : h == 2 ? 0.22f : h == 3 ? 0.06f : 0.0f; }, sr_, 41, false);
    tables_ = std::move(tables);

    ctx_ = RenderContext{};
    ctx_.sampleRate = sr_;
    ctx_.controlRate = sr_ / kSubBlock;
    ctx_.tables = tables_.get();
    ctx_.sources = &sources_;

    voices_.prepare(sr_);
    texture_.prepare(sr_);
    conductor_.prepare(ctx_.controlRate,
                       static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()) | 1u);
    tape_.prepare(sr_);
    metalHp_.c.highpass(300.0f, 0.707f, sr_);
    metalLp_.c.lowpass(6500.0f, 0.707f, sr_);
    metalHp_.reset(); metalLp_.reset();

    for (int i = 0; i < kParamCount; ++i) {
        const ParamInfo& info = paramInfo(static_cast<ParamId>(i));
        smooth_[static_cast<size_t>(i)].setTime(info.smoothingSeconds, ctx_.controlRate);
        smooth_[static_cast<size_t>(i)].snap(params_[static_cast<size_t>(i)].load());
    }

    padHp_.c.highpass(70.0f, 0.707f, sr_);
    lowLp_.c.lowpass(260.0f, 0.707f, sr_);
    lowHp_.c.highpass(28.0f, 0.707f, sr_);
    sendHp_.c.highpass(180.0f, 0.707f, sr_);
    sendLp_.c.lowpass(7500.0f, 0.707f, sr_);
    for (SvfStereo* f : {&padHp_, &lowLp_, &lowHp_, &lowShelf_, &warmBell_, &warmHighCut_, &tilt_, &sendHp_, &sendLp_}) f->reset();
    dcL_.setLowpass(8.0f, sr_); dcR_.setLowpass(8.0f, sr_); dcL_.reset(); dcR_.reset();
    sideLp_.setLowpass(140.0f, sr_); sideLp_.reset();
    for (float& t : lastTone_) t = -1.0f;

    ensemble_.prepare(sr_);
    shimmer_.prepare(sr_);
    reverbs_[0].reverb = std::make_unique<ClassicReverb>();
    reverbs_[1].reverb = std::make_unique<DattorroPlate>();
    reverbs_[2].reverb = std::make_unique<CloudReverb>();
    for (auto& s : reverbs_) { s.reverb->prepare(sr_); s.gain = 0.0f; s.running = false; s.quietBlocks = 0; }
    activeReverb_ = -1;
    glue_.prepare(sr_, kSubBlock);
    glue_.setThresholdDb(-17.0f);
    glue_.setRatio(1.6f);
    limiter_.prepare(sr_, 3.0f, 180.0f);
    limiter_.setThreshold(kLimiterThreshold);

    Command c;
    while (commands_.pop(c)) {}

    updateControl();
    // Começa já com o reverb do preset ativo (sem rampa) e os ganhos nos valores atuais.
    if (activeReverb_ >= 0) reverbs_[static_cast<size_t>(activeReverb_)].gain = 1.0f;
    dryGain_ = 1.0f; wetGain_ = 0.0f; chorus_ = smooth_[static_cast<int>(ParamId::Chorus)].value();

    refreshWanted();
    waitForSources(waitSeconds);
    prepared_.store(true, std::memory_order_release);
    return true;
}

// ----------------------------------------------------------------------------- fontes

uint32_t PadEngine::wantedMask() const {
    auto P = [&](ParamId id) { return params_[static_cast<size_t>(id)].load(std::memory_order_relaxed); };
    uint32_t m = 0;
    if (P(ParamId::LayerWarm) > 1e-4f) m |= sourceBit(SourceId::WarmDark) | sourceBit(SourceId::WarmOpen);
    if (P(ParamId::LayerVox) > 1e-4f) m |= sourceBit(SourceId::VoxAh) | sourceBit(SourceId::VoxOh);
    if (P(ParamId::LayerAir) > 1e-4f) m |= sourceBit(SourceId::AirA) | sourceBit(SourceId::AirB);
    if (P(ParamId::LayerShimmer) > 1e-4f) m |= sourceBit(SourceId::Glass);
    if (P(ParamId::LayerStrings) > 1e-4f) {
        if (P(ParamId::StringsSource) < 0.5f) {
            m |= sourceBit(SourceId::StringsReal);
            // Se os samples reais falharam (assets ausentes), a reserva sintética é carregada.
            if (sources_.state(SourceId::StringsReal) == SourceLibrary::State::Failed) m |= sourceBit(SourceId::StringsSynth);
        } else {
            m |= sourceBit(SourceId::StringsSynth);
        }
    }
    if (P(ParamId::LayerTexture) > 1e-4f) m |= sourceBit(P(ParamId::TextureType) < 0.5f ? SourceId::PianoCathedral : SourceId::Organ);
    // v0.3: texturas e transições. O swell manual sempre pode ser pedido, então piano/EP
    // invertido, pratos e Glass ficam carregados enquanto houver texturas ou transições.
    const bool keys = P(ParamId::LayerKeys) > 1e-4f, ep = P(ParamId::KeysType) >= 0.5f;
    const bool transitions = P(ParamId::Bloom) > 1e-4f || keys || P(ParamId::LayerMetal) > 1e-4f;
    if (keys) m |= sourceBit(ep ? SourceId::EpTine : SourceId::PianoSoft);
    if (P(ParamId::LayerSparkle) > 1e-4f) m |= sourceBit(SourceId::Vibes);
    if (P(ParamId::LayerMetal) > 1e-4f || transitions) m |= sourceBit(SourceId::Cymbal) | sourceBit(SourceId::CymbalRev);
    if (transitions) m |= sourceBit(ep ? SourceId::EpTineRev : SourceId::PianoSoftRev) | sourceBit(SourceId::Glass);
    // v0.4: fontes pedidas pela paleta de microeventos do preset.
    auto M = [&](int i) { return micro_[static_cast<size_t>(i)].load(std::memory_order_relaxed) > 0.0f; };
    if (M(0)) m |= sourceBit(SourceId::Vibes);
    if (M(1)) m |= sourceBit(ep ? SourceId::EpTine : SourceId::PianoSoft);
    if (M(2)) m |= sourceBit(SourceId::EpTine);
    if (M(3)) m |= sourceBit(SourceId::Glass);
    if (M(4)) m |= sourceBit(ep ? SourceId::EpTineRev : SourceId::PianoSoftRev);
    if (M(5)) m |= sourceBit(SourceId::Cymbal);
    return m;
}

void PadEngine::refreshWanted() { sources_.setWanted(wantedMask()); }

bool PadEngine::waitForSources(double timeoutSeconds) {
    refreshWanted();
    bool ok = sources_.waitReady(wantedMask(), timeoutSeconds);
    // Uma fonte real pode ter falhado agora e exigir a reserva: pede de novo e espera.
    refreshWanted();
    ok = sources_.waitReady(wantedMask(), timeoutSeconds) && ok;
    return ok;
}

// ----------------------------------------------------------------------------- controle

bool PadEngine::pushCommand(const Command& c) {
    std::lock_guard<std::mutex> lock(producerMutex_); // só o lado produtor
    if (!commands_.push(c)) { statDropped_.fetch_add(1, std::memory_order_relaxed); return false; }
    return true;
}

bool PadEngine::playChord(int rootPitchClass, ChordType type) {
    const int pc = ((rootPitchClass % 12) + 12) % 12;
    const int t = static_cast<int>(type) < static_cast<int>(ChordType::Count) ? static_cast<int>(type) : 0;
    uiKey_.store(pc);
    uiChord_.store(t);
    return pushCommand({CmdType::PlayChord, static_cast<uint8_t>(pc), static_cast<uint8_t>(t), 0});
}
bool PadEngine::setKey(int rootPitchClass) { return playChord(rootPitchClass, static_cast<ChordType>(uiChord_.load())); }
bool PadEngine::setChordType(ChordType type) { return playChord(uiKey_.load(), type); }
bool PadEngine::stop() { return pushCommand({CmdType::Stop, 0, 0, 0}); }
bool PadEngine::panic() { return pushCommand({CmdType::Panic, 0, 0, 0}); }
bool PadEngine::swell() { return pushCommand({CmdType::Swell, 0, 0, 0}); }
bool PadEngine::setSeed(uint32_t seed) {
    Command c{CmdType::Seed, 0, 0, 0};
    c.seed = seed;
    return pushCommand(c);
}

void PadEngine::setParam(ParamId id, float value) {
    const int i = static_cast<int>(id);
    if (i < 0 || i >= kParamCount) return;
    const ParamInfo& info = paramInfo(id);
    if (!std::isfinite(value)) return;
    params_[static_cast<size_t>(i)].store(clampf(value, info.minValue, info.maxValue), std::memory_order_relaxed);
    switch (id) {
        case ParamId::LayerWarm: case ParamId::LayerVox: case ParamId::LayerAir: case ParamId::LayerShimmer:
        case ParamId::LayerStrings: case ParamId::LayerTexture: case ParamId::StringsSource: case ParamId::TextureType:
        case ParamId::LayerKeys: case ParamId::KeysType: case ParamId::LayerSparkle: case ParamId::LayerMetal: case ParamId::Bloom:
            refreshWanted();
            break;
        default: break;
    }
}

float PadEngine::getParam(ParamId id) const {
    const int i = static_cast<int>(id);
    if (i < 0 || i >= kParamCount) return 0.0f;
    return params_[static_cast<size_t>(i)].load(std::memory_order_relaxed);
}

void PadEngine::applyPreset(int index) {
    const Preset& p = preset(index);
    for (int i = 0; i < kParamCount; ++i) {
        if (static_cast<ParamId>(i) == ParamId::MasterVolume) continue; // o volume é do operador
        const ParamInfo& info = paramInfo(static_cast<ParamId>(i));
        params_[static_cast<size_t>(i)].store(clampf(p.values[i], info.minValue, info.maxValue), std::memory_order_relaxed);
    }
    presetTrim_.store(dbToGain(p.trimDb), std::memory_order_relaxed);
    const MicroPalette& mp = p.micro;
    const float pv[10] = {mp.bell, mp.piano, mp.pluck, mp.harmonic, mp.reverse, mp.metal, mp.bow, mp.tapeGust, mp.perMinute, mp.highBias};
    for (int i = 0; i < 10; ++i) micro_[static_cast<size_t>(i)].store(pv[i], std::memory_order_relaxed);
    refreshWanted();
}

EngineStats PadEngine::readStats() {
    EngineStats s;
    s.activeVoices = statVoices_.load(std::memory_order_relaxed);
    s.steals = statSteals_.load(std::memory_order_relaxed);
    s.hardSteals = statHardSteals_.load(std::memory_order_relaxed);
    s.droppedCommands = statDropped_.load(std::memory_order_relaxed);
    s.limiterMinGain = statLimiter_.exchange(1.0f, std::memory_order_relaxed);
    s.peakOut = statPeak_.exchange(0.0f, std::memory_order_relaxed);
    s.glueMaxReductionDb = statGlue_.exchange(0.0f, std::memory_order_relaxed);
    s.load = statLoad_.load(std::memory_order_relaxed);
    s.loadPeak = statLoadPeak_.exchange(0.0f, std::memory_order_relaxed);
    s.textureVoices = statTexVoices_.load(std::memory_order_relaxed);
    s.textureEvents = statTexEvents_.load(std::memory_order_relaxed);
    s.blooms = statBlooms_.load(std::memory_order_relaxed);
    s.swells = statSwells_.load(std::memory_order_relaxed);
    s.lifeStage = statStage_.load(std::memory_order_relaxed);
    s.currentKey = uiKey_.load();
    s.currentChord = uiChord_.load();
    s.playing = statPlaying_.load(std::memory_order_relaxed);
    s.sourcesReady = sources_.readyMask();
    s.sourcesFailed = sources_.failedMask();
    s.sourcesMemoryBytes = sources_.memoryBytes();
    return s;
}

// ----------------------------------------------------------------------------- áudio

void PadEngine::handleCommands() {
    Command c;
    while (commands_.pop(c)) {
        switch (c.type) {
            case CmdType::PlayChord: {
                const Voicing v = buildVoicing(c.root, static_cast<ChordType>(c.chord));
                voices_.playChord(v, getParam(ParamId::Attack), getParam(ParamId::CrossfadeTime), getParam(ParamId::Release), ctx_);
                conductor_.onChord(v, static_cast<ChordType>(c.chord), c.root);
                break;
            }
            case CmdType::Stop:
                voices_.stopAll(getParam(ParamId::Release));
                conductor_.onStop();
                texture_.releaseAll(std::max(2.0f, getParam(ParamId::Release)));
                break;
            case CmdType::Panic:
                voices_.panic();
                conductor_.onStop();
                texture_.panic();
                break;
            case CmdType::Swell: conductor_.onSwell(); break;
            case CmdType::Seed: conductor_.reseed(c.seed); break;
        }
    }
}

void PadEngine::updateControl() {
    float v[kParamCount];
    for (int i = 0; i < kParamCount; ++i) {
        Smoother& s = smooth_[static_cast<size_t>(i)];
        s.setTarget(params_[static_cast<size_t>(i)].load(std::memory_order_relaxed));
        v[i] = s.tick();
    }
    auto P = [&](ParamId id) { return v[static_cast<int>(id)]; };

    ctx_.layerWarm = P(ParamId::LayerWarm);
    ctx_.layerVox = P(ParamId::LayerVox);
    ctx_.layerStrings = P(ParamId::LayerStrings);
    ctx_.layerTexture = P(ParamId::LayerTexture);
    ctx_.layerAir = P(ParamId::LayerAir);
    ctx_.layerShimmer = P(ParamId::LayerShimmer);
    ctx_.layerLow = P(ParamId::LayerLow);
    ctx_.brightness = P(ParamId::Brightness);
    ctx_.warmth = P(ParamId::Warmth);
    ctx_.air = P(ParamId::Air);
    ctx_.movement = P(ParamId::Movement);
    ctx_.detune = P(ParamId::Detune);
    ctx_.evolution = P(ParamId::Evolution);
    {
        const double rate = 0.017 + 0.05 * ctx_.evolution; // ciclos de ~60 s (calmo) a ~15 s (vivo)
        const double inc = rate / ctx_.controlRate;
        auto wave = [&](int i, double mul) {
            double& a = evoPhase_[i];
            double& b = evoPhase_[i + 3];
            a += inc * mul;            a -= std::floor(a);
            b += inc * mul * 1.618034; b -= std::floor(b);
            return static_cast<float>(0.65 * std::sin(2.0 * M_PI * a) + 0.35 * std::sin(2.0 * M_PI * b));
        };
        ctx_.evoMorph = wave(0, 1.0);
        ctx_.evoMorph2 = wave(1, 1.27);
        ctx_.evoCut = wave(2, 0.71);
    }
    ctx_.intensity = P(ParamId::Intensity);
    ctx_.layerKeys = P(ParamId::LayerKeys);
    ctx_.keysType = P(ParamId::KeysType) < 0.5f ? 0 : 1;
    ctx_.layerSparkle = P(ParamId::LayerSparkle);
    ctx_.layerMetal = P(ParamId::LayerMetal);
    ctx_.tape = P(ParamId::Tape);
    ctx_.activity = P(ParamId::Activity);
    ctx_.bloom = P(ParamId::Bloom);
    ctx_.bloomChance = P(ParamId::BloomChance);
    ctx_.swellTime = P(ParamId::SwellTime);
    {
        auto M = [&](int i) { return micro_[static_cast<size_t>(i)].load(std::memory_order_relaxed); };
        ctx_.microBell = M(0); ctx_.microPiano = M(1); ctx_.microPluck = M(2); ctx_.microHarmonic = M(3);
        ctx_.microReverse = M(4); ctx_.microMetal = M(5); ctx_.microBow = M(6); ctx_.microTapeGust = M(7);
        ctx_.microPerMinute = M(8); ctx_.microHighBias = M(9);
    }
    // Maestro: decide eventos e devolve a "segunda camada" e os deslocamentos do organismo.
    conductor_.tick(ctx_, sources_, texture_);
    {
        const ConductorOut& co = conductor_.out();
        ctx_.layerStrings *= co.strings;
        ctx_.layerVox *= co.vox;
        ctx_.layerShimmer *= co.glass;
        ctx_.brightness = clampf(ctx_.brightness + co.bright, 0.0f, 1.0f);
        sendBoost_ = co.send;
        baseWidthBoost_ = co.width;
    }
    ctx_.stringsSource = P(ParamId::StringsSource) < 0.5f ? 0 : 1;
    ctx_.textureType = P(ParamId::TextureType) < 0.5f ? 0 : 1;

    // Tom do conjunto (recalcula só quando o valor muda).
    const float low = P(ParamId::Low), warmth = P(ParamId::Warmth), bright = P(ParamId::Brightness), inten = P(ParamId::Intensity);
    if (std::fabs(low - lastTone_[0]) > 1e-4f) {
        const float db = low < 0.5f ? (low - 0.5f) * 24.0f : (low - 0.5f) * 10.0f; // -12..+5 dB
        lowShelf_.c.lowShelf(140.0f, 0.7f, db, sr_);
        lastTone_[0] = low;
    }
    if (std::fabs(warmth - lastTone_[1]) > 1e-4f) {
        warmBell_.c.bell(300.0f, 0.8f, warmth * 3.0f - 0.5f, sr_);     // -0,5..+2,5 dB
        warmHighCut_.c.highShelf(6500.0f, 0.7f, -warmth * 3.0f, sr_);  //  0..-3 dB
        satDrive_ = 1.0f + 1.5f * warmth;
        satMix_ = 0.4f * warmth;
        lastTone_[1] = warmth;
    }
    const float tilt = clampf(bright + 0.6f * (inten - 0.5f), 0.0f, 1.0f); // tocar mais forte = mais brilho
    if (std::fabs(tilt - lastTone_[2]) > 1e-4f) {
        tilt_.c.highShelf(2500.0f, 0.5f, -3.0f + 5.0f * tilt, sr_);    // -3..+2 dB, inclinação suave
        lastTone_[2] = tilt;
    }

    // Espaço
    ReverbParams rp;
    rp.decaySeconds = P(ParamId::ReverbDecay);
    rp.size = P(ParamId::ReverbSize);
    rp.damping = P(ParamId::ReverbDamping);
    rp.diffusion = P(ParamId::Diffusion);
    rp.preDelaySeconds = P(ParamId::PreDelay);
    rp.early = P(ParamId::EarlyReflections);
    rp.modulation = 0.35f + 0.4f * P(ParamId::Movement);
    const int type = static_cast<int>(clampf(std::round(params_[static_cast<int>(ParamId::ReverbType)].load()), 0.0f, 2.0f));
    if (type != activeReverb_) {
        activeReverb_ = type;
        reverbs_[static_cast<size_t>(type)].running = true;
        reverbs_[static_cast<size_t>(type)].quietBlocks = 0;
    }
    for (auto& s : reverbs_) if (s.running) s.reverb->setParams(rp);
}

void PadEngine::renderSubBlock(float* out, int n) {
    for (int i = 0; i < n; ++i)
        padL_[i] = padR_[i] = airL_[i] = airR_[i] = shimL_[i] = shimR_[i] = lowL_[i] = lowR_[i] = wetL_[i] = wetR_[i] = 0.0f;
    Buses buses{padL_.data(), padR_.data(), airL_.data(), airR_.data(), shimL_.data(), shimR_.data(), lowL_.data(), lowR_.data()};
    voices_.render(ctx_, buses, n);
    for (int i = 0; i < n; ++i) texL_[i] = texR_[i] = metL_[i] = metR_[i] = 0.0f;
    texture_.render(texL_.data(), texR_.data(), metL_.data(), metR_.data(), n);

    const float invN = 1.0f / static_cast<float>(n);

    // ---- Corpo: passa-alta (espaço para baixo e bumbo) + ensemble de 3 fases
    const float c0 = chorus_, c1 = smooth_[static_cast<int>(ParamId::Chorus)].value();
    for (int i = 0; i < n; ++i) {
        float l = padL_[i], r = padR_[i];
        padHp_.process(l, r);
        float el = 0.0f, er = 0.0f;
        ensemble_.process(l, r, el, er);
        const float c = c0 + (c1 - c0) * static_cast<float>(i + 1) * invN;
        // Mistura de potência constante entre seco e ensemble (sinais pouco correlacionados).
        const float dry = 1.0f - 0.35f * c, wet = 0.75f * c;
        padL_[i] = l * dry + el * wet;
        padR_[i] = r * dry + er * wet;
    }
    chorus_ = c1;

    // ---- Texturas tonais entram no corpo depois do ensemble (o chorus borraria o ataque do
    // piano); pratos passam por passa-alta/passa-baixa fixos do barramento de metal.
    for (int i = 0; i < n; ++i) {
        float ml = metL_[i], mr = metR_[i];
        metalHp_.process(ml, mr);
        metalLp_.process(ml, mr);
        metL_[i] = ml;
        metR_[i] = mr;
    }
    // ---- Fita (Vintage): no corpo e nas texturas tonais, mesma modulação nos dois canais.
    for (int i = 0; i < n; ++i) { padL_[i] += texL_[i] * kTexDry; padR_[i] += texR_[i] * kTexDry; }
    tape_.process(padL_.data(), padR_.data(), n, ctx_.tape, conductor_.out().tapeGust);

    // ---- Envio do reverb: corpo + Air (inteiro) + Shimmer + um pouco de grave, filtrado
    const float shimmer = smooth_[static_cast<int>(ParamId::Shimmer)].value();
    const float shimSend = 0.6f + 0.8f * shimmer;
    for (int i = 0; i < n; ++i) {
        float lo = lowHp_.l.process(lowHp_.c, lowL_[i]);
        lo = lowLp_.l.process(lowLp_.c, lo);
        lowL_[i] = lo;
        float sl = (padL_[i] + airL_[i] + shimL_[i] * shimSend + lo * 0.1f) * sendBoost_ +
                   texL_[i] * kTexSendExtra + metL_[i] * kMetalSend;
        float sr = (padR_[i] + airR_[i] + shimR_[i] * shimSend + lo * 0.1f) * sendBoost_ +
                   texR_[i] * kTexSendExtra + metR_[i] * kMetalSend;
        sendHp_.process(sl, sr);
        sendLp_.process(sl, sr);
        sendL_[i] = sl;
        sendR_[i] = sr;
    }
    shimmer_.addToSend(sendL_.data(), sendR_.data(), n, shimmer * kShimmerFeedback);

    // ---- Reverbs (o ativo recebe o envio; os outros terminam a cauda e saem em rampa)
    const float step = static_cast<float>(n) / (kReverbSwitchSeconds * sr_);
    for (int k = 0; k < static_cast<int>(ReverbType::Count); ++k) {
        ReverbSlot& s = reverbs_[static_cast<size_t>(k)];
        if (!s.running) continue;
        const bool active = (k == activeReverb_);
        const float g0 = s.gain;
        const float g1 = active ? std::min(1.0f, g0 + step) : std::max(0.0f, g0 - step);
        s.gain = g1;
        s.reverb->process(active ? sendL_.data() : zero_.data(), active ? sendR_.data() : zero_.data(), tmpL_.data(), tmpR_.data(), n);
        float e = 0.0f;
        for (int i = 0; i < n; ++i) {
            const float g = g0 + (g1 - g0) * static_cast<float>(i + 1) * invN;
            wetL_[i] += tmpL_[i] * g;
            wetR_[i] += tmpR_[i] * g;
            e += tmpL_[i] * tmpL_[i] + tmpR_[i] * tmpR_[i];
        }
        if (!active && g1 <= 0.0f) {
            // Continua com entrada zero até a cauda ficar abaixo de -100 dB por ~0,5 s,
            // assim os buffers estão limpos se este reverb for escolhido de novo.
            s.quietBlocks = (e < 1e-10f * n) ? s.quietBlocks + 1 : 0;
            if (s.quietBlocks > static_cast<int>(0.5f * sr_ / kSubBlock)) s.running = false;
        }
    }
    shimmer_.storeWet(wetL_.data(), wetR_.data(), n);

    // ---- Mistura final
    const float rv = smooth_[static_cast<int>(ParamId::Reverb)].value();
    const float dry1 = 1.0f - 0.3f * rv;
    const float wet1 = 1.3f * rv;
    const float width1 = smooth_[static_cast<int>(ParamId::Width)].value() + baseWidthBoost_;
    const float mv = smooth_[static_cast<int>(ParamId::MasterVolume)].value();
    const float master1 = mv * mv * presetTrim_.load(std::memory_order_relaxed);
    const float dry0 = dryGain_, wet0 = wetGain_, width0 = width_, master0 = master_;
    float peak = 0.0f;

    for (int i = 0; i < n; ++i) {
        const float k = static_cast<float>(i + 1) * invN;
        float l = padL_[i] + airL_[i] * kAirDirect + shimL_[i] + lowL_[i] + metL_[i] * kMetalDry;
        float r = padR_[i] + airR_[i] * kAirDirect + shimR_[i] + lowL_[i] + metR_[i] * kMetalDry;
        lowShelf_.process(l, r);
        warmBell_.process(l, r);
        warmHighCut_.process(l, r);
        tilt_.process(l, r);
        if (satMix_ > 0.0f) {
            l += satMix_ * (softSat(l * satDrive_) / satDrive_ - l);
            r += satMix_ * (softSat(r * satDrive_) / satDrive_ - r);
        }
        l = dcL_.hp(l);
        r = dcR_.hp(r);

        const float dry = dry0 + (dry1 - dry0) * k;
        const float wet = wet0 + (wet1 - wet0) * k;
        const float ol = (l * dry + wetL_[i] * wet) * kHeadroom;
        const float orr = (r * dry + wetR_[i] * wet) * kHeadroom;

        // Largura M/S com graves sempre mono (abaixo de ~140 Hz o lado é removido).
        // As fontes da v0.2 já saem quase descorrelacionadas (leituras L/R independentes do
        // PADsynth + detune), então a largura "natural" (1,0) usa 80% do lado e, acima de 1, o
        // lado cresce pela metade. Correlação L/R típica ~0,2: largo, mas sem perder corpo
        // quando a mesa ou a caixa da igreja somam em mono.
        const float wl = width0 + (width1 - width0) * k;
        const float w = wl <= 1.0f ? 0.8f * wl : 0.8f + 0.5f * (wl - 1.0f);
        const float m = 0.5f * (ol + orr);
        float s = 0.5f * (ol - orr);
        s -= sideLp_.lp(s);
        s *= w;
        tmpL_[i] = m + s;
        tmpR_[i] = m - s;
    }
    glue_.process(tmpL_.data(), tmpR_.data(), n);
    for (int i = 0; i < n; ++i) {
        const float g = master0 + (master1 - master0) * static_cast<float>(i + 1) * invN;
        float ol = tmpL_[i] * g, orr = tmpR_[i] * g;
        limiter_.process(ol, orr);
        out[2 * i] = ol;
        out[2 * i + 1] = orr;
        peak = std::max(peak, std::max(std::fabs(ol), std::fabs(orr)));
    }
    dryGain_ = dry1; wetGain_ = wet1; width_ = width1; master_ = master1;
    peakAcc_ = std::max(peakAcc_, peak);
}

void PadEngine::process(float* out, int frames) {
    if (frames <= 0) return;
    if (!prepared_.load(std::memory_order_acquire)) {
        for (int i = 0; i < frames * 2; ++i) out[i] = 0.0f;
        return;
    }
    ScopedNoDenormals noDenormals;
    const auto t0 = std::chrono::steady_clock::now();

    handleCommands();

    int done = 0;
    while (done < frames) {
        const int n = std::min(kSubBlock, frames - done);
        updateControl();
        renderSubBlock(out + 2 * done, n);
        done += n;
    }

    // Uso das fontes (o carregador só libera o que nenhuma voz usa).
    voices_.countSourceUsage(usage_.data());
    texture_.countSourceUsage(usage_.data());
    sources_.publishUsage(usage_.data());

    statVoices_.store(voices_.activeCount(), std::memory_order_relaxed);
    statSteals_.store(voices_.stealCount(), std::memory_order_relaxed);
    statHardSteals_.store(voices_.hardStealCount(), std::memory_order_relaxed);
    statPlaying_.store(voices_.isPlaying(), std::memory_order_relaxed);
    statTexVoices_.store(texture_.activeCount(), std::memory_order_relaxed);
    statTexEvents_.store(conductor_.noteEvents() + conductor_.metalEvents(), std::memory_order_relaxed);
    statBlooms_.store(conductor_.blooms(), std::memory_order_relaxed);
    statSwells_.store(conductor_.swells(), std::memory_order_relaxed);
    statStage_.store(static_cast<int>(conductor_.out().stage), std::memory_order_relaxed);
    atomicMax(statPeak_, peakAcc_);
    atomicMin(statLimiter_, limiter_.takeMinGain());
    atomicMax(statGlue_, glue_.takeMaxReductionDb());
    peakAcc_ = 0.0f;
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    const double budget = static_cast<double>(frames) / sr_;
    const float inst = static_cast<float>(elapsed / budget);
    loadEma_ += (inst - loadEma_) * 0.05f;
    statLoad_.store(loadEma_, std::memory_order_relaxed);
    atomicMax(statLoadPeak_, inst);
}

} // namespace pad
