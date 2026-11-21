---
name: pad-audio-engineering
description: PAD Worship (eleonardop/PAD) audio engineering and sound design rules — use before changing the C++ engine, sounds, samples, presets or tests of the PAD Worship Android app.
---

# PAD Worship — Audio Engineering & Sound Design

Repository: `eleonardop/PAD`. The user is Brazilian, does not program, and judges sound by ear on
the phone. Write user-facing text and docs in Brazilian Portuguese. Never claim sound quality is
approved; the user decides by listening.

## Where things are
- Engine (C++17, no Android deps): `engine/src` — `PadEngine.*` (callback, mix chain), `core/`
  (voices, sources, presets, params, conductor, texture player), `dsp/` (PADsynth, ladder,
  reverbs, ensemble, tape…). Third-party: `engine/third_party` (Cloud Seed MIT, dr_flac).
- Samples: `assets/samples/<set>/` FLAC 16-bit 48 kHz + `manifest.txt`; built by
  `tools/samplelab/prepare_samples.py` from VSCO-2-CE / VCSL / Salamander clones.
- Android: `android/` (Oboe/AAudio, JNI in `app/src/main/cpp/JniBridge.cpp`, test bench UI in
  `MainActivity.kt`, `PadParam` enum must mirror `ParamId` order exactly).
- Docs: `docs/ARQUITETURA.md`, `docs/RELATORIO_ETAPA*.md`, `docs/sound-design/` (RESEARCH,
  DSP_REFERENCE, MUSICAL_DESIGN, SAMPLE_LICENSES, ARCHITECTURE, DECISIONS — read DECISIONS first).
- Rollback branches: `versao-0.1-motor`, `versao-0.2-som`. Tags cannot be pushed from the cloud
  session; use branches.

## Real-time rules (non-negotiable)
- `PadEngine::process` and everything it calls: no allocation, no locks, no I/O, no JNI, no file
  loading, no unbounded loops. Tests count `operator new` during `process()` — keep it at 0.
- Heavy work (PADsynth, FLAC decode, FM rendering, reversing, resampling) runs in the
  `SourceLibrary` loader thread. The callback reads atomic pointers and publishes usage counts;
  a source is freed only after 2 blocks unused.
- New parameters go at the END of `ParamId` (ids are stable), with `ParamInfo` range/smoothing,
  every preset row updated, Kotlin `PadParam` updated.
- Engine always runs at 48 kHz; sub-block 32 samples; Cloud Seed needs `BUFFER_SIZE=256`.

## Sound rules
- Creativity = musical events + layers entering over the chord's life, not more effects.
- Generated notes: only chord tones (+9th in high register for chords that allow it); Pad5 never
  gets a third; register ≥ E4 for textures; no rhythmic grid; nothing exactly on the chord change.
- Mono: no Haas delays, no polarity tricks. Width = complementary panning, M/S, reverb
  decorrelation. Lows < 140 Hz mono. Check mono loss and 1/3-octave cancellation on every
  comparison WAV.
- Cymbals: soft-mallet recordings only, HP ~300 Hz, LP ≤ 6 kHz, mostly reverb send.
- Textures sit ≥ 8 dB under the pad; presets are loudness-trimmed (`Preset::trimDb`) to ±0.5 dB.
- Never compensate a weak source with reverb.

## Licensing
Only CC0 / public domain / licenses explicitly allowing commercial redistribution, verified on
the specific file's repository. No GPL/LGPL/AGPL code (JUCE, Surge excluded). Update
`assets/CREDITOS_E_LICENCAS.txt`, `docs/sound-design/SAMPLE_LICENSES.md` and
`engine/third_party/README.md` for every new source. CC BY (Salamander) needs attribution +
modification notice.

## Testing procedure
1. `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j`
2. `./build/engine/pad_tests --rapido` (all), then full `./build/engine/pad_tests` (10-min test).
3. ASan/UBSan build: `-O1 -g -fsanitize=address,undefined` → `pad_tests --rapido` (speed checks
   auto-skipped under sanitizers).
4. `pad_bench` (offline benchmark — NOT the real callback load; the app shows real callback load
   and Android xRun count separately).
5. Comparison WAVs: `pad_scenarios`/compare tools against the previous version's branch, loudness
   normalized (−20 LUFS, TP ≤ −1 dBTP) plus mono versions and metrics.
6. Measurements on the build server are not proof of phone performance; CI also runs on
   `ubuntu-24.04-arm`; the user runs the in-app benchmark on a Galaxy S23 Ultra (SM8550).
7. Workflow uses `shell: bash` (pipefail) so `| tee` doesn't hide failures. CI artifacts can't be
   downloaded from the cloud session (blob storage blocked) — read job logs via `gh api`.

## Known reference numbers (v0.2, S23 Ultra, 96-frame blocks)
Presets with Cloud reverb: 7.6–8.5% mean, ~10% p99; Plate/Classic ~4%. Memory ~97–109 MB.
