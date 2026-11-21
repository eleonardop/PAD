# PAD Worship

Aplicativo Android de pads ambientais para cultos, ministrações e música ao vivo.

**Etapa atual: 2 — qualidade sonora (v0.2).** A interface do músico será uma etapa futura; o app ainda traz uma bancada de teste.

## O que já existe

* Motor C++17 em tempo real com 7 camadas: **Warm** e **Vox** (síntese PADsynth com morph), **Strings** (naipes reais do VSCO 2 CE ou cordas sintéticas), **Textura** (piano Cathedral ou órgão suave), **Air** (harmônico, sem ruído), **Shimmer** e **Low**.
* Fontes carregadas sob demanda (só o que o preset usa fica na memória), sem alocar nem travar no áudio.
* Filtro ladder por voz, chorus ensemble, macros **Evolução** e **Intensidade**.
* Três reverbs: **Cloud** (Cloud Seed, padrão), **Plate** (Dattorro, leve) e **Clássico** (o da v0.1), com reflexões iniciais, pré-delay e difusão; shimmer de oitava com banda limitada.
* 12 tonalidades × 10 tipos de acorde, voice leading e crossfade sem queda de nível; 48 vozes.
* 8 presets com loudness igualado: Warm, Deep, Air, Shimmer, Cinematic, Ambient, Cathedral, Coral.
* App Android (Oboe/AAudio) com serviço em primeiro plano, bancada de teste com todos os 32 parâmetros, **teste de desempenho no próprio aparelho** e tela de créditos e licenças.
* 43 testes automatizados (x86-64 e ARM64 no CI), 34 WAVs de demonstração e comparação antes × depois com loudness igualado.

## Como baixar o APK e os arquivos de áudio

1. Abra a aba **Actions** do repositório no GitHub.
2. Entre na execução mais recente de **"Compilar e testar"** (com o ✓ verde).
3. Em **Artifacts**, baixe:
   * `PAD-Worship-APK`: o app para instalar no celular;
   * `PAD-comparacao-antes-depois`: os mesmos trechos na v0.1 e na v0.2, no mesmo volume percebido, com espectrogramas e medições;
   * `PAD-resultados-x86_64`: WAVs de demonstração, medidas dos reverbs e resultado dos testes;
   * `PAD-resultados-arm64`: testes e desempenho em processador ARM.

A versão anterior (v0.1) está preservada na branch `versao-0.1-motor`.

## Compilar no computador (desenvolvedores)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/engine/pad_tests                 # testes (--rapido pula o de 10 minutos)
./build/engine/pad_wavgen wav_exemplos   # WAVs de demonstração
./build/engine/pad_bench                 # desempenho (o mesmo teste do botão no app)
./build/engine/pad_reverblab irs && python3 tools/reverblab/analyze_ir.py irs
python3 tools/analysis/spectrogram.py wav_exemplos/13_transicoes_I_V_vi_IV.wav
```

App Android: abrir a pasta `android/` no Android Studio, ou rodar `gradle assembleDebug` dentro dela.

## Documentação

* [`docs/ARQUITETURA.md`](docs/ARQUITETURA.md): arquitetura, decisões de DSP, regras de tempo real.
* [`docs/RELATORIO_ETAPA1.md`](docs/RELATORIO_ETAPA1.md): etapa 1 (motor).
* [`docs/RELATORIO_ETAPA2.md`](docs/RELATORIO_ETAPA2.md): etapa 2 (qualidade sonora): decisões, medições, licenças e limitações.
* [`engine/third_party/README.md`](engine/third_party/README.md) e [`assets/CREDITOS_E_LICENCAS.txt`](assets/CREDITOS_E_LICENCAS.txt): código e sons de terceiros.
