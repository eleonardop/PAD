#include "Presets.h"

namespace pad {

namespace {
// Ordem dos valores = ordem de ParamId:
//  warm  air  strings shimmerL lowL | warmth bright air low width movement detune |
//  reverb decay size damping shimmer | attack release xfade master |
//  vox texture chorus evolution intensity reverbType preDelay early diffusion stringsSrc textureType
// reverbType: 0 Clássico, 1 Plate (Dattorro), 2 Cloud (Cloud Seed). textureType: 0 piano, 1 órgão.
// O último número de cada preset é o ajuste de nível (trimDb).
const Preset kPresets[] = {
    {"Warm (v0.2)", "Corpo quente e redondo com cordas suaves por baixo. Base segura para qualquer louvor.",
     {0.90f, 0.22f, 0.35f, 0.06f, 0.45f,  0.70f, 0.38f, 0.35f, 0.50f, 1.00f, 0.40f, 0.45f,
      0.42f, 5.5f, 0.70f, 0.60f, 0.06f,  2.5f, 4.0f, 1.8f, 0.80f,
      0.20f, 0.15f, 0.50f, 0.45f, 0.45f, 2.0f, 0.030f, 0.35f, 0.75f, 0.0f, 0.0f,
      0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 4.0f}, 0.7f},
    {"Deep (v0.2)", "Grave profundo e escuro, sala grande e órgão suave ao fundo. Para oração e ministração.",
     {0.85f, 0.14f, 0.30f, 0.00f, 0.75f,  0.80f, 0.25f, 0.20f, 0.62f, 1.05f, 0.35f, 0.40f,
      0.52f, 8.5f, 0.90f, 0.72f, 0.04f,  4.0f, 6.0f, 2.5f, 0.80f,
      0.35f, 0.25f, 0.45f, 0.40f, 0.35f, 2.0f, 0.045f, 0.30f, 0.80f, 0.0f, 1.0f,
      0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 4.0f}, 0.6f},
    {"Air (v0.2)", "Leve e aberto: pouco corpo, ar harmônico respirando e cordas no alto.",
     {0.50f, 0.70f, 0.45f, 0.28f, 0.25f,  0.40f, 0.55f, 0.70f, 0.42f, 1.10f, 0.55f, 0.55f,
      0.55f, 7.0f, 0.80f, 0.45f, 0.22f,  3.0f, 5.0f, 2.0f, 0.80f,
      0.35f, 0.20f, 0.55f, 0.60f, 0.50f, 2.0f, 0.040f, 0.30f, 0.80f, 0.0f, 0.0f,
      0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 4.0f}, 1.8f},
    {"Shimmer (v0.2)", "Brilho em oitavas subindo pela cauda, sempre por trás do corpo, sem aspereza.",
     {0.70f, 0.40f, 0.35f, 0.55f, 0.35f,  0.50f, 0.48f, 0.55f, 0.45f, 1.10f, 0.50f, 0.45f,
      0.60f, 9.0f, 0.85f, 0.52f, 0.50f,  3.0f, 6.0f, 2.2f, 0.78f,
      0.25f, 0.20f, 0.50f, 0.55f, 0.50f, 2.0f, 0.050f, 0.25f, 0.80f, 0.0f, 0.0f,
      0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 4.0f}, 1.3f},
    {"Cinematic (v0.2)", "Largo e dramático: cordas reais à frente, coral e graves presentes, cauda longa.",
     {0.75f, 0.28f, 0.85f, 0.22f, 0.60f,  0.60f, 0.45f, 0.40f, 0.58f, 1.15f, 0.55f, 0.50f,
      0.55f, 10.0f, 1.00f, 0.55f, 0.22f,  3.5f, 7.0f, 2.8f, 0.80f,
      0.45f, 0.30f, 0.45f, 0.65f, 0.55f, 2.0f, 0.060f, 0.40f, 0.80f, 0.0f, 0.0f,
      0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 4.0f}, -2.1f},
    {"Ambient (v0.2)", "Textura em movimento constante, quase sem ataque; flutua por trás da banda.",
     {0.60f, 0.50f, 0.50f, 0.38f, 0.30f,  0.45f, 0.40f, 0.60f, 0.45f, 1.15f, 0.80f, 0.55f,
      0.65f, 12.0f, 0.95f, 0.50f, 0.32f,  6.0f, 9.0f, 3.5f, 0.78f,
      0.40f, 0.55f, 0.55f, 0.85f, 0.45f, 2.0f, 0.050f, 0.25f, 0.85f, 0.0f, 0.0f,
      0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 4.0f}, 0.1f},
    {"Cathedral (v0.2)", "Piano congelado no espectro numa sala de pedra: sustentado, quente e reverente.",
     {0.45f, 0.24f, 0.30f, 0.18f, 0.40f,  0.55f, 0.40f, 0.40f, 0.50f, 1.05f, 0.45f, 0.40f,
      0.60f, 9.0f, 0.95f, 0.60f, 0.14f,  3.0f, 7.0f, 2.5f, 0.80f,
      0.30f, 0.90f, 0.30f, 0.50f, 0.45f, 2.0f, 0.050f, 0.45f, 0.80f, 0.0f, 0.0f,
      0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 4.0f}, 0.1f},
    {"Coral (v0.2)", "Coral em \"oh\" e \"ah\" mudando de vogal devagar, com corpo quente por baixo.",
     {0.45f, 0.28f, 0.25f, 0.14f, 0.35f,  0.55f, 0.42f, 0.40f, 0.50f, 1.05f, 0.45f, 0.50f,
      0.55f, 7.0f, 0.85f, 0.55f, 0.10f,  3.5f, 6.0f, 2.5f, 0.80f,
      0.90f, 0.15f, 0.40f, 0.55f, 0.50f, 2.0f, 0.040f, 0.35f, 0.80f, 0.0f, 0.0f,
      0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 4.0f}, 1.0f},
    // ---- v0.3: presets com papéis musicais (índices 8–15). Os da v0.2 continuam acima, sem
    // texturas nem transições (os parâmetros novos ficam em 0), para comparação e rollback.
    // keys keysType sparkle metal tape activity bloom bloomChance swellTime
    {"Warm Worship", "Pad quente com cordas suaves; notas esparsas de piano suave e uma expansão natural quando o acorde fica.",
     {0.85f, 0.20f, 0.40f, 0.05f, 0.45f,  0.70f, 0.40f, 0.35f, 0.50f, 1.00f, 0.40f, 0.45f,
      0.45f, 6.0f, 0.75f, 0.60f, 0.05f,  2.5f, 4.0f, 1.8f, 0.80f,
      0.15f, 0.00f, 0.45f, 0.50f, 0.45f, 2.0f, 0.030f, 0.35f, 0.75f, 0.0f, 0.0f,
      0.45f, 0.0f, 0.00f, 0.25f, 0.0f, 0.30f, 0.45f, 0.45f, 4.0f}, 0.6f},
    {"Deep Worship", "Escuro e profundo: grave presente, coral crescendo com o tempo, órgão ao fundo e sala grande.",
     {0.85f, 0.12f, 0.15f, 0.00f, 0.80f,  0.85f, 0.22f, 0.20f, 0.65f, 1.00f, 0.35f, 0.40f,
      0.55f, 9.0f, 0.95f, 0.75f, 0.03f,  4.0f, 6.0f, 2.5f, 0.80f,
      0.55f, 0.15f, 0.40f, 0.55f, 0.30f, 2.0f, 0.050f, 0.30f, 0.85f, 0.0f, 1.0f,
      0.25f, 0.0f, 0.00f, 0.35f, 0.0f, 0.15f, 0.50f, 0.40f, 5.0f}, 1.0f},
    {"Cinematic", "Cordas à frente, piano esparso, pratos e transições mais expressivas nas mudanças grandes.",
     {0.65f, 0.25f, 0.85f, 0.18f, 0.60f,  0.60f, 0.48f, 0.40f, 0.58f, 1.15f, 0.50f, 0.50f,
      0.55f, 10.0f, 1.00f, 0.55f, 0.20f,  3.0f, 7.0f, 2.8f, 0.80f,
      0.35f, 0.00f, 0.40f, 0.65f, 0.60f, 2.0f, 0.060f, 0.40f, 0.80f, 0.0f, 0.0f,
      0.55f, 0.0f, 0.25f, 0.60f, 0.0f, 0.45f, 0.80f, 0.70f, 4.0f}, -2.2f},
    {"Ambient", "Camadas atmosféricas em movimento lento: brilho de vibrafone, prato com arco ao longe, cauda longa.",
     {0.60f, 0.45f, 0.30f, 0.35f, 0.30f,  0.45f, 0.42f, 0.60f, 0.45f, 1.15f, 0.80f, 0.55f,
      0.65f, 12.0f, 0.95f, 0.50f, 0.35f,  6.0f, 9.0f, 3.5f, 0.78f,
      0.40f, 0.00f, 0.60f, 0.85f, 0.45f, 2.0f, 0.050f, 0.25f, 0.85f, 0.0f, 0.0f,
      0.30f, 0.0f, 0.45f, 0.40f, 0.0f, 0.35f, 0.60f, 0.50f, 6.0f}, 2.4f},
    {"Vintage", "EP de FM, órgão e cordas de máquina, com caráter de fita e plate curto.",
     {0.70f, 0.15f, 0.20f, 0.00f, 0.50f,  0.75f, 0.40f, 0.25f, 0.55f, 0.90f, 0.45f, 0.55f,
      0.40f, 4.5f, 0.60f, 0.65f, 0.00f,  2.0f, 3.5f, 1.6f, 0.80f,
      0.00f, 0.40f, 0.70f, 0.45f, 0.50f, 1.0f, 0.020f, 0.40f, 0.70f, 1.0f, 1.0f,
      0.55f, 1.0f, 0.00f, 0.00f, 0.6f, 0.40f, 0.30f, 0.35f, 3.0f}, -0.8f},
    {"Cathedral", "Coral e cordas numa sala de pedra enorme, órgão discreto; poucas texturas, muito espaço.",
     {0.50f, 0.22f, 0.65f, 0.12f, 0.45f,  0.55f, 0.38f, 0.40f, 0.50f, 1.05f, 0.40f, 0.40f,
      0.65f, 11.0f, 1.00f, 0.60f, 0.12f,  3.5f, 8.0f, 2.8f, 0.80f,
      0.75f, 0.20f, 0.30f, 0.55f, 0.45f, 2.0f, 0.070f, 0.50f, 0.90f, 0.0f, 1.0f,
      0.00f, 0.0f, 0.15f, 0.30f, 0.0f, 0.15f, 0.50f, 0.45f, 5.0f}, -0.4f},
    {"Ethereal", "Glass, harmônicos e vibrafone no alto, shimmer e ar: leve e aberto, quase sem corpo grave.",
     {0.45f, 0.60f, 0.25f, 0.55f, 0.25f,  0.40f, 0.55f, 0.70f, 0.42f, 1.15f, 0.60f, 0.55f,
      0.62f, 10.0f, 0.90f, 0.45f, 0.45f,  4.0f, 8.0f, 3.0f, 0.78f,
      0.30f, 0.00f, 0.50f, 0.70f, 0.50f, 2.0f, 0.050f, 0.25f, 0.80f, 0.0f, 0.0f,
      0.00f, 0.0f, 0.60f, 0.35f, 0.0f, 0.35f, 0.55f, 0.50f, 5.0f}, 2.8f},
    {"Modern Worship", "Equilíbrio de pad, piano suave, cordas, brilho e transições: o preset para o culto inteiro.",
     {0.80f, 0.25f, 0.45f, 0.15f, 0.55f,  0.62f, 0.45f, 0.40f, 0.55f, 1.05f, 0.45f, 0.50f,
      0.50f, 7.0f, 0.85f, 0.55f, 0.15f,  2.5f, 5.0f, 2.0f, 0.80f,
      0.25f, 0.00f, 0.45f, 0.60f, 0.50f, 2.0f, 0.040f, 0.35f, 0.80f, 0.0f, 0.0f,
      0.50f, 0.0f, 0.25f, 0.45f, 0.0f, 0.40f, 0.60f, 0.55f, 4.0f}, 0.3f},
};
constexpr int kCount = static_cast<int>(sizeof(kPresets) / sizeof(kPresets[0]));
}

int presetCount() { return kCount; }
const Preset& preset(int index) { return kPresets[index < 0 ? 0 : (index >= kCount ? kCount - 1 : index)]; }

} // namespace pad
