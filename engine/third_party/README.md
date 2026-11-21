# Código de terceiros

| Pasta | Origem | Licença | Uso no PAD | Modificações |
|---|---|---|---|---|
| `cloudseed/` | [Cloud Seed Core](https://github.com/GhostNoteAudio/CloudSeedCore), Ghost Note Audio (commit em `UPSTREAM_COMMIT.txt`) | MIT (ver `cloudseed/LICENSE`). Crédito obrigatório: "Reverb algorithm: Cloud Seed by Ghost Note Audio" | Um dos três algoritmos de reverb (`engine/src/dsp/CloudReverb.*`) | `RandomBuffer::Generate` e os vetores de sementes passaram a usar buffers fixos, sem `std::vector`, para poder mudar parâmetros no thread de áudio sem alocar memória. `strcpy_s` mapeado para `snprintf` fora do MSVC; `MAX_STR_SIZE` definido em `Parameters.h`; `std::sinf`/`std::cosf` trocados por `sinf`/`cosf` (portabilidade com libc++/NDK); buffers de atraso reduzidos para o motor a 48 kHz (`DelayBufferSize`: 96000 em ModulatedDelay e MultitapDelay, 9600 em ModulatedAllpass), o que levou a memória do reverb de 67 MB para 23 MB. Marcadas com `MODIFICADO` no código. O programa de demonstração (`PluginProcessor.cpp`) e os arquivos de projeto do Visual Studio não foram incluídos |
| `dr_libs/dr_flac.h` | [dr_libs](https://github.com/mackron/dr_libs), David Reid (commit em `UPSTREAM_COMMIT.txt`) | Domínio público (Unlicense) ou MIT-0, à escolha | Decodificação dos samples FLAC (`decodeFlac16` em `engine/src/core/Sources.cpp`) | Nenhuma |

Nenhum código GPL/LGPL/AGPL é usado no motor.

## Samples (pasta `assets/samples` do repositório)

| Conjunto | Origem (commit) | Licença | Arquivos |
|---|---|---|---|
| `strings` | VSCO 2 CE, Versilian Studios (`440300901dfe9275fd84e0b7763af1f8443ae62e`) | CC0 1.0 | 16 zonas, violoncelo/viola/violino em naipe, sustain com vibrato |
| `organ` | VCSL, Versilian Studios (`c1ea7bcc3c7309650ab0da9d15c9cd1fbc4a4c7e`) | CC0 1.0 | 17 zonas, órgão de tubos, manual quieto |
| `piano_cathedral` | Salamander Grand Piano V3, Alexander Holm, via sfzinstruments (`3382bf9496bba2486f5ab0de55a264d1dfc38404`) | CC BY 3.0 (atribuição obrigatória; modificações indicadas) | 17 zonas, espectro congelado |

Os samples são gerados por `tools/samplelab/prepare_samples.py` a partir dos repositórios originais. Créditos para o usuário final: `assets/CREDITOS_E_LICENCAS.txt` (aparece no app em "Créditos e licenças dos sons"); textos completos das licenças em `assets/licencas/`.
