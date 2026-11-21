// Ferramentas de medição de áudio usadas pelos testes.
#pragma once

// Verdadeiro quando compilado com AddressSanitizer/UBSan: as verificações de velocidade
// são puladas (o código instrumentado é várias vezes mais lento), as de áudio não.
#if defined(__SANITIZE_ADDRESS__)
#define PAD_SANITIZER_BUILD 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define PAD_SANITIZER_BUILD 1
#endif
#endif
#ifdef PAD_SANITIZER_BUILD
constexpr bool kSanitizerBuild = true;
#else
constexpr bool kSanitizerBuild = false;
#endif

#include <vector>
#include <complex>
#include <functional>
#include "PadEngine.h"

namespace padtest {

using Stereo = std::vector<float>; // intercalado L,R

// Motor compartilhado (o "bake" do banco leva ~1 s; os testes reaproveitam).
pad::PadEngine& sharedEngine();
// Deixa o motor silencioso e com parâmetros padrão (ou de um preset).
void resetEngine(pad::PadEngine& e, int presetIndex = -1);

// Renderiza 'seconds' segundos em blocos de 'block' quadros. 'events' é chamado a cada bloco
// com o tempo (s) do início do bloco, para disparar comandos.
Stereo render(pad::PadEngine& e, double seconds, int block = 192,
              const std::function<void(double)>& events = nullptr, bool trackAllocations = false);

double rmsDb(const Stereo& x, size_t frameBegin, size_t frameEnd);
std::vector<double> windowRmsDb(const Stereo& x, int windowFrames); // mono (L+R)/2
float peakAbs(const Stereo& x);
size_t countAbove(const Stereo& x, float threshold);
bool allFinite(const Stereo& x);
double lrCorrelation(const Stereo& x, size_t frameBegin, size_t frameEnd);

// Detector de cliques (descontinuidades isoladas) na região [testBegin, testEnd) em quadros.
// Devolve a maior razão "pico local / mediana da vizinhança". Som limpo: ~1,3–2; clique: > 3.
double clickScore(const Stereo& x, size_t testBegin, size_t testEnd);

// Espectro de magnitude (dB) de um trecho mono com janela de Blackman-Harris. N potência de 2.
std::vector<double> magnitudeSpectrumDb(const std::vector<float>& mono, size_t start, size_t n);
std::vector<float> mono(const Stereo& x);
double spectralCentroid(const std::vector<float>& m, double sampleRate, size_t start, size_t n);
// Frequência do maior pico entre fLo e fHi (com interpolação parabólica).
double peakFrequency(const std::vector<float>& m, double sampleRate, size_t start, size_t n, double fLo, double fHi);

void fft(std::vector<std::complex<double>>& a);

} // namespace padtest
