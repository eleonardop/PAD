#include "Sources.h"
#include "SampleBank.h"
#include "../dsp/PadSynth.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <sstream>

#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_STDIO
#include "../../third_party/dr_libs/dr_flac.h"

namespace pad {

// ----------------------------------------------------------------------------- ZoneBank

void ZoneBank::add(Zone&& z) {
    zones_.push_back(std::move(z));
    std::sort(zones_.begin(), zones_.end(), [](const Zone& a, const Zone& b) { return a.rootHz < b.rootHz; });
}

const Zone* ZoneBank::nearest(float targetHz, float outputRate, double& ratioOut) const {
    if (zones_.empty()) return nullptr;
    const Zone* best = &zones_[0];
    float bestDist = 1e9f;
    for (const Zone& z : zones_) {
        const float d = std::fabs(std::log2(targetHz / z.rootHz));
        if (d < bestDist) { bestDist = d; best = &z; }
    }
    ratioOut = (static_cast<double>(targetHz) / best->rootHz) * (static_cast<double>(best->sampleRate) / outputRate);
    return best;
}

size_t ZoneBank::bytes() const {
    size_t b = 0;
    for (const auto& z : zones_) b += z.bytes();
    return b;
}

// ----------------------------------------------------------------------------- assets

bool FileAssetProvider::readAll(const std::string& path, std::vector<uint8_t>& out) {
    const std::string full = root_ + "/" + path;
    FILE* f = std::fopen(full.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (size <= 0) { std::fclose(f); return false; }
    out.resize(static_cast<size_t>(size));
    const bool ok = std::fread(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    return ok;
}

bool decodeFlac16(const std::vector<uint8_t>& data, std::vector<int16_t>& pcm, int& frames, int& channels, int& sampleRate) {
    unsigned int ch = 0, sr = 0;
    drflac_uint64 total = 0;
    drflac_int16* p = drflac_open_memory_and_read_pcm_frames_s16(data.data(), data.size(), &ch, &sr, &total, nullptr);
    if (!p || total == 0) { if (p) drflac_free(p, nullptr); return false; }
    pcm.assign(p, p + total * ch);
    drflac_free(p, nullptr);
    frames = static_cast<int>(total);
    channels = static_cast<int>(ch);
    sampleRate = static_cast<int>(sr);
    return true;
}

const char* sourceName(SourceId s) {
    static const char* kNames[kSourceCount] = {"warm_escuro", "warm_aberto", "vox_ah", "vox_oh", "air_a", "air_b", "glass",
                                               "cordas_reais", "cordas_sinteticas", "piano_cathedral", "orgao", "piano_suave", "piano_suave_rev", "ep", "ep_rev", "vibrafone", "pratos", "pratos_rev"};
    const int i = static_cast<int>(s);
    return (i >= 0 && i < kSourceCount) ? kNames[i] : "?";
}

// ----------------------------------------------------------------------------- timbres PADsynth

namespace {

// Ressonância (formante) com forma de Lorentz.
inline float formant(float f, float center, float width) {
    const float x = (f - center) / (0.5f * width);
    return 1.0f / (1.0f + x * x);
}

PadSynthSpec specFor(SourceId s) {
    PadSynthSpec p;
    switch (s) {
        case SourceId::WarmDark:
            // Corpo quente: harmônicos caem como num dente de serra passado por um passa-baixa
            // de 12 dB/oitava em 2,4 kHz. Faixas de 28 cents: ensemble "analógico" largo.
            p.amplitude = [](int h, float f) { return std::pow(static_cast<float>(h), -1.5f) / (1.0f + (f / 2400.0f) * (f / 2400.0f)); };
            p.bandwidthCents = 28.0f; p.bandwidthScale = 1.0f; p.maxFreqHz = 9000.0f;
            break;
        case SourceId::WarmOpen:
            // A mesma família, mais aberta (destino do morph e da Intensidade).
            p.amplitude = [](int h, float f) { return std::pow(static_cast<float>(h), -1.15f) / (1.0f + (f / 5000.0f) * (f / 5000.0f)); };
            p.bandwidthCents = 20.0f; p.bandwidthScale = 1.0f; p.maxFreqHz = 13000.0f;
            break;
        case SourceId::VoxAh:
            // Coral "ah": fonte glotal suave modelada por formantes largos (coral, não voz solo).
            p.amplitude = [](int h, float f) {
                const float src = std::pow(static_cast<float>(h), -0.8f) / (1.0f + (f / 4500.0f) * (f / 4500.0f));
                const float env = 0.03f + formant(f, 780.0f, 160.0f) + 0.62f * formant(f, 1180.0f, 190.0f) +
                                  0.22f * formant(f, 2800.0f, 260.0f) + 0.10f * formant(f, 3700.0f, 320.0f);
                return src * env;
            };
            p.bandwidthCents = 42.0f; p.bandwidthScale = 1.12f; p.maxFreqHz = 11000.0f;
            break;
        case SourceId::VoxOh:
            p.amplitude = [](int h, float f) {
                const float src = std::pow(static_cast<float>(h), -0.8f) / (1.0f + (f / 4000.0f) * (f / 4000.0f));
                const float env = 0.03f + formant(f, 460.0f, 130.0f) + 0.55f * formant(f, 820.0f, 160.0f) +
                                  0.12f * formant(f, 2800.0f, 260.0f) + 0.06f * formant(f, 3600.0f, 320.0f);
                return src * env;
            };
            p.bandwidthCents = 42.0f; p.bandwidthScale = 1.12f; p.maxFreqHz = 10000.0f;
            break;
        case SourceId::AirA:
            // Air harmônico: só harmônicos a partir do 3º, envelope suave em torno de 2,2 kHz,
            // queda forte acima de 6,5 kHz. Faixas largas (90 cents) = "respiração" afinada.
            // Não há ruído: todo o conteúdo pertence à série harmônica da nota.
            p.amplitude = [](int h, float f) {
                const float d = std::log2(f / 2200.0f) / 0.9f;
                const float lp = 1.0f / (1.0f + std::pow(f / 6500.0f, 4.0f));
                return std::exp(-0.5f * d * d) * lp * (f < 700.0f ? 0.0f : 1.0f) / std::sqrt(static_cast<float>(h));
            };
            p.bandwidthCents = 90.0f; p.bandwidthScale = 1.2f; p.maxFreqHz = 9500.0f; p.firstHarmonic = 3;
            break;
        case SourceId::AirB:
            p.amplitude = [](int h, float f) {
                const float d = std::log2(f / 1600.0f) / 1.0f;
                const float lp = 1.0f / (1.0f + std::pow(f / 5500.0f, 4.0f));
                return std::exp(-0.5f * d * d) * lp * (f < 600.0f ? 0.0f : 1.0f) / std::sqrt(static_cast<float>(h));
            };
            p.bandwidthCents = 110.0f; p.bandwidthScale = 1.25f; p.maxFreqHz = 9000.0f; p.firstHarmonic = 2;
            break;
        case SourceId::Glass:
            // Shimmer: quase senoidal, um leve brilho de sino, faixas estreitas (10 cents).
            p.amplitude = [](int h, float) {
                static const float a[7] = {0.0f, 1.0f, 0.30f, 0.10f, 0.04f, 0.015f, 0.006f};
                return h <= 6 ? a[h] : 0.0f;
            };
            p.bandwidthCents = 10.0f; p.bandwidthScale = 1.0f; p.maxFreqHz = 12000.0f; p.maxHarmonics = 6;
            break;
        default:
            break;
    }
    return p;
}

// Notas raiz das zonas PADsynth: de 4 em 4 semitons (transposição máxima de ±2 semitons).
constexpr int kPadFirst = 34, kPadLast = 90, kPadStep = 4;

} // namespace

bool SourceLibrary::buildPadSynth(SourceId s, float sampleRate, int n, ZoneBank& out) {
    const PadSynthSpec spec = specFor(s);
    if (!spec.amplitude) return false;
    std::vector<int> notes;
    for (int m = kPadFirst; m <= kPadLast; m += kPadStep) notes.push_back(m);
    std::vector<Zone> zones(notes.size());
    // As duas tabelas de uma família de morph (Warm escuro/aberto, Vox oh/ah, Air A/B) usam a
    // MESMA semente: cada faixa de frequência tem a mesma fase nas duas, então a mistura linear
    // entre elas interpola os espectros, sem cancelamentos ("phasing") durante a evolução.
    int family = 3;
    if (s == SourceId::WarmDark || s == SourceId::WarmOpen) family = 0;
    else if (s == SourceId::VoxAh || s == SourceId::VoxOh) family = 1;
    else if (s == SourceId::AirA || s == SourceId::AirB) family = 2;
    const unsigned hw = std::max(1u, std::min(8u, std::thread::hardware_concurrency()));
    std::vector<std::thread> workers;
    for (unsigned w = 0; w < hw; ++w) {
        workers.emplace_back([&, w]() {
            std::vector<float> buf;
            for (size_t i = w; i < notes.size(); i += hw) {
                const float octave = s == SourceId::Glass ? 2.0f : 1.0f; // Glass soa uma oitava acima
                const float f0 = static_cast<float>(midiToHz(notes[i])) * octave;
                generatePadSynth(spec, f0, sampleRate, n, 0x9AD5ULL * (static_cast<uint64_t>(family) + 1) + static_cast<uint64_t>(notes[i]) * 7919ULL, buf);
                Zone& z = zones[i];
                z.rootNote = notes[i];
                z.rootHz = f0;
                z.sampleRate = sampleRate;
                z.setFromFloat(buf.data(), nullptr, n);
            }
        });
    }
    for (auto& t : workers) t.join();
    for (auto& z : zones) out.add(std::move(z));
    return true;
}

bool SourceLibrary::loadSampleSet(AssetProvider& assets, const std::string& set, ZoneBank& out) {
    std::vector<uint8_t> manifest;
    if (!assets.readAll("samples/" + set + "/manifest.txt", manifest)) return false;
    std::istringstream in(std::string(manifest.begin(), manifest.end()));
    std::string line;
    int loaded = 0;
    bool oneShot = false;
    std::vector<uint8_t> data;
    std::vector<int16_t> pcm;
    while (std::getline(in, line)) {
        if (line.rfind("#! oneshot", 0) == 0) { oneShot = true; continue; }
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ls(line);
        std::string file, kind;
        int root = 0;
        double hz = 0.0;
        if (!(ls >> file >> root >> hz)) continue;
        ls >> kind;
        if (!assets.readAll("samples/" + set + "/" + file, data)) return false;
        int frames = 0, ch = 0, sr = 0;
        if (!decodeFlac16(data, pcm, frames, ch, sr)) return false;
        Zone z;
        z.rootNote = root;
        z.rootHz = static_cast<float>(hz);
        z.sampleRate = static_cast<float>(sr);
        z.setFromInterleaved16(pcm.data(), frames, ch);
        if (oneShot) {
            // One-shots: o nível relativo entre as notas já foi acertado na preparação
            // (o decaimento natural faz parte do som); só o pico comum é recuperado.
            z.makeOneShotGuards();
            std::snprintf(z.kind, sizeof z.kind, "%s", kind.c_str());
            if (hz <= 0.0) { z.rootNote = 0; z.rootHz = 0.0f; }
            z.computePeak();
            out.add(std::move(z));
            ++loaded;
            continue;
        }
        // Normaliza cada zona para -20 dBFS RMS (o mesmo nível das tabelas PADsynth). Os
        // arquivos já vêm perto disso, mas o limite de pico do FLAC pode baixar notas com
        // muitos transientes; aqui o equilíbrio entre zonas fica garantido no próprio motor.
        double e = 0.0;
        for (int i = 0; i < frames; ++i) {
            const double a = z.l[static_cast<size_t>(i + 1)];
            const double b = z.channels == 2 ? z.r[static_cast<size_t>(i + 1)] : a;
            e += 0.5 * (a * a + b * b);
        }
        const double rms = std::sqrt(e / std::max(frames, 1));
        if (rms > 1.0) z.scale = static_cast<float>(0.1 / rms);
        out.add(std::move(z));
        ++loaded;
    }
    return loaded > 0;
}

// ----------------------------------------------------------------------------- biblioteca

SourceLibrary::SourceLibrary() = default;
SourceLibrary::~SourceLibrary() { stop(); }

void SourceLibrary::configure(float sampleRate, std::shared_ptr<AssetProvider> assets, int padSynthSize) {
    stop();
    sr_ = sampleRate;
    assets_ = std::move(assets);
    padN_ = padSynthSize;
    for (auto& s : slots_) {
        s.live.store(nullptr);
        s.owned.reset();
        s.state.store(static_cast<int>(State::Unloaded));
        s.usage.store(0);
    }
    wanted_ = 0;
}

void SourceLibrary::start() {
    std::lock_guard<std::mutex> lk(mutex_);
    if (running_) return;
    quit_ = false;
    running_ = true;
    thread_ = std::thread([this] { loaderLoop(); });
}

void SourceLibrary::stop() {
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if (!running_) return;
        quit_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
    std::lock_guard<std::mutex> lk(mutex_);
    running_ = false;
}

void SourceLibrary::setWanted(uint32_t mask) {
    {
        std::lock_guard<std::mutex> lk(mutex_);
        wanted_ = mask;
    }
    cv_.notify_all();
}

bool SourceLibrary::waitReady(uint32_t mask, double timeoutSeconds) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(timeoutSeconds);
    while (std::chrono::steady_clock::now() < deadline) {
        bool done = true;
        for (int i = 0; i < kSourceCount; ++i) {
            if (!(mask & (1u << i))) continue;
            const int st = slots_[static_cast<size_t>(i)].state.load();
            if (st != static_cast<int>(State::Ready) && st != static_cast<int>(State::Failed)) done = false;
        }
        if (done) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

uint32_t SourceLibrary::readyMask() const {
    uint32_t m = 0;
    for (int i = 0; i < kSourceCount; ++i)
        if (slots_[static_cast<size_t>(i)].state.load() == static_cast<int>(State::Ready)) m |= 1u << i;
    return m;
}

uint32_t SourceLibrary::failedMask() const {
    uint32_t m = 0;
    for (int i = 0; i < kSourceCount; ++i)
        if (slots_[static_cast<size_t>(i)].state.load() == static_cast<int>(State::Failed)) m |= 1u << i;
    return m;
}

size_t SourceLibrary::memoryBytes() const {
    std::lock_guard<std::mutex> lk(mutex_);
    size_t b = 0;
    for (const auto& s : slots_) if (s.owned) b += s.owned->bytes();
    return b;
}

void SourceLibrary::publishUsage(const uint16_t* counts) {
    for (int i = 0; i < kSourceCount; ++i) slots_[static_cast<size_t>(i)].usage.store(counts[i], std::memory_order_relaxed);
    audioBlocks_.fetch_add(1, std::memory_order_release);
}

bool SourceLibrary::loadSource(SourceId s, ZoneBank& out) {
    switch (s) {
        case SourceId::StringsSynth: bakeSynthStrings(out, sr_); return !out.empty();
        case SourceId::StringsReal: return assets_ && loadSampleSet(*assets_, "strings", out);
        case SourceId::PianoCathedral: return assets_ && loadSampleSet(*assets_, "piano_cathedral", out);
        case SourceId::Organ: return assets_ && loadSampleSet(*assets_, "organ", out);
        case SourceId::PianoSoft: return assets_ && loadSampleSet(*assets_, "piano_soft", out);
        case SourceId::Vibes: return assets_ && loadSampleSet(*assets_, "vibes", out);
        case SourceId::Cymbal: return assets_ && loadSampleSet(*assets_, "cymbal", out);
        case SourceId::EpTine: return buildEp(sr_, out);
        case SourceId::PianoSoftRev: {
            ZoneBank tmp;
            if (!assets_ || !loadSampleSet(*assets_, "piano_soft", tmp)) return false;
            buildReversed(tmp, out, 3.5);
            return !out.empty();
        }
        case SourceId::EpTineRev: {
            ZoneBank tmp;
            if (!buildEp(sr_, tmp)) return false;
            buildReversed(tmp, out, 3.0);
            return !out.empty();
        }
        case SourceId::CymbalRev: {
            ZoneBank tmp;
            if (!assets_ || !loadSampleSet(*assets_, "cymbal", tmp)) return false;
            buildReversed(tmp, out, 6.0, "hit");
            return !out.empty();
        }
        default: return buildPadSynth(s, sr_, padN_, out);
    }
}

void SourceLibrary::loaderLoop() {
    std::unique_lock<std::mutex> lk(mutex_);
    while (!quit_) {
        bool didWork = false;
        for (int i = 0; i < kSourceCount && !quit_; ++i) {
            Slot& slot = slots_[static_cast<size_t>(i)];
            const bool want = (wanted_ >> i) & 1u;
            const auto st = static_cast<State>(slot.state.load());
            if (want && st == State::Unloaded) {
                slot.state.store(static_cast<int>(State::Loading));
                lk.unlock();
                auto bank = std::make_unique<ZoneBank>();
                const bool ok = loadSource(static_cast<SourceId>(i), *bank);
                lk.lock();
                if (ok) {
                    slot.owned = std::move(bank);
                    slot.live.store(slot.owned.get(), std::memory_order_release);
                    slot.state.store(static_cast<int>(State::Ready));
                } else {
                    slot.state.store(static_cast<int>(State::Failed));
                }
                didWork = true;
            } else if (!want && st == State::Ready) {
                // Retira: novas vozes deixam de usar; espera o áudio confirmar que ninguém usa mais.
                slot.live.store(nullptr, std::memory_order_release);
                slot.retireAfterBlock = audioBlocks_.load(std::memory_order_acquire) + 2;
                slot.state.store(static_cast<int>(State::Retiring));
                didWork = true;
            } else if (want && st == State::Retiring) {
                slot.live.store(slot.owned.get(), std::memory_order_release);
                slot.state.store(static_cast<int>(State::Ready));
            } else if (!want && st == State::Retiring) {
                if (audioBlocks_.load(std::memory_order_acquire) > slot.retireAfterBlock && slot.usage.load() == 0) {
                    slot.owned.reset();
                    slot.state.store(static_cast<int>(State::Unloaded));
                    didWork = true;
                }
            } else if (!want && st == State::Failed) {
                slot.state.store(static_cast<int>(State::Unloaded)); // permite nova tentativa no futuro
            }
        }
        if (!didWork) cv_.wait_for(lk, std::chrono::milliseconds(100));
    }
}

} // namespace pad
