#include "Presets.h"

namespace pad {

namespace {
// Ordem dos valores = ordem de ParamId:
// warm air strings shimmerL lowL | warmth bright air low width movement detune |
// reverb decay size damping shimmer | attack release xfade master
const Preset kPresets[] = {
    {"Warm", "Corpo quente e redondo, agudos contidos. Base segura para qualquer louvor.",
     {0.90f, 0.20f, 0.55f, 0.10f, 0.45f,  0.75f, 0.32f, 0.25f, 0.50f, 0.95f, 0.40f, 0.35f,
      0.40f, 5.0f, 0.65f, 0.60f, 0.10f,  2.5f, 4.0f, 1.8f, 0.80f}},
    {"Deep", "Grave profundo e escuro, sala grande. Para momentos de oração e ministração.",
     {0.85f, 0.15f, 0.45f, 0.05f, 0.75f,  0.80f, 0.22f, 0.15f, 0.62f, 1.05f, 0.35f, 0.45f,
      0.50f, 8.5f, 0.90f, 0.72f, 0.05f,  4.0f, 6.0f, 2.5f, 0.80f}},
    {"Air", "Leve e aberto: pouco corpo, muito ar afinado e cordas suaves no alto.",
     {0.45f, 0.75f, 0.60f, 0.30f, 0.20f,  0.35f, 0.62f, 0.75f, 0.40f, 1.10f, 0.55f, 0.30f,
      0.50f, 6.5f, 0.75f, 0.35f, 0.25f,  3.0f, 5.0f, 2.0f, 0.80f}},
    {"Shimmer", "Brilho em oitavas subindo pela cauda do reverb, sem aspereza.",
     {0.70f, 0.40f, 0.45f, 0.65f, 0.35f,  0.50f, 0.50f, 0.55f, 0.45f, 1.10f, 0.50f, 0.35f,
      0.58f, 9.0f, 0.85f, 0.50f, 0.62f,  3.0f, 6.0f, 2.2f, 0.78f}},
    {"Cinematic", "Largo e dramático: cordas à frente, graves presentes, cauda longa.",
     {0.75f, 0.30f, 0.85f, 0.30f, 0.60f,  0.60f, 0.45f, 0.40f, 0.58f, 1.15f, 0.60f, 0.50f,
      0.55f, 10.0f, 1.00f, 0.55f, 0.30f,  3.5f, 7.0f, 2.8f, 0.80f}},
    {"Ambient", "Textura em movimento constante, quase sem ataque; flutua por trás da banda.",
     {0.60f, 0.55f, 0.65f, 0.45f, 0.30f,  0.45f, 0.40f, 0.60f, 0.45f, 1.15f, 0.85f, 0.55f,
      0.65f, 12.0f, 0.95f, 0.50f, 0.40f,  6.0f, 9.0f, 3.5f, 0.78f}},
};
constexpr int kCount = static_cast<int>(sizeof(kPresets) / sizeof(kPresets[0]));
}

int presetCount() { return kCount; }
const Preset& preset(int index) { return kPresets[index < 0 ? 0 : (index >= kCount ? kCount - 1 : index)]; }

} // namespace pad
