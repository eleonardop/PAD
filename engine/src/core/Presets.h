// PAD Engine — presets de fábrica (identidades sonoras).
#pragma once

#include "Params.h"

namespace pad {

struct Preset {
    const char* name;
    const char* description;
    float values[kParamCount];
};

int presetCount();
const Preset& preset(int index);

} // namespace pad
