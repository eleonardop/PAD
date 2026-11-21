#include "PadEngine.h"
#include <chrono>

namespace pad {

namespace {
constexpr float kHeadroom = 0.55f;          // ganho interno antes do limitador (~-5,2 dB)
constexpr float kLimiterThreshold = 0.891f; // -1 dBFS

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

PadEngine::~PadEngine() {
    delete pendingBank_.exchange(nullptr);
    delete retiredBank_.exchange(nullptr);
}

bool PadEngine::prepare(float sampleRate) {
    if (sampleRate < 8000.0f || sampleRate > 192000.0f) return false;
    prepared_.store(false, std::memory_order_release);
    sr_ = sampleRate;

    // Wavetables (aditivas, limitadas em banda para esta taxa).
    auto tables = std::make_unique<LayerTables>();
    tables->warmA.build([](int h) { return 1.0f / std::pow(static_cast<float>(h), 1.05f); }, sr_, 11, true);
    tables->warmB.build([](int h) { return ((h & 1) ? 1.0f : 0.35f) / std::pow(static_cast<float>(h), 1.05f); }, sr_, 23, true);
    tables->shimmer.build([](int h) { return h == 1 ? 1.0f : h == 2 ? 0.18f : h == 3 ? 0.06f : h == 4 ? 0.02f : 0.0f; }, sr_, 37, false);
    tables->low.build([](int h) { return h == 1 ? 1.0f : h == 2 ? 0.30f : h == 3 ? 0.10f : h == 4 ? 0.03f : 0.0f; }, sr_, 41, false);
    tables_ = std::move(tables);

    // Banco de textura de cordas.
    auto bank = std::make_unique<SampleBank>();
    bank->bakeStringTexture(sr_);
    bank_ = std::move(bank);

    ctx_ = RenderContext{};
    ctx_.sampleRate = sr_;
    ctx_.controlRate = sr_ / kSubBlock;
    ctx_.tables = tables_.get();
    ctx_.bank = bank_.get();

    voices_.prepare(sr_);

    for (int i = 0; i < kParamCount; ++i) {
        const ParamInfo& info = paramInfo(static_cast<ParamId>(i));
        smooth_[static_cast<size_t>(i)].setTime(info.smoothingSeconds, ctx_.controlRate);
        smooth_[static_cast<size_t>(i)].snap(params_[static_cast<size_t>(i)].load());
    }

    padHp_.c.highpass(70.0f, 0.707f, sr_);
    lowLp_.c.lowpass(260.0f, 0.707f, sr_);
    lowHp_.c.highpass(28.0f, 0.707f, sr_);
    padHp_.reset(); lowLp_.reset(); lowHp_.reset();
    dcL_.setLowpass(8.0f, sr_); dcR_.setLowpass(8.0f, sr_); dcL_.reset(); dcR_.reset();
    lowShelf_.reset(); warmBell_.reset(); warmHighCut_.reset(); brightShelf_.reset(); airShelf_.reset();
    for (float& t : lastTone_) t = -1.0f;

    reverb_.prepare(sr_);
    limiter_.prepare(sr_, 3.0f, 180.0f);
    limiter_.setThreshold(kLimiterThreshold);

    // Esvazia comandos antigos.
    Command c;
    while (commands_.pop(c)) {}

    updateControl();
    dryGain_ = 1.0f; wetGain_ = 0.0f;
    prepared_.store(true, std::memory_order_release);
    return true;
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

void PadEngine::setParam(ParamId id, float value) {
    const int i = static_cast<int>(id);
    if (i < 0 || i >= kParamCount) return;
    const ParamInfo& info = paramInfo(id);
    if (!std::isfinite(value)) return;
    params_[static_cast<size_t>(i)].store(clampf(value, info.minValue, info.maxValue), std::memory_order_relaxed);
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
        setParam(static_cast<ParamId>(i), p.values[i]);
    }
}

void PadEngine::replaceSampleBank(std::unique_ptr<SampleBank> bank) {
    collectGarbage();
    delete pendingBank_.exchange(bank.release());
}

void PadEngine::collectGarbage() { delete retiredBank_.exchange(nullptr, std::memory_order_acq_rel); }

EngineStats PadEngine::readStats() {
    EngineStats s;
    s.activeVoices = statVoices_.load(std::memory_order_relaxed);
    s.steals = statSteals_.load(std::memory_order_relaxed);
    s.hardSteals = statHardSteals_.load(std::memory_order_relaxed);
    s.droppedCommands = statDropped_.load(std::memory_order_relaxed);
    s.limiterMinGain = statLimiter_.exchange(1.0f, std::memory_order_relaxed);
    s.peakOut = statPeak_.exchange(0.0f, std::memory_order_relaxed);
    s.load = statLoad_.load(std::memory_order_relaxed);
    s.currentKey = uiKey_.load();
    s.currentChord = uiChord_.load();
    s.playing = statPlaying_.load(std::memory_order_relaxed);
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
                break;
            }
            case CmdType::Stop: voices_.stopAll(getParam(ParamId::Release)); break;
            case CmdType::Panic: voices_.panic(); break;
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
    ctx_.layerAir = P(ParamId::LayerAir);
    ctx_.layerStrings = P(ParamId::LayerStrings);
    ctx_.layerShimmer = P(ParamId::LayerShimmer);
    ctx_.layerLow = P(ParamId::LayerLow);
    ctx_.brightness = P(ParamId::Brightness);
    ctx_.air = P(ParamId::Air);
    ctx_.movement = P(ParamId::Movement);
    ctx_.detune = P(ParamId::Detune);

    // Filtros de tom: recalcula só quando o valor muda.
    const float low = P(ParamId::Low), warmth = P(ParamId::Warmth), bright = P(ParamId::Brightness), air = P(ParamId::Air);
    const float tone[6] = {low, warmth, bright, air, 0, 0};
    if (std::fabs(tone[0] - lastTone_[0]) > 1e-4f) {
        const float db = low < 0.5f ? (low - 0.5f) * 24.0f : (low - 0.5f) * 10.0f; // -12..+5 dB
        lowShelf_.c.lowShelf(140.0f, 0.7f, db, sr_);
        lastTone_[0] = tone[0];
    }
    if (std::fabs(tone[1] - lastTone_[1]) > 1e-4f) {
        warmBell_.c.bell(320.0f, 0.8f, warmth * 4.0f - 0.5f, sr_);      // -0,5..+3,5 dB
        warmHighCut_.c.highShelf(6500.0f, 0.7f, -warmth * 4.0f, sr_);   //  0..-4 dB
        satDrive_ = 1.0f + 1.8f * warmth;
        satMix_ = 0.6f * warmth;
        lastTone_[1] = tone[1];
    }
    if (std::fabs(tone[2] - lastTone_[2]) > 1e-4f) {
        brightShelf_.c.highShelf(3000.0f, 0.6f, -7.0f + 12.0f * bright, sr_); // -7..+5 dB
        lastTone_[2] = tone[2];
    }
    if (std::fabs(tone[3] - lastTone_[3]) > 1e-4f) {
        airShelf_.c.highShelf(11000.0f, 0.6f, -2.0f + 6.0f * air, sr_);      // -2..+4 dB
        lastTone_[3] = tone[3];
    }

    reverb_.setParams(P(ParamId::ReverbDecay), P(ParamId::ReverbSize), P(ParamId::ReverbDamping), P(ParamId::Shimmer));
}

void PadEngine::renderSubBlock(float* out, int n) {
    for (int i = 0; i < n; ++i) padL_[i] = padR_[i] = shimL_[i] = shimR_[i] = lowL_[i] = lowR_[i] = 0.0f;
    Buses buses{padL_.data(), padR_.data(), shimL_.data(), shimR_.data(), lowL_.data(), lowR_.data()};
    voices_.render(ctx_, buses, n);

    // Ganhos de saída interpolados por amostra (sem zipper).
    const float rv = smooth_[static_cast<int>(ParamId::Reverb)].value();
    const float dry1 = 1.0f - 0.35f * rv;
    const float wet1 = 1.25f * rv;
    const float width1 = smooth_[static_cast<int>(ParamId::Width)].value();
    const float mv = smooth_[static_cast<int>(ParamId::MasterVolume)].value();
    const float master1 = mv * mv * kHeadroom;
    const float shimSend = 0.6f + 0.8f * smooth_[static_cast<int>(ParamId::Shimmer)].value();
    const float dry0 = dryGain_, wet0 = wetGain_, width0 = width_, master0 = master_;
    const float invN = 1.0f / static_cast<float>(n);
    float peak = 0.0f;

    for (int i = 0; i < n; ++i) {
        const float k = static_cast<float>(i + 1) * invN;
        float l = padL_[i], r = padR_[i];
        padHp_.process(l, r); // abre espaço para baixo e bumbo
        float lo = lowL_[i];
        lo = lowHp_.l.process(lowHp_.c, lo);
        lo = lowLp_.l.process(lowLp_.c, lo);
        l += lo; r += lo;

        lowShelf_.process(l, r);
        warmBell_.process(l, r);
        warmHighCut_.process(l, r);
        brightShelf_.process(l, r);
        airShelf_.process(l, r);
        if (satMix_ > 0.0f) {
            l += satMix_ * (softSat(l * satDrive_) / satDrive_ - l);
            r += satMix_ * (softSat(r * satDrive_) / satDrive_ - r);
        }
        // Bloqueio de DC: a saturação (ímpar) em formas de onda assimétricas gera um pequeno offset.
        l = dcL_.hp(l);
        r = dcR_.hp(r);

        float wl = 0.0f, wr = 0.0f;
        reverb_.process(l + shimL_[i] * shimSend, r + shimR_[i] * shimSend, wl, wr);

        const float dry = dry0 + (dry1 - dry0) * k;
        const float wet = wet0 + (wet1 - wet0) * k;
        float ol = l * dry + wl * wet;
        float orr = r * dry + wr * wet;

        const float w = width0 + (width1 - width0) * k;
        const float m = 0.5f * (ol + orr);
        const float s = 0.5f * (ol - orr) * w;
        const float g = master0 + (master1 - master0) * k;
        ol = (m + s) * g;
        orr = (m - s) * g;

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

    // Troca de banco de samples: só sem vozes (as vozes guardam ponteiros para zonas) e
    // depois que o banco anterior já foi recolhido fora do callback (nunca há delete aqui).
    if (pendingBank_.load(std::memory_order_acquire) != nullptr &&
        retiredBank_.load(std::memory_order_acquire) == nullptr && voices_.activeCount() == 0) {
        SampleBank* nb = pendingBank_.exchange(nullptr, std::memory_order_acq_rel);
        if (nb) {
            SampleBank* old = bank_.release();
            bank_.reset(nb);
            ctx_.bank = nb;
            retiredBank_.store(old, std::memory_order_release);
        }
    }

    handleCommands();

    int done = 0;
    while (done < frames) {
        const int n = std::min(kSubBlock, frames - done);
        updateControl();
        renderSubBlock(out + 2 * done, n);
        done += n;
    }

    // Telemetria (sem bloqueio).
    statVoices_.store(voices_.activeCount(), std::memory_order_relaxed);
    statSteals_.store(voices_.stealCount(), std::memory_order_relaxed);
    statHardSteals_.store(voices_.hardStealCount(), std::memory_order_relaxed);
    statPlaying_.store(voices_.isPlaying(), std::memory_order_relaxed);
    atomicMax(statPeak_, peakAcc_);
    atomicMin(statLimiter_, limiter_.takeMinGain());
    peakAcc_ = 0.0f;
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    const double budget = static_cast<double>(frames) / sr_;
    loadEma_ += (static_cast<float>(elapsed / budget) - loadEma_) * 0.05f;
    statLoad_.store(loadEma_, std::memory_order_relaxed);
}

} // namespace pad
