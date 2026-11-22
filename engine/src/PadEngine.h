// PAD Engine — API pública do motor de áudio de pads (versão 0.2).
//
// Modelo de threads:
//  - prepare(): thread comum (não tempo real). Gera a wavetable do grave, inicia o
//    carregador de fontes e espera as fontes do preset atual ficarem prontas.
//  - process(): SOMENTE o callback de áudio. Sem locks, sem alocação, sem I/O, sem JNI.
//  - playChord()/setKey()/stop()/setParam()/applyPreset(): qualquer thread. Comandos vão
//    por uma fila SPSC sem trava; parâmetros são std::atomic<float>. Um mutex serializa
//    APENAS o lado produtor (o callback nunca o toca).
//  - Fontes (tabelas PADsynth e samples) são carregadas sob demanda por um thread próprio
//    (SourceLibrary) conforme os volumes de camada; o callback só lê ponteiros atômicos.
#pragma once

#include <array>
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
#include "core/Sources.h"
#include "dsp/Smoother.h"
#include "dsp/Svf.h"
#include "dsp/ReverbBase.h"
#include "dsp/ShimmerLoop.h"
#include "dsp/Ensemble.h"
#include "dsp/GlueCompressor.h"
#include "dsp/TapeCharacter.h"
#include "core/TexturePlayer.h"
#include "core/Conductor.h"
#include "dsp/Limiter.h"

namespace pad {

struct EngineStats {
    int activeVoices = 0;
    uint64_t steals = 0;
    uint64_t hardSteals = 0;
    uint64_t droppedCommands = 0;
    float limiterMinGain = 1.0f;  // menor ganho do limitador desde a última leitura
    float peakOut = 0.0f;         // pico absoluto de saída desde a última leitura
    float glueMaxReductionDb = 0.0f;
    float load = 0.0f;            // tempo de processamento / duração do buffer (média móvel)
    float loadPeak = 0.0f;        // pior bloco desde a última leitura
    int textureVoices = 0;        // v0.3: vozes do TexturePlayer ativas
    uint32_t textureEvents = 0, blooms = 0, swells = 0;
    int lifeStage = 0;
    int currentKey = 0;
    int currentChord = 0;
    bool playing = false;
    uint32_t sourcesReady = 0;    // bits de SourceId prontos
    uint32_t sourcesFailed = 0;
    size_t sourcesMemoryBytes = 0;
};

enum class ReverbType : int { Classic = 0, Plate = 1, Cloud = 2, Count = 3 };

class PadEngine {
public:
    PadEngine();
    ~PadEngine();
    PadEngine(const PadEngine&) = delete;
    PadEngine& operator=(const PadEngine&) = delete;

    // Onde ficam os samples (assets). Chamar antes de prepare(). Sem assets, as camadas de
    // samples reais ficam indisponíveis e as cordas usam automaticamente a versão sintética.
    void setAssetProvider(std::shared_ptr<AssetProvider> assets) { assets_ = std::move(assets); }

    // Prepara o motor e espera (até waitSeconds) as fontes do preset atual. Stream parado.
    bool prepare(float sampleRate, double waitSeconds = 30.0);
    bool isPrepared() const { return prepared_.load(std::memory_order_acquire); }
    float sampleRate() const { return sr_; }

    // Callback de áudio: preenche 'frames' quadros estéreo intercalados (L,R,L,R...).
    void process(float* interleavedStereo, int frames);

    // ---- Controle (qualquer thread)
    bool playChord(int rootPitchClass, ChordType type);
    bool setKey(int rootPitchClass);
    bool setChordType(ChordType type);
    bool stop();
    bool panic();
    // v0.3: pre-swell manual — cresce durante SwellTime, com pico no fim (para trocar o acorde ali).
    bool swell();
    // v0.4: semente dos microeventos (por padrão vem do relógio em prepare(): cada execução varia).
    bool setSeed(uint32_t seed);
    void setParam(ParamId id, float value);
    float getParam(ParamId id) const;
    void applyPreset(int index);

    // Espera as fontes pedidas pelos parâmetros atuais (ex.: depois de trocar de preset).
    bool waitForSources(double timeoutSeconds);
    // Mantido por compatibilidade com a ponte JNI da v0.1 (hoje a liberação é automática).
    void collectGarbage() {}

    EngineStats readStats();
    int latencyFrames() const { return limiter_.latency(); }

    // Somente testes (mesmo thread que chama process()).
    const VoiceManager& voiceManagerForTests() const { return voices_; }
    const RenderContext& contextForTests() const { return ctx_; }
    const SourceLibrary& sourcesForTests() const { return sources_; }
    const Conductor& conductorForTests() const { return conductor_; }
    const TexturePlayer& texturesForTests() const { return texture_; }

private:
    enum class CmdType : uint8_t { PlayChord, Stop, Panic, Swell, Seed };
    struct Command { CmdType type; uint8_t root; uint8_t chord; uint8_t pad; uint32_t seed = 0; };

    struct ReverbSlot {
        std::unique_ptr<ReverbBase> reverb;
        float gain = 0.0f;       // peso atual na mistura (troca suave de tipo)
        bool running = false;    // continua processando (com entrada zerada) até a cauda morrer
        int quietBlocks = 0;
    };

    bool pushCommand(const Command& c);
    void handleCommands();
    void refreshWanted();
    uint32_t wantedMask() const;
    void updateControl();
    void renderSubBlock(float* out, int n);

    float sr_ = 48000.0f;
    std::atomic<bool> prepared_{false};

    std::shared_ptr<AssetProvider> assets_;
    SourceLibrary sources_;
    std::unique_ptr<LayerTables> tables_;
    VoiceManager voices_;
    RenderContext ctx_;

    std::array<std::atomic<float>, kParamCount> params_;
    std::array<Smoother, kParamCount> smooth_;
    // Moduladores globais da Evolução: soma de senos com razões irracionais (quase periódica).
    // Um ruído suave pode passar dezenas de segundos quase parado; aqui o percurso é garantido.
    std::atomic<float> presetTrim_{1.0f};
    std::array<std::atomic<float>, 11> micro_{}; // paleta de microeventos do preset (v0.4) // ganho linear do trim do preset atual
    double evoPhase_[6] = {0.0, 0.37, 0.71, 0.13, 0.52, 0.89};

    SpscRingBuffer<Command, 256> commands_;
    std::mutex producerMutex_;
    std::atomic<int> uiKey_{0};
    std::atomic<int> uiChord_{0};

    // Barramentos (pré-alocados)
    std::array<float, kSubBlock> padL_{}, padR_{}, airL_{}, airR_{}, shimL_{}, shimR_{}, lowL_{}, lowR_{};
    std::array<float, kSubBlock> sendL_{}, sendR_{}, wetL_{}, wetR_{}, tmpL_{}, tmpR_{}, zero_{};
    std::array<float, kSubBlock> texL_{}, texR_{}, metL_{}, metR_{};
    TexturePlayer texture_;
    Conductor conductor_;
    TapeCharacter tape_;
    SvfStereo metalHp_, metalLp_;
    float baseWidthBoost_ = 0.0f, sendBoost_ = 1.0f;
    std::array<uint16_t, kSourceCount> usage_{};

    // Cadeia master
    SvfStereo toneCutA_, toneCutB_;
    float lastToneCut_ = -1.0f;
    bool toneCutOn_ = false;
    SvfStereo padHp_, lowLp_, lowHp_, lowShelf_, warmBell_, warmHighCut_, tilt_, sendHp_, sendLp_;
    OnePole dcL_, dcR_, sideLp_;
    EnsembleChorus ensemble_;
    ShimmerLoop shimmer_;
    std::array<ReverbSlot, static_cast<size_t>(ReverbType::Count)> reverbs_;
    int activeReverb_ = -1;
    GlueCompressor glue_;
    LookaheadLimiter limiter_;
    float lastTone_[4] = {-1, -1, -1, -1};
    float dryGain_ = 1.0f, wetGain_ = 0.0f, width_ = 1.0f, master_ = 0.5f, satMix_ = 0.0f, satDrive_ = 1.0f, chorus_ = 0.0f;

    // Telemetria
    std::atomic<int> statVoices_{0};
    std::atomic<uint64_t> statSteals_{0}, statHardSteals_{0}, statDropped_{0};
    std::atomic<int> statTexVoices_{0}, statStage_{0};
    std::atomic<uint32_t> statTexEvents_{0}, statBlooms_{0}, statSwells_{0};
    std::atomic<float> statLimiter_{1.0f}, statPeak_{0.0f}, statLoad_{0.0f}, statGlue_{0.0f}, statLoadPeak_{0.0f};
    std::atomic<bool> statPlaying_{false};
    float peakAcc_ = 0.0f, loadEma_ = 0.0f;
};

} // namespace pad
