// PAD Engine — leitura e escrita de WAV (fora do callback de áudio).
#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace pad {

// Grava estéreo intercalado em PCM 24 bits com dither TPDF (formato aceito por qualquer player).
bool writeWav24(const std::string& path, const std::vector<float>& interleavedStereo, int sampleRate);

struct WavData {
    int sampleRate = 0;
    int channels = 0;
    std::vector<float> samples; // intercalado
};

// Lê PCM 16/24/32 bits ou float 32 bits. Ignora chunks desconhecidos.
bool readWav(const std::string& path, WavData& out);

// Mistura para mono (para o banco de samples).
std::vector<float> toMono(const WavData& w);

} // namespace pad
