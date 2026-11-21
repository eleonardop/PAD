// PAD Engine — tonalidades, tipos de acorde e voicings de pad.
#pragma once

#include <cstdint>

namespace pad {

enum class ChordType : uint8_t {
    Pad5 = 0,   // fundamental + quinta + oitava (serve para maior e menor — o "pad" clássico de worship)
    Major,
    Minor,
    Sus2,
    Sus4,
    Major7,
    Minor7,
    Dominant7,
    Add9,
    MinorAdd9,
    Count
};

constexpr int kMaxChordNotes = 6;

struct Voicing {
    int notes[kMaxChordNotes] = {0};  // notas MIDI, da mais grave para a mais aguda
    int count = 0;
    int rootNote = 0;                 // nota MIDI da fundamental do voicing (recebe a camada Low)
    int lowNote = 0;                  // nota MIDI da camada Low (fundamental uma ou duas oitavas abaixo)
};

// Monta um voicing aberto, próprio para pad: fundamental perto do centro de registro
// (evita saltos grandes de registro ao trocar de tom), quinta, e as notas de cor
// (terça, sétima, nona) uma oitava acima, onde não embolam.
Voicing buildVoicing(int rootPitchClass, ChordType type);

const char* chordTypeName(ChordType type);
const char* pitchClassName(int pc);

} // namespace pad
