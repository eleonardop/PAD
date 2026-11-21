# Procedência e licença de cada sample

Regra do projeto: só entra no APK material **CC0, domínio público ou licença que permita
explicitamente redistribuição comercial**, verificada no repositório de origem do arquivo
(não no site em geral). Licença ambígua = não usar. Nada entra "porque parece com um Nord".

## Fontes em uso

| Conjunto no app | Arquivos de origem | Autor | Origem (commit) | Licença | Redistribuição / uso comercial no APK |
|---|---|---|---|---|---|
| `samples/strings` (v0.2) | `Strings/{Cello,Viola,Violin} Section/sus*vib*` | Versilian Studios (Sam Gossner) | [VSCO-2-CE](https://github.com/sgossner/VSCO-2-CE) `440300901dfe` | CC0 1.0 (`LICENSE` do repositório) | sim, sem condições |
| `samples/organ` (v0.2) | `Aerophones/Edge-blown Aerophones/Pipe Organ/Quiet/*` | Versilian Studios | [VCSL](https://github.com/sgossner/VCSL) `c1ea7bcc3c73` | CC0 1.0 (README: "Public Domain… even make commercial software, no royalties, no credit") | sim, sem condições |
| `samples/piano_cathedral` (v0.2) | Salamander Grand Piano V3, notas em terças menores | Alexander Holm | [sfzinstruments/SalamanderGrandPiano](https://github.com/sfzinstruments/SalamanderGrandPiano) `3382bf9496bb` | CC BY 3.0 | sim, **com atribuição e indicação de modificação** (feitas na tela "Créditos e licenças" e em `assets/CREDITOS_E_LICENCAS.txt`) |
| `samples/piano_soft` (**v0.3**) | `Chordophones/Zithers/Grand Piano, Steinway B/Sus/JHPiano_Sus_Close_{C4…E6}_vl2_rr1.wav` (15 notas, tons inteiros, pedal de sustain, camada dinâmica mais suave) | Versilian Studios | VCSL `c1ea7bcc3c73` | CC0 1.0 | sim, sem condições |
| `samples/vibes` (**v0.3**) | `Idiophones/Struck Idiophones/Vibraphone/Soft Mallets/Vibes_soft_{E3,G3,B3,D4,F4,A4,C5,E5}_v1_*.wav` (baqueta macia, dinâmica suave) | Versilian Studios | VCSL `c1ea7bcc3c73` | CC0 1.0 | sim, sem condições |
| `samples/cymbal` (**v0.3**) | `Idiophones/Struck Idiophones/Suspended Cymbal 1/`: `susCymb1_cresc_{2s,4s,7.5s}`, `susCymb1_roll_mp1_nloop`, `susCymb1_bow_{7,13}`, `susCymb1_hit_pp1`; `Suspended Cymbal 2/`: `susCymb2_cresc_{2.5s2,4s,7s}`, `susCymb2_roll_pp1_nloop`, `susCymb2_bowed_{2,4}`, `susCymb2_hit_pp1` | Versilian Studios | VCSL `c1ea7bcc3c73` | CC0 1.0 | sim, sem condições |

Processamento feito pelo PAD (em `tools/samplelab/prepare_samples.py`): conversão para 48 kHz,
recorte, fade, filtros suaves, normalização e FLAC 16 bits. Os reverses (piano e prato) e o EP
são gerados pelo próprio motor no carregamento, a partir desses arquivos ou por síntese.

## Avaliadas e não usadas

| Fonte | Motivo |
|---|---|
| VCSL `Electrophones/TX81Z/*` (FM Piano) | a nota de origem diz que são patches criados pelo usuário e gravados do hardware Yamaha; o repositório é CC0, mas preferi não depender de gravação de hardware comercial quando o mesmo resultado pode ser sintetizado por FM próprio |
| VCSL `Glockenspiel`, `Triangles`, `Mark Trees` | agudos fortes demais para ficar atrás da voz sem aspereza |
| VCSL `Wine Glasses` | só 4 notas (D#4–D5): transposição demais para cobrir o registro |
| VCSL `Gong 2` | arquivos MP3 (perda de qualidade na origem) |
| VCSL `Hi-Hat`, `Clash Cymbals`, `Suspended Cymbal` com baqueta de madeira | ataque e ritmo chamam atenção (contra o objetivo) |
| Bancos comerciais ou "free for personal use" | licença não permite redistribuição no APK |

O texto completo do CC0 está em `assets/licencas/VCSL_CC0.txt` e `assets/licencas/VSCO-2-CE_CC0.txt`;
o do CC BY 3.0 em `assets/licencas/Salamander_CC-BY-3.0.txt`.
