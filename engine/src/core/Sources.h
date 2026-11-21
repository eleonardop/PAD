// PAD Engine — biblioteca de fontes sonoras com carregamento sob demanda.
//
// Fontes:
//  - Tabelas PADsynth (geradas na hora, sem ocupar espaço no APK): Warm escuro/aberto,
//    Vox "ah"/"oh", Air A/B (camada harmônica respirada) e Glass (shimmer).
//  - Samples reais (assets FLAC preparados por tools/samplelab): cordas VSCO 2 CE,
//    piano "Cathedral" (Salamander) e órgão suave (VCSL).
//  - Cordas sintéticas da versão 0.1 (reserva enquanto os samples reais são validados).
//
// Modelo de threads:
//  - Um thread "carregador" (não tempo real) gera/decodifica as fontes pedidas e libera as
//    que não são mais usadas.
//  - O callback de áudio só LÊ um ponteiro atômico por fonte (nullptr = indisponível) e
//    publica, a cada bloco, quantas vozes usam cada fonte. Uma fonte só é liberada depois
//    que o ponteiro foi zerado, pelo menos um bloco de áudio inteiro passou e nenhuma voz
//    a usa mais. Nunca há alocação, liberação ou espera no callback.
#pragma once

#include <array>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "Zone.h"

namespace pad {

enum class SourceId : int {
    WarmDark = 0,
    WarmOpen,
    VoxAh,
    VoxOh,
    AirA,
    AirB,
    Glass,
    StringsReal,
    StringsSynth,
    PianoCathedral,
    Organ,
    Count
};
constexpr int kSourceCount = static_cast<int>(SourceId::Count);
inline uint32_t sourceBit(SourceId s) { return 1u << static_cast<int>(s); }
const char* sourceName(SourceId s);

// Acesso a arquivos de dados (assets). No Android é implementado com AAssetManager.
class AssetProvider {
public:
    virtual ~AssetProvider() = default;
    virtual bool readAll(const std::string& path, std::vector<uint8_t>& out) = 0;
};

class FileAssetProvider : public AssetProvider {
public:
    explicit FileAssetProvider(std::string root) : root_(std::move(root)) {}
    bool readAll(const std::string& path, std::vector<uint8_t>& out) override;
private:
    std::string root_;
};

// Decodifica FLAC (16 bits) da memória. Devolve frames e canais.
bool decodeFlac16(const std::vector<uint8_t>& data, std::vector<int16_t>& pcm, int& frames, int& channels, int& sampleRate);

class SourceLibrary {
public:
    enum class State : int { Unloaded = 0, Loading, Ready, Retiring, Failed };

    SourceLibrary();
    ~SourceLibrary();

    void configure(float sampleRate, std::shared_ptr<AssetProvider> assets, int padSynthSize = 1 << 18);
    void start();
    void stop();

    // ---- thread comum
    void setWanted(uint32_t mask);
    bool waitReady(uint32_t mask, double timeoutSeconds);
    uint32_t readyMask() const;
    uint32_t failedMask() const;
    size_t memoryBytes() const;
    State state(SourceId s) const { return static_cast<State>(slots_[static_cast<size_t>(s)].state.load()); }

    // ---- callback de áudio
    const ZoneBank* get(SourceId s) const { return slots_[static_cast<size_t>(s)].live.load(std::memory_order_acquire); }
    void publishUsage(const uint16_t* counts);

    // Geração isolada (testes e ferramentas).
    static bool buildPadSynth(SourceId s, float sampleRate, int n, ZoneBank& out);
    static bool loadSampleSet(AssetProvider& assets, const std::string& set, ZoneBank& out);

private:
    struct Slot {
        std::atomic<const ZoneBank*> live{nullptr};
        std::unique_ptr<ZoneBank> owned;
        std::atomic<int> state{0};
        std::atomic<uint16_t> usage{0};
        uint64_t retireAfterBlock = 0;
    };
    void loaderLoop();
    bool loadSource(SourceId s, ZoneBank& out);

    float sr_ = 48000.0f;
    int padN_ = 1 << 18;
    std::shared_ptr<AssetProvider> assets_;
    std::array<Slot, kSourceCount> slots_;
    std::atomic<uint64_t> audioBlocks_{0};

    std::thread thread_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    uint32_t wanted_ = 0;
    bool quit_ = false;
    bool running_ = false;
};

} // namespace pad
