# PAD Worship

Aplicativo Android de pads ambientais para cultos, ministrações e música ao vivo.

**Versão atual: 0.4 — refinamento.**

### Novidades da v0.4
* **Biblioteca enxuta de 8 presets em 4 funções**: FOUNDATION (Warm, Strings), ATMOSPHERE (Air, Ambient), CHARACTER (Vintage, Cinematic), TEXTURES (Soft Piano, Reverse Bloom). Cada um tem corpo, ataque, reverb, largura e microeventos próprios.
* **Microeventos**: pequenas notas de sino, piano, harmônico, reverse, prato com arco, arco de cordas ou rajada de fita — raros, sem BPM, sem repetição, só com notas do acorde (7ª/9ª só no agudo e quando combinam com a tonalidade). Controle **Microeventos** na tela principal (zero = nenhum).
* **Interface limpa**: categorias com botões grandes, preset ativo destacado, 5 controles principais; o resto em **Avançado**, incluindo as versões anteriores dos presets (v0.2 e v0.3).
* Versões anteriores preservadas: branches `versao-0.1-motor`, `versao-0.2-som` e `versao-0.3-ambient`.

### v0.3
* **Maestro musical**: o motor acompanha a vida de cada acorde (estabelecimento → expansão → transformação → sustentação), faz a segunda camada (cordas, coral, glass) crescer com o tempo e decide transições pela distância harmônica entre os acordes.
* **Texturas esparsas** com notas só do acorde: piano suave com pedal (VCSL, CC0), EP por FM próprio e vibrafone de baqueta macia (VCSL, CC0).
* **Transições**: *bloom* automático depois da troca (controlado por intensidade e frequência) e **Swell manual** (botão) com piano/EP invertido, prato crescente e swell harmônico, com pico no fim do tempo escolhido.
* **Pratos** de baqueta macia e arco (VCSL, CC0), sempre filtrados e no fundo.
* **Fita** (wow/flutter/saturação, sem chiado) para o preset Vintage.
* 8 presets novos com papéis diferentes: Warm Worship, Deep Worship, Cinematic, Ambient, Vintage, Cathedral, Ethereal, Modern Worship. Os 8 da v0.2 continuam no app com "(v0.2)" no nome.
* Versões anteriores preservadas: branches `versao-0.1-motor` e `versao-0.2-som`.

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
   * `PAD-v03-comparacao-A-M`: os testes A–M da v0.3 contra a v0.2, no mesmo volume percebido, em estéreo e em mono, com espectrogramas e medições;
   * `PAD-resultados-x86_64`: WAVs de demonstração, medidas dos reverbs e resultado dos testes;
   * `PAD-resultados-arm64`: testes e desempenho em processador ARM.

As versões anteriores estão preservadas nas branches `versao-0.1-motor` e `versao-0.2-som`.

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
* [`docs/RELATORIO_PREPARACAO_V03.md`](docs/RELATORIO_PREPARACAO_V03.md) e [`docs/RELATORIO_V03.md`](docs/RELATORIO_V03.md): pesquisa e entrega da v0.3.
* [`docs/sound-design/`](docs/sound-design/): pesquisa, técnicas, regras musicais, licenças, arquitetura e decisões (base de conhecimento do projeto).
* [`engine/third_party/README.md`](engine/third_party/README.md) e [`assets/CREDITOS_E_LICENCAS.txt`](assets/CREDITOS_E_LICENCAS.txt): código e sons de terceiros.
