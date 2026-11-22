#include "MusicTheory.h"

namespace pad {

namespace {
// Intervalos (semitons acima da fundamental) de cada voicing.
struct Shape { int n; int iv[kMaxChordNotes]; };
constexpr Shape kShapes[static_cast<int>(ChordType::Count)] = {
    {4, {0, 7, 12, 19}},           // Pad5:     1 5 8 12
    {5, {0, 7, 12, 16, 19}},       // Major:    1 5 8 10 12
    {5, {0, 7, 12, 15, 19}},       // Minor:    1 5 8 b10 12
    {5, {0, 7, 12, 14, 19}},       // Sus2:     1 5 8 9 12
    {5, {0, 7, 12, 17, 19}},       // Sus4:     1 5 8 11 12
    {5, {0, 7, 11, 16, 19}},       // Major7:   1 5 7 10 12
    {5, {0, 7, 10, 15, 19}},       // Minor7:   1 5 b7 b10 12
    {5, {0, 7, 10, 16, 19}},       // Dom7:     1 5 b7 10 12
    {6, {0, 7, 12, 14, 16, 19}},   // Add9:     1 5 8 9 10 12
    {6, {0, 7, 12, 14, 15, 19}},   // mAdd9:    1 5 8 9 b10 12
};
constexpr int kRegisterCenter = 51; // Eb3: fundamental fica entre A#2 (46) e A3 (57)
}

Voicing buildVoicing(int rootPitchClass, ChordType type, int style) {
    const int pc = ((rootPitchClass % 12) + 12) % 12;
    int ti = static_cast<int>(type);
    if (ti < 0 || ti >= static_cast<int>(ChordType::Count)) ti = 0;
    const Shape& s = kShapes[ti];

    // Fundamental mais próxima do centro (empate: a mais grave).
    int root = 48 + pc; // C3..B3
    while (root - kRegisterCenter > 6) root -= 12;
    while (kRegisterCenter - root > 5) root += 12;

    Voicing v;
    if (style == 1) {
        int iv[kMaxChordNotes];
        int n = 0;
        // Só quando a quinta de baixo não desce além de Sol2 (~98 Hz); em Si e Lá# a fundamental
        // já está no grave e a quinta fica por cima, como no voicing padrão.
        if (root >= 48) iv[n++] = -5;
        iv[n++] = 0;
        iv[n++] = 7;
        iv[n++] = 12;
        for (int i = 0; i < s.n && n < kMaxChordNotes; ++i) {
            const int x = s.iv[i];
            if (x == 0 || x == 7 || x == 12 || x == 19) continue;
            iv[n++] = x < 12 ? x + 12 : x;
        }
        if (n < kMaxChordNotes) iv[n++] = 19;
        for (int i = 1; i < n; ++i)
            for (int j = i; j > 0 && iv[j - 1] > iv[j]; --j) { const int t = iv[j]; iv[j] = iv[j - 1]; iv[j - 1] = t; }
        v.count = n;
        for (int i = 0; i < n; ++i) v.notes[i] = root + iv[i];
    } else {
        v.count = s.n;
        for (int i = 0; i < s.n; ++i) v.notes[i] = root + s.iv[i];
    }
    v.rootNote = root;
    // Camada Low: fundamental na faixa 33..44 (A1..G#2, ~55–104 Hz), acima da região do bumbo
    // e abaixo do corpo do baixo elétrico na maior parte das tonalidades.
    int low = root - 12;
    while (low > 44) low -= 12;
    while (low < 33) low += 12;
    v.lowNote = low;
    return v;
}

const char* chordTypeName(ChordType type) {
    static const char* kNames[] = {"Pad5", "Maior", "Menor", "Sus2", "Sus4", "Maj7", "m7", "7", "Add9", "mAdd9"};
    const int i = static_cast<int>(type);
    return (i >= 0 && i < static_cast<int>(ChordType::Count)) ? kNames[i] : "?";
}

const char* pitchClassName(int pc) {
    static const char* kNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return kNames[((pc % 12) + 12) % 12];
}

} // namespace pad
