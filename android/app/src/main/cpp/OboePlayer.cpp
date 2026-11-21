#include "OboePlayer.h"
#include <android/log.h>
#include <chrono>
#include <thread>

#define LOG_TAG "PadEngine"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace padandroid {

bool OboePlayer::openStream() {
    oboe::AudioStreamBuilder b;
    b.setDirection(oboe::Direction::Output)
        ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
        ->setSharingMode(oboe::SharingMode::Exclusive) // o sistema cai para Shared se não puder
        ->setFormat(oboe::AudioFormat::Float)
        ->setChannelCount(oboe::ChannelCount::Stereo)
        // O motor roda sempre a 48 kHz. Se a saída usar outra taxa (ex.: alguns fones
        // Bluetooth a 44,1 kHz), o Oboe faz a conversão com qualidade média.
        ->setSampleRate(kEngineSampleRate)
        ->setSampleRateConversionQuality(oboe::SampleRateConversionQuality::Medium)
        ->setUsage(oboe::Usage::Media)
        ->setContentType(oboe::ContentType::Music)
        ->setDataCallback(this)
        ->setErrorCallback(this);

    oboe::Result r = b.openStream(stream_);
    if (r != oboe::Result::OK) {
        LOGE("Falha ao abrir stream: %s", oboe::convertToText(r));
        stream_.reset();
        return false;
    }
    // Pads não exigem latência mínima; 4 bursts dão margem contra interrupções do sistema
    // (~10–20 ms na maioria dos aparelhos), priorizando continuidade sobre latência.
    const int burst = stream_->getFramesPerBurst();
    auto res = stream_->setBufferSizeInFrames(burst * 4);
    bufferFrames_ = res ? res.value() : stream_->getBufferSizeInFrames();
    deviceRate_ = stream_->getSampleRate();
    LOGI("Stream aberto: %s, %d Hz, burst %d, buffer %d, compartilhamento %s",
         stream_->getAudioApi() == oboe::AudioApi::AAudio ? "AAudio" : "OpenSL ES", deviceRate_.load(), burst,
         bufferFrames_.load(), stream_->getSharingMode() == oboe::SharingMode::Exclusive ? "exclusivo" : "compartilhado");
    return true;
}

bool OboePlayer::start() {
    std::lock_guard<std::mutex> g(lock_);
    if (running_) return true;
    if (!engine_.isPrepared()) {
        LOGI("Preparando motor...");
        if (!engine_.prepare(static_cast<float>(kEngineSampleRate))) return false;
    }
    if (!openStream()) return false;
    const oboe::Result r = stream_->requestStart();
    if (r != oboe::Result::OK) {
        LOGE("Falha ao iniciar stream: %s", oboe::convertToText(r));
        stream_->close();
        stream_.reset();
        return false;
    }
    running_ = true;
    return true;
}

void OboePlayer::stop() {
    std::lock_guard<std::mutex> g(lock_);
    running_ = false;
    if (stream_) {
        stream_->requestStop();
        stream_->close();
        stream_.reset();
    }
}

oboe::DataCallbackResult OboePlayer::onAudioReady(oboe::AudioStream*, void* audioData, int32_t numFrames) {
    // Caminho crítico: só o motor (sem locks, sem alocação, sem JNI).
    engine_.process(static_cast<float*>(audioData), numFrames);
    return oboe::DataCallbackResult::Continue;
}

void OboePlayer::onErrorAfterClose(oboe::AudioStream*, oboe::Result error) {
    // Ex.: fone conectado/desconectado, troca de saída Bluetooth. O Oboe já fechou o stream;
    // reabrimos na nova saída. O motor continua com o mesmo estado (o acorde segue tocando).
    LOGI("Stream encerrado pelo sistema (%s); reabrindo.", oboe::convertToText(error));
    if (!running_) return;
    std::lock_guard<std::mutex> g(lock_);
    stream_.reset();
    for (int attempt = 0; attempt < 5; ++attempt) {
        if (openStream()) {
            if (stream_->requestStart() == oboe::Result::OK) return;
            stream_->close();
            stream_.reset();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200)); // nova saída ainda inicializando
    }
    LOGE("Não foi possível reabrir o stream de áudio.");
    running_ = false;
}

} // namespace padandroid
