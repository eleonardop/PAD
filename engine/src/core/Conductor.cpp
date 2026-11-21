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
    pcWeight_.fill(0.0f);
    keyPc_ = -1;
    chordDurEma_ = 6.0f;
    nextMicroAt_ = 0.0;
    microClock_ = microTarget_ = 0.0f;
    lastMicroAt_ = bowStart_ = gustStart_ = -1e30;
    lastTypeAt_.fill(-1e30);
    lastType_ = -1;
    sameTypeRun_ = 0;
    microCounts_.fill(0);
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
    e.keyPc = static_cast<int8_t>(keyPc_);
    e.absTime = static_cast<float>(now_);
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
        e.level = 0.38f * sparkle * vel;
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
        e.level = 0.60f * keys * vel; // pico das notas ~7 dB abaixo do pad (medido em janelas de 400 ms)
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

// ============================================================================ v0.4: microeventos

namespace {
constexpr int kMajorScale[7] = {0, 2, 4, 5, 7, 9, 11};
bool inScale(int key, int pc) {
    for (int d : kMajorScale) if ((key + d) % 12 == pc) return true;
    return false;
}
float microCooldown(MicroType t) {
    switch (t) {
        case MicroType::Bell: return 5.0f;
        case MicroType::Piano: return 4.0f;
        case MicroType::Pluck: return 5.0f;
        case MicroType::Harmonic: return 9.0f;
        case MicroType::Reverse: return 12.0f;
        case MicroType::Metal: return 22.0f;
        case MicroType::Bow: return 14.0f;
        case MicroType::TapeGust: return 16.0f;
        default: return 10.0f;
    }
}
} // namespace

// Tonalidade estimada pelos acordes recentes (memória que decai): pesos por classe de altura e
// comparação com as 12 escalas maiores (as menores relativas usam o mesmo conjunto). Só há
// tonalidade quando ela se destaca das vizinhas; um acorde isolado é ambíguo (−1).
void Conductor::updateKey() {
    for (auto& w : pcWeight_) w *= 0.7f;
    for (int pc = 0; pc < 12; ++pc) if ((pcMask_ >> pc) & 1) pcWeight_[static_cast<size_t>(pc)] += 1.0f;
    pcWeight_[static_cast<size_t>(rootPc_)] += 0.5f;
    float total = 0.0f;
    for (float w : pcWeight_) total += w;
    float best = -1e9f, second = -1e9f;
    int bestK = -1;
    for (int k = 0; k < 12; ++k) {
        float sc = 0.0f;
        for (int pc = 0; pc < 12; ++pc) sc += inScale(k, pc) ? pcWeight_[static_cast<size_t>(pc)] : -1.5f * pcWeight_[static_cast<size_t>(pc)];
        if (sc > best) { second = best; best = sc; bestK = k; }
        else if (sc > second) second = sc;
    }
    keyPc_ = (total > 0.0f && (best - second) / total > 0.08f) ? bestK : -1;
}

// Cores permitidas (7ª e 9ª): só com tonalidade estimada e só se a nota está na escala.
uint16_t Conductor::colorMask(int /*midiLo*/) const {
    if (keyPc_ < 0) return 0;
    uint16_t m = 0;
    const int ninth = (rootPc_ + 2) % 12;
    const bool minor = isMinor(type_);
    const int seventh = (rootPc_ + (minor || type_ == ChordType::Dominant7 ? 10 : 11)) % 12;
    const bool ninthOk = type_ != ChordType::Sus4;
    const bool seventhOk = type_ != ChordType::Pad5 && type_ != ChordType::Sus2 && type_ != ChordType::Sus4;
    if (ninthOk && inScale(keyPc_, ninth)) m = static_cast<uint16_t>(m | (1u << ninth));
    if (seventhOk && inScale(keyPc_, seventh)) m = static_cast<uint16_t>(m | (1u << seventh));
    return static_cast<uint16_t>(m & ~pcMask_);
}

int Conductor::chooseMicroNote(int lo, int hi, bool colors, int center) {
    const uint16_t cmask = colors ? colorMask(lo) : 0;
    int cand[48];
    float w[48];
    int n = 0;
    const int ref = (lastNote_ >= lo - 7 && lastNote_ <= hi + 7) ? lastNote_ : center;
    for (int m = lo; m <= hi && n < 48; ++m) {
        const int pc = m % 12;
        const bool tone = (pcMask_ >> pc) & 1;
        const bool color = (cmask >> pc) & 1 && m >= 76; // cores só no registro alto (Mi5+)
        if (!tone && !color) continue;
        const int iv = std::abs(m - ref);
        float weight = iv == 0 ? (repeatCount_ >= 2 ? 0.0f : 0.3f) : iv <= 2 ? 1.0f : iv <= 5 ? 0.85f : iv <= 9 ? 0.45f : 0.15f;
        if (tone) weight *= pc == rootPc_ ? 1.1f : 1.0f;
        else weight *= pc == (rootPc_ + 2) % 12 ? 0.5f : 0.3f; // 9ª mais que 7ª
        if (weight <= 0.0f) continue;
        cand[n] = m;
        w[n] = weight;
        ++n;
    }
    if (n == 0) return -1;
    float tot = 0.0f;
    for (int i = 0; i < n; ++i) tot += w[i];
    float r = rng_.uniform() * tot;
    for (int i = 0; i < n; ++i) { r -= w[i]; if (r <= 0.0f) return cand[i]; }
    return cand[n - 1];
}

// Próximo microevento em "unidades de evento": o relógio avança na taxa atual (por minuto), então
// mudar a Atividade vale na hora, sem esperar um intervalo agendado com o valor antigo.
float Conductor::microInterval(const RenderContext& /*ctx*/) {
    float units = 1.0f;
    if (rng_.uniform() < 0.12f) units *= 2.5f;      // às vezes um longo silêncio
    const float g = (rng_.uniform() + rng_.uniform() + rng_.uniform() + rng_.uniform() - 2.0f) * 1.7f;
    return units * std::exp(0.55f * g);             // log-normal: sem periodicidade perceptível
}

void Conductor::microEvent(const RenderContext& ctx, const SourceLibrary& lib, TexturePlayer& tex) {
    const float since = static_cast<float>(now_ - chordStart_);
    if (since < 1.2f) return;
    // Densidade: nunca mais de 3 microeventos tonais soando ao mesmo tempo.
    if (tex.activeInGroup(3) >= 3) return;
    // "Pode não acontecer nada."
    if (rng_.uniform() < 0.2f) return;

    // A história do acorde: o que cabe em cada momento.
    float story[static_cast<size_t>(MicroType::Count)];
    if (since < 4.0f) { const float s0[] = {0.4f, 0.6f, 0.7f, 0.2f, 0.0f, 0.0f, 0.3f, 0.5f}; std::copy(s0, s0 + 8, story); }
    else if (since < 12.0f) { const float s1[] = {1.0f, 1.0f, 1.0f, 1.0f, 0.5f, 0.4f, 1.0f, 1.0f}; std::copy(s1, s1 + 8, story); }
    else { const float s2[] = {1.0f, 0.8f, 0.8f, 1.2f, 1.2f, 1.0f, 1.0f, 1.0f}; std::copy(s2, s2 + 8, story); }

    const float pal[] = {ctx.microBell, ctx.microPiano, ctx.microPluck, ctx.microHarmonic,
                         ctx.microReverse, ctx.microMetal, ctx.microBow, ctx.microTapeGust};
    // Volume de cada família = nível da camada correspondente no preset.
    const float vol[] = {ctx.layerSparkle, ctx.layerKeys, ctx.layerKeys, ctx.layerSparkle,
                         ctx.layerKeys, ctx.layerMetal, ctx.layerStrings, ctx.tape};
    // Espaço espectral: com o pad já brilhante ou cheio de ar, menos eventos agudos.
    const float highSpace = (ctx.brightness > 0.55f ? 0.6f : 1.0f) * (ctx.layerAir + ctx.layerShimmer > 0.9f ? 0.7f : 1.0f);
    float w[static_cast<size_t>(MicroType::Count)];
    float tot = 0.0f;
    for (int t = 0; t < static_cast<int>(MicroType::Count); ++t) {
        float x = pal[t] * story[t] * (vol[t] > 0.02f ? 1.0f : 0.0f);
        if (t == static_cast<int>(MicroType::Bell) || t == static_cast<int>(MicroType::Harmonic)) x *= highSpace;
        if (now_ - lastTypeAt_[static_cast<size_t>(t)] < microCooldown(static_cast<MicroType>(t))) x = 0.0f;
        if (t == lastType_) x *= sameTypeRun_ >= 2 ? 0.0f : 0.35f;
        w[t] = x;
        tot += x;
    }
    if (tot <= 0.0f) return;
    float r = rng_.uniform() * tot;
    int type = 0;
    for (; type < static_cast<int>(MicroType::Count) - 1; ++type) { r -= w[type]; if (r <= 0.0f) break; }
    const MicroType mt = static_cast<MicroType>(type);

    const float bias = clampf(ctx.microHighBias, -1.0f, 1.0f);
    const int shift = static_cast<int>(std::lround(bias * 3.0f));
    if (rng_.uniform() < 0.8f) lastPanSide_ = -lastPanSide_;
    const float pan = clampf(lastPanSide_ * (0.2f + 0.45f * rng_.uniform()), -0.75f, 0.75f);
    const float vel = 0.7f + 0.3f * rng_.uniform();
    TexEvent e;
    e.pan = pan;
    e.group = 3;
    e.priority = 1;
    int note = 0;
    bool fired = false;
    switch (mt) {
        case MicroType::Bell: {
            note = chooseMicroNote(76 + shift, 88 + shift, true, 81);
            if (note < 0) break;
            e.source = SourceId::Vibes; e.midi = note;
            e.level = 0.34f * ctx.layerSparkle * vel;
            e.lowpassHz = 6000.0f + 1500.0f * ctx.brightness; e.highpassHz = 350.0f; e.release = 2.0f; e.priority = 0;
            fired = tex.trigger(e, lib);
            break;
        }
        case MicroType::Piano: {
            note = chooseMicroNote(72 + shift, 91 + shift, true, 79);
            if (note < 0) break;
            e.source = ctx.keysType == 0 ? SourceId::PianoSoft : SourceId::EpTine; e.midi = note;
            e.level = 0.55f * ctx.layerKeys * vel;
            e.lowpassHz = 2200.0f + 2600.0f * ctx.brightness; e.highpassHz = 150.0f; e.release = 2.5f;
            fired = tex.trigger(e, lib);
            break;
        }
        case MicroType::Pluck: {
            note = chooseMicroNote(67 + shift, 84 + shift, false, 74);
            if (note < 0) break;
            e.source = SourceId::EpTine; e.midi = note;
            e.level = 0.36f * ctx.layerKeys * vel;
            e.attack = 0.004f; e.hold = 0.22f + 0.15f * rng_.uniform(); e.release = 0.9f;
            e.lowpassHz = 2600.0f + 1500.0f * ctx.brightness; e.highpassHz = 200.0f;
            fired = tex.trigger(e, lib);
            break;
        }
        case MicroType::Harmonic: {
            note = chooseMicroNote(79 + shift, 96 + shift, true, 86);
            if (note < 0) break;
            e.source = SourceId::Glass; e.midi = note;
            e.level = 0.06f * ctx.layerSparkle * vel;
            e.attack = 1.0f + 1.5f * rng_.uniform(); e.attackCurve = 2.0f; e.hold = 0.8f + 1.7f * rng_.uniform(); e.release = 3.0f;
            e.lowpassHz = 7000.0f; e.highpassHz = 600.0f; e.priority = 0;
            fired = tex.trigger(e, lib);
            break;
        }
        case MicroType::Reverse: {
            note = chooseMicroNote(69 + shift, 84 + shift, false, 76);
            if (note < 0) break;
            const SourceId rs = ctx.keysType == 0 ? SourceId::PianoSoftRev : SourceId::EpTineRev;
            const ZoneBank* kb = lib.get(rs);
            if (!kb) break;
            double ratio = 1.0;
            const Zone* z = kb->nearest(static_cast<float>(440.0 * std::pow(2.0, (note - 69) / 12.0)), ctx.sampleRate, ratio);
            if (!z) break;
            const float L = 1.2f + 1.4f * rng_.uniform(); // duração do crescimento
            const float peakOut = static_cast<float>(z->peakFrame) / (z->sampleRate * static_cast<float>(ratio));
            e.source = rs; e.midi = note;
            e.startOffset = std::max(0.0f, (peakOut - L) * static_cast<float>(ratio));
            e.level = 0.32f * ctx.layerKeys * vel;
            e.attack = 0.05f; e.release = 1.5f;
            e.lowpassHz = 2400.0f + 2000.0f * ctx.brightness; e.highpassHz = 180.0f;
            fired = tex.trigger(e, lib);
            break;
        }
        case MicroType::Metal: {
            e.source = SourceId::Cymbal;
            const bool bow = rng_.uniform() < 0.6f;
            e.kind = bow ? "bow" : "wash";
            e.kindIndex = static_cast<int>(rng_.uniform() * (bow ? 4.0f : 2.0f));
            e.level = 0.12f * ctx.layerMetal * vel;
            e.attack = 1.5f; e.attackCurve = 1.5f; e.hold = 1.0f; e.release = 3.0f;
            e.lowpassHz = 4500.0f; e.highpassHz = 500.0f; e.bus = TexBus::Metal; e.group = 0; e.priority = 2;
            fired = tex.trigger(e, lib);
            break;
        }
        case MicroType::Bow:
            bowStart_ = now_;
            bowStrength_ = 0.6f + 0.4f * rng_.uniform();
            fired = true;
            break;
        case MicroType::TapeGust:
            gustStart_ = now_;
            fired = true;
            break;
        default: break;
    }
    if (!fired && mt != MicroType::Bow && mt != MicroType::TapeGust && note < 0) return;
    // Registra mesmo se a fonte ainda não carregou (as regras de nota valem do mesmo jeito).
    lastTypeAt_[static_cast<size_t>(type)] = now_;
    sameTypeRun_ = (type == lastType_) ? sameTypeRun_ + 1 : 0;
    lastType_ = type;
    lastMicroAt_ = now_;
    ++microCounts_[static_cast<size_t>(type)];
    if (note > 0) {
        repeatCount_ = (note == lastNote_) ? repeatCount_ + 1 : 0;
        lastNote_ = note;
    }
    logNote(note > 0 ? note : 0, static_cast<uint8_t>(10 + type));
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
        if (!fromSilence) chordDurEma_ += (std::min(prevDur, 30.0f) - chordDurEma_) * 0.35f;
        updateKey();
        // Microeventos tonais do acorde anterior que não cabem no novo saem devagar.
        tex.releaseGroupNotInMask(3, pcMask_, 0.6f);
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
        // Microeventos: o primeiro do acorde vem depois do estabelecimento (ou nunca).
        nextMicroAt_ = now_ + 1.2 + 3.0 * rng_.uniform();
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

        const bool palette = ctx.microBell + ctx.microPiano + ctx.microPluck + ctx.microHarmonic + ctx.microReverse +
                             ctx.microMetal + ctx.microBow + ctx.microTapeGust > 0.0f;
        if (palette) {
            // v0.4: microeventos pela paleta do preset (substituem as notas de textura da v0.3).
            // Atividade abaixo de 2% = desligado. (Os parâmetros sobem suavizados: agendar com a
            // atividade ainda perto de zero daria um intervalo de muitos minutos.)
            if (ctx.activity > 0.02f && ctx.microPerMinute > 0.0f) {
                float rate = ctx.microPerMinute * clampf(ctx.activity, 0.0f, 1.0f) * 2.0f; // eventos por minuto
                if (chordDurEma_ < 2.5f) rate *= 0.25f;     // trocas rápidas: quase nada acontece
                microClock_ += dt_ * rate / 60.0f;
                if (microTarget_ <= 0.0f) microTarget_ = microInterval(ctx);
                // Espaçamento mínimo global de 2,5 s entre microeventos.
                if (microClock_ >= microTarget_ && now_ - lastMicroAt_ >= 2.5 && now_ >= nextMicroAt_) {
                    microEvent(ctx, lib, tex);
                    microClock_ = 0.0f;
                    microTarget_ = microInterval(ctx);
                }
            }
        } else if (now_ >= nextTexAt_) {
            const bool allowed = out_.stage != LifeStage::Establishment || notesThisChord_ == 0;
            if (allowed && ctx.activity > 1e-3f) textureNote(ctx, lib, tex);
            nextTexAt_ = ctx.activity > 1e-3f ? now_ + nextInterval(ctx) : 1e30;
        } else if (ctx.activity > 0.02f && nextTexAt_ > now_ + 120.0) {
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
    // Arco das cordas (microevento): cresce ~2,5 s e volta em ~3,5 s, até +3 dB.
    float bowEnv = 0.0f;
    const float tbw = static_cast<float>(now_ - bowStart_);
    if (tbw >= 0.0f && tbw < 6.0f) bowEnv = tbw < 2.5f ? std::sin(0.5f * kPi * tbw / 2.5f) : std::cos(0.5f * kPi * (tbw - 2.5f) / 3.5f);
    float gust = 0.0f;
    const float tg = static_cast<float>(now_ - gustStart_);
    if (tg >= 0.0f && tg < 1.8f) gust = std::sin(kPi * tg / 1.8f);
    out_.tapeGust = gust * gust;
    ConductorOut target;
    target.strings = 1.0f + 0.55f * evo * expansion + 0.45f * bowStrength_ * bowEnv * bowEnv;
    target.vox = 1.0f + 0.5f * evo * transformation;
    target.glass = 1.0f + 0.6f * evo * transformation;
    target.bright = 0.06f * evo * expansion + 0.05f * evo * transformation * slow;
    target.send = 1.0f + 0.2f * evo * transformation + 0.35f * bs;
    target.width = 0.05f * evo * expansion + 0.12f * bs;
    const float k = 1.0f - std::exp(-dt_ / 1.5f);
    const float kFast = 1.0f - std::exp(-dt_ / 0.08f); // o bloom precisa seguir o próprio envelope
    out_.strings += (target.strings - out_.strings) * (bowEnv > 0.0f ? kFast : k);
    out_.vox += (target.vox - out_.vox) * k;
    out_.glass += (target.glass - out_.glass) * k;
    out_.bright += (target.bright - out_.bright) * k;
    out_.send += (target.send - out_.send) * kFast;
    out_.width += (target.width - out_.width) * kFast;
}

} // namespace pad
