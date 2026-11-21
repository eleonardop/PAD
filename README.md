# PAD Worship

Aplicativo Android de pads ambientais para cultos, ministrações e música ao vivo.

**Etapa atual: 1 — motor de áudio profissional** (C++ em tempo real + Oboe + JNI/Kotlin). A interface do músico será a próxima etapa.

## O que já existe

* Motor C++17 com 5 camadas (Warm, Air, Strings/Texture, Shimmer, Low), síntese híbrida (wavetables sem aliasing + samples com interpolação Hermite).
* 12 tonalidades × 10 tipos de acorde, com voice leading (notas comuns continuam soando) e crossfade equal-power ciente de correlação.
* 48 vozes pré-alocadas com voice stealing sem cliques.
* Reverb FDN modulado, shimmer de oitava, filtros de tom, largura estéreo, limitador lookahead.
* 6 presets: Warm, Deep, Air, Shimmer, Cinematic, Ambient.
* App Android com Oboe (AAudio), serviço em primeiro plano para tocar com a tela desligada e uma **bancada de teste provisória**.
* 26 testes automatizados e um gerador de 21 WAVs para avaliação de ouvido.

## Como baixar o APK e os WAVs

1. Abra a aba **Actions** do repositório no GitHub.
2. Entre na execução mais recente de **"Compilar e testar"** (com o ✓ verde).
3. Em **Artifacts**, baixe:
   * `PAD-Worship-APK`: o app para instalar no celular;
   * `PAD-wav-exemplos-e-testes`: os WAVs de avaliação e o resultado dos testes.

## Compilar no computador (desenvolvedores)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/engine/pad_tests                 # testes
./build/engine/pad_wavgen wav_exemplos   # WAVs de avaliação
python3 tools/analysis/spectrogram.py wav_exemplos/09_transicoes_I_V_vi_IV.wav
```

App Android: abrir a pasta `android/` no Android Studio, ou rodar `gradle assembleDebug` dentro dela.

## Documentação

* [`docs/ARQUITETURA.md`](docs/ARQUITETURA.md): arquitetura, decisões de DSP, regras de tempo real.
* [`docs/RELATORIO_ETAPA1.md`](docs/RELATORIO_ETAPA1.md): resultados dos testes, limitações e próximos passos.
