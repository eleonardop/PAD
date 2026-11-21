#include "Conductor.h"
#include <cmath>

namespace pad {

namespace {
float smoothstep(float a, float b, float x) {
    if (b <= a) return x >= b ? 1.0f : 0.0f;
    const float t = clampf((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
bool isMinor(ChordType t) { return t == ChordType::Minor || t == ChordType::Minor7 || t == ChordType::MinorAdd9; }
bool allowsNinth(ChordType t) {
    switch (t) {
        case ChordType::Pad5: case ChordType::Major: case ChordType::Minor: case ChordType::Sus2:
        case ChordType::Add9: case ChordType::MinorAdd9: case ChordType::Major7: return true;
        default: return false;
    }
}
uint16_t maskOf(int rootPc, ChordType t) {
    const Voicing v = buildVoicing(rootPc, t);
    uint16_t m = 0;
    for (int i = 0; i < v.count; ++i) m = static_cast<uint16_t>(m | (1u << (v.notes[i] % 12)));
    return m;
}
int popcount12(uint16_t m) { int c = 0; for (int i = 0; i < 12; ++i) c += (m >> i) & 1; return c; }
// Posição no círculo de quintas; menores usam a relativa maior (Em ~ G), como no Tonnetz.
int fifthsPos(int rootPc, ChordType t) {
    const int r = isMinor(t) ? (rootPc + 3) % 12 : rootPc;
    return (r * 7) % 12;
}
} // namespace

float Conductor::harmonicDistance(int rootA, ChordType ta, int rootB, ChordType tb) {
    const uint16_t a = maskOf(rootA, ta), b = maskOf(rootB, tb);
    const int na = popcount12(a), nb = popcount12(b);
    const float common = static_cast<float>(popcount12(static_cast<uint16_t>(a & b))) / static_cast<float>(std::max(1, std::min(na, nb)));
    int c = std::abs(fifthsPos(rootA, ta) - fifthsPos(rootB, tb));
    c = std::min(c, 12 - c);
    const float mode = (isMinor(ta) != isMinor(tb)) ? 1.0f : 0.0f;
    return clampf(0.5f * (1.0f - common) + 0.35f * (static_cast<float>(c) / 6.0f) + 0.15f * mode, 0.0f, 1.0f);
}

void Conductor::prepare(float controlRate, uint64_t seed) {
    dt_ = 1.0f / controlRate;
    rng_.setSeed(seed);
    reset();
}

void Conductor::reset() {
    now_ = 0.0;
    playing_ = hasChord_ = chordPending_ = swellPending_ = swellActive_ = false;
    age_ = 0.0f;
    nextTexAt_ = nextMetalAt_ = 1e30;
    lastBloomAt_ = bloomStart_ = -1e30;
    bloomStrength_ = 0.0f;
    out_ = ConductorOut{};
}

void Conductor::onChord(const Voicing& v, ChordType type, int rootPc) {
    (void)v;
    prevRootPc_ = rootPc_;
    prevType_ = type_;
    rootPc_ = ((rootPc % 12) + 12) % 12;
    type_ = type;
    pcMask_ = maskOf(rootPc_, type_);
    chordPending_ = true; // o bloom é decidido no próximo tick, com os parâmetros atuais
}

void Conductor::onStop() {
    playing_ = false;
    chordPending_ = false;
    nextTexAt_ = nextMetalAt_ = 1e30;
    swellPending_ = false;
}

void Conductor::onSwell() { swellPending_ = true; }

void Conductor::logNote(int midi, uint8_t instrument) {
    NoteLogEntry& e = log_[static_cast<size_t>(logCount_ % kLog)];
    e.time = static_cast<float>(now_ - chordStart_);
    e.midi = static_cast<int16_t>(midi);
    e.rootPc = static_cast<int8_t>(rootPc_);
    e.chordType = static_cast<int8_t>(type_);
    e.instrument = instrument;
    ++logCount_;
}

// Escolhe uma nota do acorde em [lo, hi], perto da anterior (linha melódica que respira).
int Conductor::chooseNote(int lo, int hi, bool allowNinth, int center) {
    uint16_t mask = pcMask_;
    if (allowNinth && allowsNinth(type_)) mask = static_cast<uint16_t>(mask | (1u << ((rootPc_ + 2) % 12)));
    int cand[40];
    float w[40];
    int n = 0;
    const int ref = (lastNote_ >= lo && lastNote_ <= hi) ? lastNote_ : center;
    for (int m = lo; m <= hi && n < 40; ++m) {
        if (!((mask >> (m % 12)) & 1)) continue;
        const int iv = std::abs(m - ref);
        if (iv > 7) continue;
        float weight;
        if (iv == 0) weight = repeatCount_ >= 2 ? 0.0f : 0.25f;
        else if (iv <= 2) weight = 1.0f;
        else if (iv <= 5) weight = 0.8f;
        else weight = 0.35f;
        if (weight <= 0.0f) continue;
        cand[n] = m;
        w[n] = weight;
        ++n;
    }
    if (n == 0) {
        // Nenhuma nota perto: a nota do acorde mais próxima do centro do registro.
        int best = -1, bd = 99;
        for (int m = lo; m <= hi; ++m)
            if (((mask >> (m % 12)) & 1) && std::abs(m - center) < bd) { bd = std::abs(m - center); best = m; }
        return best;
    }
    float tot = 0.0f;
    for (int i = 0; i < n; ++i) tot += w[i];
    float r = rng_.uniform() * tot;
    for (int i = 0; i < n; ++i) { r -= w[i]; if (r <= 0.0f) return cand[i]; }
    return cand[n - 1];
}

float Conductor::nextInterval(const RenderContext& ctx) {
    // Média de 11 s (atividade baixa) a 3 s (alta); Evolução alta deixa um pouco mais denso.
    float mean = 11.0f + (3.0f - 11.0f) * clampf(ctx.activity, 0.0f, 1.0f);
    mean *= 1.3f - 0.5f * clampf(ctx.evolution, 0.0f, 1.0f);
    switch (out_.stage) {
        case LifeStage::Establishment: mean *= 1.6f; break;
        case LifeStage::Sustain: mean *= 1.3f; break; // vivo, mas sem cansar
        default: break;
    }
    // Log-normal aproximada (soma de 4 uniformes): sem periodicidade perceptível.
    const float g = (rng_.uniform() + rng_.uniform() + rng_.uniform() + rng_.uniform() - 2.0f) * 1.7f;
    return std::max(1.8f, mean * std::exp(0.45f * g));
}

void Conductor::textureNote(const RenderContext& ctx, const SourceLibrary& lib, TexturePlayer& tex) {
    const float keys = ctx.layerKeys, sparkle = ctx.layerSparkle;
    if (keys <= 1e-3f && sparkle <= 1e-3f) return;
    // Brilho só depois que o acorde se estabeleceu (ou se for a única textura do preset).
    const float sparkleW = sparkle * ((out_.stage >= LifeStage::Expansion || keys <= 1e-3f) ? 1.0f : 0.0f);
    const bool useSparkle = rng_.uniform() * (keys + sparkleW) < sparkleW;
    const bool ninth = ctx.evolution > 0.4f;
    const float vel = 0.7f + 0.3f * rng_.uniform();
    // Pan complementar: lado oposto ao último evento na maior parte das vezes.
    if (rng_.uniform() < 0.8f) lastPanSide_ = -lastPanSide_;
    const float drift = 0.15f * std::sin(static_cast<float>(now_) * (useSparkle ? 0.0967f : 0.157f));
    const float pan = clampf(lastPanSide_ * (0.25f + 0.45f * rng_.uniform()) + drift, -0.8f, 0.8f);

    TexEvent e;
    e.pan = pan;
    e.hold = -1.0f;
    if (useSparkle) {
        const int m = chooseNote(72, 88, ninth, 81);
        if (m < 0) return;
        e.source = SourceId::Vibes;
        e.midi = m;
        e.level = 0.30f * sparkle * vel;
        e.lowpassHz = 5500.0f + 2000.0f * ctx.brightness;
        e.highpassHz = 300.0f;
        e.release = 2.0f;
        e.priority = 0;
        repeatCount_ = (m == lastNote_) ? repeatCount_ + 1 : 0;
        lastNote_ = m;
        logNote(m, 1);
        tex.trigger(e, lib);
    } else {
        const int m = chooseNote(64, 86, false, 72);
        if (m < 0) return;
        e.source = ctx.keysType == 0 ? SourceId::PianoSoft : SourceId::EpTine;
        e.midi = m;
        e.level = 0.42f * keys * vel;
        // Piano "felt": agudos bem fechados; EP um pouco mais aberto.
        e.lowpassHz = ctx.keysType == 0 ? 1800.0f + 2400.0f * ctx.brightness : 3000.0f + 2500.0f * ctx.brightness;
        e.highpassHz = 120.0f;
        e.release = 2.5f;
        e.priority = 1;
        repeatCount_ = (m == lastNote_) ? repeatCount_ + 1 : 0;
        lastNote_ = m;
        logNote(m, 0);
        tex.trigger(e, lib);
        // Microvariação: com Evolução alta, às vezes uma segunda nota do acorde por baixo
        // (oitava ou uma nota do acorde entre 3 e 7 semitons abaixo), mais suave.
        if (ctx.evolution > 0.55f && rng_.uniform() < 0.2f * ctx.evolution) {
            int second = -1;
            for (int iv : {12, 7, 5, 4, 3}) {
                const int s = m - iv;
                if (s >= 64 && ((pcMask_ >> (s % 12)) & 1)) { second = s; break; }
            }
            if (second > 0) {
                e.midi = second;
                e.level *= 0.6f;
                e.pan = clampf(-pan * 0.5f, -0.8f, 0.8f);
                logNote(second, 0);
                tex.trigger(e, lib);
            }
        }
    }
    ++noteEvents_;
    ++notesThisChord_;
}

void Conductor::startBloom(const RenderContext& ctx, const SourceLibrary& lib, TexturePlayer& tex, float strength, float d) {
    ++blooms_;
    lastBloomAt_ = now_;
    bloomStart_ = now_;
    bloomStrength_ = strength;
    // Swell harmônico: notas do novo acorde no registro alto, timbre Glass, entrando por baixo.
    const int notes[3] = {rootPc_, (rootPc_ + 7) % 12, isMinor(type_) ? (rootPc_ + 3) % 12 : (rootPc_ + 4) % 12};
    const bool ambiguous = (type_ == ChordType::Pad5 || type_ == ChordType::Sus2 || type_ == ChordType::Sus4);
    const int count = ambiguous ? 2 : 3;
    for (int k = 0; k < count; ++k) {
        int m = 72 + ((notes[k] - 72 % 12) + 12) % 12; // primeira ocorrência a partir de Dó5
        if (k == 1 && m < 76) m += 12;
        TexEvent e;
        e.source = SourceId::Glass;
        e.midi = m;
        e.level = 0.05f * strength;
        e.pan = (k == 0 ? 0.0f : (k == 1 ? -0.45f : 0.45f));
        e.attack = 0.9f;
        e.attackCurve = 2.0f;
        e.hold = 0.8f;
        e.release = 3.5f;
        e.lowpassHz = 6000.0f;
        e.highpassHz = 400.0f;
        e.priority = 2;
        e.group = 2;
        tex.trigger(e, lib);
    }
    // Prato: só em mudanças grandes e blooms fortes, e sempre por baixo.
    if (ctx.layerMetal > 1e-3f && strength > 0.45f && d > 0.3f) {
        TexEvent e;
        e.source = SourceId::Cymbal;
        e.kind = "wash";
        e.kindIndex = static_cast<int>(rng_.uniform() * 2.0f);
        e.level = 0.30f * ctx.layerMetal * strength;
        e.pan = 0.3f * rng_.bipolar();
        e.attack = 1.2f;
        e.attackCurve = 1.5f;
        e.hold = 1.5f;
        e.release = 4.0f;
        e.lowpassHz = 4500.0f + 1500.0f * ctx.brightness;
        e.highpassHz = 400.0f;
        e.bus = TexBus::Metal;
        e.priority = 2;
        e.group = 2;
        if (tex.trigger(e, lib)) ++metalEvents_;
    }
}

void Conductor::startSwell(const RenderContext& ctx, const SourceLibrary& lib, TexturePlayer& tex) {
    ++swells_;
    const float T = clampf(ctx.swellTime, 1.5f, 8.0f);
    swellActive_ = true;
    swellEnd_ = now_ + T;
    const float metal = std::max(ctx.layerMetal, 0.5f);
    // Rulo crescente real: começa de modo que o pico caia em T.
    const char* kind = T < 3.0f ? "swell2" : (T < 5.5f ? "swell4" : "swell7");
    const ZoneBank* cym = lib.get(SourceId::Cymbal);
    if (cym) {
        const int idx = static_cast<int>(rng_.uniform() * 2.0f);
        const Zone* z = cym->kindAt(kind, idx);
        if (z) {
            TexEvent e;
            e.source = SourceId::Cymbal;
            e.kind = kind;
            e.kindIndex = idx;
            const float pk = static_cast<float>(z->peakFrame) / z->sampleRate;
            e.startOffset = std::max(0.0f, pk - T);
            e.delay = std::max(0.0f, T - pk);
            e.level = 0.38f * metal;
            e.attack = 0.3f;
            e.release = 3.0f;
            e.lowpassHz = 5000.0f + 1000.0f * ctx.brightness;
            e.highpassHz = 350.0f;
            e.bus = TexBus::Metal;
            e.priority = 3;
            if (tex.trigger(e, lib)) ++metalEvents_;
        }
    }
    const ZoneBank* rev = lib.get(SourceId::CymbalRev);
    if (rev && rev->size() > 0) {
        const Zone* z = rev->zoneAt(static_cast<size_t>(rng_.uniform() * static_cast<float>(rev->size())) % rev->size());
        TexEvent e;
        e.source = SourceId::CymbalRev;
        e.kind = "rev";
        e.kindIndex = static_cast<int>(rev->indexOf(z));
        const float pk = static_cast<float>(z->peakFrame) / z->sampleRate;
        e.startOffset = std::max(0.0f, pk - T);
        e.delay = std::max(0.0f, T - pk);
        e.level = 0.22f * metal;
        e.attack = 0.2f;
        e.release = 1.5f;
        e.lowpassHz = 6000.0f;
        e.highpassHz = 400.0f;
        e.bus = TexBus::Metal;
        e.priority = 3;
        tex.trigger(e, lib);
    }
    // Piano (ou EP) invertido na fundamental e na quinta: as notas mais estáveis do acorde.
    const SourceId rs = ctx.keysType == 0 ? SourceId::PianoSoftRev : SourceId::EpTineRev;
    const ZoneBank* kb = lib.get(rs);
    if (kb) {
        int root = 64 + ((rootPc_ - 64 % 12) + 12) % 12;
        if (root < 67) root += 12;
        const int fifth = root + 7 <= 86 ? root + 7 : root - 5;
        const float keys = ctx.layerKeys > 1e-3f ? ctx.layerKeys : 0.6f;
        for (int k = 0; k < 2; ++k) {
            const int m = k == 0 ? root : fifth;
            double ratio = 1.0;
            const Zone* z = kb->nearest(static_cast<float>(440.0 * std::pow(2.0, (m - 69) / 12.0)), ctx.sampleRate, ratio);
            if (!z) continue;
            TexEvent e;
            e.source = rs;
            e.midi = m;
            // O reverse toca mais rápido ou mais devagar conforme a transposição: o tempo até o
            // pico na saída é peakFrame / (taxa · razão).
            const float peakOut = static_cast<float>(z->peakFrame) / (z->sampleRate * static_cast<float>(ratio));
            e.startOffset = std::max(0.0f, (peakOut - T) * static_cast<float>(ratio));
            e.delay = std::max(0.0f, T - peakOut);
            e.level = 0.40f * keys;
            e.pan = k == 0 ? -0.3f : 0.3f;
            e.attack = 0.05f;
            e.release = 1.2f;
            e.lowpassHz = 2500.0f + 2000.0f * ctx.brightness;
            e.highpassHz = 150.0f;
            e.priority = 3;
            e.group = 1;
            tex.trigger(e, lib);
        }
    }
    // Swell harmônico (Glass): cresce durante todo o SwellTime.
    for (int k = 0; k < 2; ++k) {
        int m = 72 + ((((k == 0 ? rootPc_ : rootPc_ + 7) % 12) - 72 % 12) + 12) % 12;
        TexEvent e;
        e.source = SourceId::Glass;
        e.midi = m;
        e.level = 0.06f;
        e.pan = k == 0 ? 0.35f : -0.35f;
        e.attack = T;
        e.attackCurve = 2.5f;
        e.hold = 0.3f;
        e.release = 2.5f;
        e.lowpassHz = 6000.0f;
        e.highpassHz = 400.0f;
        e.priority = 3;
        e.group = 1;
        tex.trigger(e, lib);
    }
}

void Conductor::tick(const RenderContext& ctx, const SourceLibrary& lib, TexturePlayer& tex) {
    now_ += dt_;
    const float evo = clampf(ctx.evolution, 0.0f, 1.0f);

    if (chordPending_) {
        chordPending_ = false;
        const bool fromSilence = !playing_ || !hasChord_;
        const float d = fromSilence ? 0.6f : harmonicDistance(prevRootPc_, prevType_, rootPc_, type_);
        const float prevDur = fromSilence ? 10.0f : static_cast<float>(now_ - chordStart_);
        lastDistance_ = d;
        lastPrevDuration_ = prevDur;
        // A idade do organismo não volta a zero: mudança pequena mantém o ambiente.
        age_ = fromSilence ? 0.0f : age_ * (1.0f - d);
        playing_ = true;
        hasChord_ = true;
        chordStart_ = now_;
        notesThisChord_ = 0;
        const bool swellWasActive = swellActive_ && now_ < swellEnd_ + 0.5;
        if (swellWasActive) {
            // Nenhuma nota do acorde antigo fica soando contra o novo; o prato continua.
            tex.releaseGroup(1, 0.4f);
            swellActive_ = false;
        }
        if (d > 0.0f) {
            const float durFactor = smoothstep(1.0f, 4.0f, prevDur);
            float strength = ctx.bloom * (0.35f + 0.65f * d) * durFactor;
            const float chance = ctx.bloomChance * (0.4f + 0.6f * d);
            const bool gap = (now_ - lastBloomAt_) > 4.0;
            bool fire = strength > 0.05f && gap && rng_.uniform() < chance;
            if (swellWasActive) { fire = true; strength = std::max(strength, 0.7f * std::max(ctx.bloom, 0.5f)); }
            if (fire) startBloom(ctx, lib, tex, strength, d);
        }
        // Primeira nota de textura: nunca no instante da troca.
        nextTexAt_ = ctx.activity > 1e-3f ? now_ + 0.6 + 1.9 * rng_.uniform() : 1e30;
        if (nextMetalAt_ > now_ + 120.0) nextMetalAt_ = now_ + 20.0 + 30.0 * rng_.uniform();
    }
    if (swellPending_) {
        swellPending_ = false;
        if (playing_ && !swellActive_) startSwell(ctx, lib, tex);
    }
    if (swellActive_ && now_ > swellEnd_ + 0.5) swellActive_ = false; // pousou no acorde atual

    if (playing_) {
        age_ += dt_;
        // Escala de tempo dos estados: Evolução alta = vida mais rápida.
        const float sc = 1.6f - 0.9f * evo;
        const float est = 4.0f * sc, expEnd = 14.0f * sc, trEnd = 40.0f * sc;
        out_.stage = age_ < est ? LifeStage::Establishment
                   : age_ < expEnd ? LifeStage::Expansion
                   : age_ < trEnd ? LifeStage::Transformation : LifeStage::Sustain;
        out_.age = age_;

        if (now_ >= nextTexAt_) {
            const bool allowed = out_.stage != LifeStage::Establishment || notesThisChord_ == 0;
            if (allowed && ctx.activity > 1e-3f) textureNote(ctx, lib, tex);
            nextTexAt_ = ctx.activity > 1e-3f ? now_ + nextInterval(ctx) : 1e30;
        } else if (ctx.activity > 1e-3f && nextTexAt_ > 1e29) {
            nextTexAt_ = now_ + nextInterval(ctx);
        }
        // Prato com arco muito baixo em acordes longos (metallic shimmer), raro.
        if (now_ >= nextMetalAt_) {
            if (ctx.layerMetal > 1e-3f && evo > 0.4f && out_.stage >= LifeStage::Transformation) {
                TexEvent e;
                e.source = SourceId::Cymbal;
                e.kind = "bow";
                e.kindIndex = static_cast<int>(rng_.uniform() * 4.0f);
                e.level = 0.16f * ctx.layerMetal;
                e.pan = 0.5f * rng_.bipolar();
                e.attack = 2.0f;
                e.attackCurve = 1.5f;
                e.release = 4.0f;
                e.lowpassHz = 5000.0f;
                e.highpassHz = 500.0f;
                e.bus = TexBus::Metal;
                e.priority = 2;
                if (tex.trigger(e, lib)) ++metalEvents_;
            }
            nextMetalAt_ = now_ + 25.0 + 35.0 * rng_.uniform();
        }
    } else {
        out_.stage = hasChord_ ? LifeStage::Dissolve : LifeStage::Silent;
    }

    // ---- Organismo: alvos das camadas e do espaço, suavizados (τ ≈ 1,5 s)
    const float sc = 1.6f - 0.9f * evo;
    const float expansion = playing_ ? smoothstep(4.0f * sc, 14.0f * sc, age_) : 0.0f;
    const float transformation = playing_ ? smoothstep(14.0f * sc, 40.0f * sc, age_) : 0.0f;
    const float slow = 0.5f + 0.5f * std::sin(static_cast<float>(now_) * 0.1257f); // ~50 s
    float bloomEnv = 0.0f;
    const float tb = static_cast<float>(now_ - bloomStart_);
    if (tb >= 0.0f && tb < 12.0f) {
        bloomEnv = tb < 0.7f ? std::sin(0.5f * kPi * tb / 0.7f) : std::exp(-(tb - 0.7f) / 1.8f);
        bloomEnv *= bloomEnv;
    }
    const float bs = bloomStrength_ * bloomEnv;
    ConductorOut target;
    target.strings = 1.0f + 0.55f * evo * expansion;
    target.vox = 1.0f + 0.5f * evo * transformation;
    target.glass = 1.0f + 0.6f * evo * transformation;
    target.bright = 0.06f * evo * expansion + 0.05f * evo * transformation * slow;
    target.send = 1.0f + 0.2f * evo * transformation + 0.35f * bs;
    target.width = 0.05f * evo * expansion + 0.12f * bs;
    const float k = 1.0f - std::exp(-dt_ / 1.5f);
    const float kFast = 1.0f - std::exp(-dt_ / 0.08f); // o bloom precisa seguir o próprio envelope
    out_.strings += (target.strings - out_.strings) * k;
    out_.vox += (target.vox - out_.vox) * k;
    out_.glass += (target.glass - out_.glass) * k;
    out_.bright += (target.bright - out_.bright) * k;
    out_.send += (target.send - out_.send) * kFast;
    out_.width += (target.width - out_.width) * kFast;
}

} // namespace pad
