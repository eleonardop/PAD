// PAD Engine — API pública do motor de áudio de pads.
//
// Modelo de threads:
//  - prepare(): thread comum (não tempo real). Gera wavetables e o banco de samples.
//  - process(): SOMENTE o callback de áudio. Sem locks, sem alocação, sem I/O, sem JNI.
//  - playChord()/setKey()/stop()/setParam()/applyPreset(): qualquer thread. Comandos vão
//    por uma fila SPSC sem trava; parâmetros são std::atomic<float>. Se vários threads
//    produzirem comandos, um mutex serializa APENAS o lado produtor (o callback nunca o toca).
#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <vector>
#include "core/Params.h"
#include "core/MusicTheory.h"
#include "core/RingBuffer.h"
#include "core/RenderContext.h"
#include "core/VoiceManager.h"
#include "core/Presets.h"
#include "dsp/Smoother.h"
#include "dsp/Svf.h"
#include "dsp/Reverb.h"
#include "dsp/Limiter.h"

namespace pad {

struct EngineStats {
    int activeVoices = 0;
    uint64_t steals = 0;
    uint64_t hardSteals = 0;
    uint64_t droppedCommands = 0;
    float limiterMinGain = 1.0f;  // menor ganho do limitador desde a última leitura
    float peakOut = 0.0f;         // pico absoluto de saída desde a última leitura
    float load = 0.0f;            // tempo de processamento / duração do buffer (média móvel)
    int currentKey = 0;
    int currentChord = 0;
    bool playing = false;
};

class PadEngine {
public:
    PadEngine();
    ~PadEngine();
    PadEngine(const PadEngine&) = delete;
    PadEngine& operator=(const PadEngine&) = delete;

    // Prepara o motor para uma taxa de amostragem. Pode levar ~1 s (bake do banco).
    // Chamar com o stream de áudio parado.
    bool prepare(float sampleRate);
    bool isPrepared() const { return prepared_.load(std::memory_order_acquire); }
    float sampleRate() const { return sr_; }

    // Callback de áudio: preenche 'frames' quadros estéreo intercalados (L,R,L,R...).
    void process(float* interleavedStereo, int frames);

    // ---- Controle (qualquer thread)
    bool playChord(int rootPitchClass, ChordType type);
    bool setKey(int rootPitchClass);            // mantém o tipo de acorde atual
    bool setChordType(ChordType type);          // mantém a tonalidade atual
    bool stop();                                // Release normal
    bool panic();                               // silencia em ~20 ms (uso técnico)
    void setParam(ParamId id, float value);
    float getParam(ParamId id) const;
    void applyPreset(int index);

    // ---- Banco de samples (thread comum). A troca acontece no callback quando não há vozes.
    void replaceSampleBank(std::unique_ptr<SampleBank> bank);
    // Libera, fora do callback, o banco antigo depois de uma troca (chamar periodicamente).
    void collectGarbage();
    const SampleBank* sampleBank() const { return bank_.get(); }

    EngineStats readStats();
    // Acesso somente leitura para testes (usar apenas no mesmo thread que chama process()).
    const VoiceManager& voiceManagerForTests() const { return voices_; }
    const RenderContext& contextForTests() const { return ctx_; }
    int latencyFrames() const { return limiter_.latency(); }

private:
    enum class CmdType : uint8_t { PlayChord, Stop, Panic };
    struct Command { CmdType type; uint8_t root; uint8_t chord; uint8_t pad; };

    bool pushCommand(const Command& c);
    void handleCommands();
    void updateControl();          // 1x por sub-bloco
    void renderSubBlock(float* out, int n);

    float sr_ = 48000.0f;
    std::atomic<bool> prepared_{false};

    std::unique_ptr<LayerTables> tables_;
    std::unique_ptr<SampleBank> bank_;
    std::atomic<SampleBank*> pendingBank_{nullptr};
    std::atomic<SampleBank*> retiredBank_{nullptr};

    VoiceManager voices_;
    RenderContext ctx_;

    // Parâmetros: escrita atômica pela interface, leitura pelo callback.
    std::array<std::atomic<float>, kParamCount> params_;
    std::array<Smoother, kParamCount> smooth_;

    SpscRingBuffer<Command, 256> commands_;
    std::mutex producerMutex_;
    std::atomic<int> uiKey_{0};
    std::atomic<int> uiChord_{0};

    // Barramentos (pré-alocados)
    std::array<float, kSubBlock> padL_{}, padR_{}, shimL_{}, shimR_{}, lowL_{}, lowR_{};

    // Cadeia master
    SvfStereo padHp_, lowLp_, lowHp_, lowShelf_, warmBell_, warmHighCut_, brightShelf_, airShelf_;
    OnePole dcL_, dcR_;
    FdnReverb reverb_;
    LookaheadLimiter limiter_;
    float lastTone_[6] = {-1, -1, -1, -1, -1, -1};
    float dryGain_ = 1.0f, wetGain_ = 0.0f, width_ = 1.0f, master_ = 0.5f, satMix_ = 0.0f, satDrive_ = 1.0f;
    float lowBus_ = 1.0f;

    // Telemetria
    std::atomic<int> statVoices_{0};
    std::atomic<uint64_t> statSteals_{0}, statHardSteals_{0}, statDropped_{0};
    std::atomic<float> statLimiter_{1.0f}, statPeak_{0.0f}, statLoad_{0.0f};
    std::atomic<bool> statPlaying_{false};
    float peakAcc_ = 0.0f, limAcc_ = 1.0f, loadEma_ = 0.0f;
};

} // namespace pad
