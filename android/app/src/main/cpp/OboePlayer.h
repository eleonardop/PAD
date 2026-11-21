// Saída de áudio Android via Oboe (AAudio quando disponível; OpenSL ES como reserva).
#pragma once

#include <oboe/Oboe.h>
#include <atomic>
#include <memory>
#include <mutex>
#include "PadEngine.h"

namespace padandroid {

class OboePlayer : public oboe::AudioStreamDataCallback, public oboe::AudioStreamErrorCallback {
public:
    static constexpr int kEngineSampleRate = 48000;

    explicit OboePlayer(pad::PadEngine& engine) : engine_(engine) {}
    ~OboePlayer() override { stop(); }

    // Abre o stream, prepara o motor (bake ~1–2 s) e inicia. Chamar fora do thread de UI.
    bool start();
    void stop();
    bool isRunning() const { return running_.load(); }
    int deviceSampleRate() const { return deviceRate_.load(); }
    int bufferFrames() const { return bufferFrames_.load(); }

    // Callbacks do Oboe
    oboe::DataCallbackResult onAudioReady(oboe::AudioStream* stream, void* audioData, int32_t numFrames) override;
    void onErrorAfterClose(oboe::AudioStream* stream, oboe::Result error) override;

private:
    bool openStream();

    pad::PadEngine& engine_;
    std::shared_ptr<oboe::AudioStream> stream_;
    std::mutex lock_; // protege abrir/fechar (nunca usado no callback)
    std::atomic<bool> running_{false};
    std::atomic<int> deviceRate_{0};
    std::atomic<int> bufferFrames_{0};
};

} // namespace padandroid
