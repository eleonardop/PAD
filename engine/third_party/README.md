# Código de terceiros

| Pasta | Origem | Licença | Uso no PAD | Modificações |
|---|---|---|---|---|
| `cloudseed/` | [Cloud Seed Core](https://github.com/GhostNoteAudio/CloudSeedCore), Ghost Note Audio (commit em `UPSTREAM_COMMIT.txt`) | MIT (ver `cloudseed/LICENSE`). Crédito obrigatório: "Reverb algorithm: Cloud Seed by Ghost Note Audio" | Um dos três algoritmos de reverb (`engine/src/dsp/CloudReverb.*`) | `RandomBuffer::Generate` e os vetores de sementes passaram a usar buffers fixos, sem `std::vector`, para poder mudar parâmetros no thread de áudio sem alocar memória. `strcpy_s` mapeado para `snprintf` fora do MSVC. Marcadas com `MODIFICADO` no código. O programa de demonstração (`PluginProcessor.cpp`) e os arquivos de projeto do Visual Studio não foram incluídos |
| `dr_libs/dr_flac.h` | [dr_libs](https://github.com/mackron/dr_libs), David Reid (commit em `UPSTREAM_COMMIT.txt`) | Domínio público (Unlicense) ou MIT-0, à escolha | Decodificação dos samples FLAC (`engine/src/core/AudioDecode.cpp`) | Nenhuma |

Nenhum código GPL/LGPL/AGPL é usado no motor.
