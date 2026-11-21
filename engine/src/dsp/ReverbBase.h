// PAD Engine — interface comum dos reverbs (Clássico/FDN, Plate/Dattorro, Cloud/Cloud Seed).
#pragma once

namespace pad {

struct ReverbParams {
    float decaySeconds = 6.0f;   // RT60
    float size = 0.7f;           // 0..1
    float damping = 0.45f;       // 0..1 (1 = cauda mais escura)
    float diffusion = 0.7f;      // 0..1 (densidade inicial; baixo = ecos mais distintos)
    float preDelaySeconds = 0.03f;
    float early = 0.4f;          // 0..1 nível das reflexões iniciais
    float modulation = 0.5f;     // 0..1 (movimento interno da cauda)
};

class ReverbBase {
public:
    virtual ~ReverbBase() = default;
    virtual void prepare(float sampleRate) = 0;
    virtual void reset() = 0;
    // Chamado no thread de áudio, no máximo uma vez por sub-bloco. Não aloca.
    virtual void setParams(const ReverbParams& p) = 0;
    // Saída 100% molhada.
    virtual void process(const float* inL, const float* inR, float* outL, float* outR, int n) = 0;
};

} // namespace pad
