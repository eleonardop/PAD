# PAD Worship — Relatório de preparação da v0.3 (Etapa 0)

Legenda: **[verificado]** = fonte pública; **[hipótese]** = a confirmar (de ouvido ou por medida);
**[decisão]** = escolha de projeto. Detalhes e links em `docs/sound-design/`.

## 1. O que foi estudado
* Arquitetura pública dos Nord Stage 4 / Electro 6 / Piano 6 (camadas, faders, efeitos por camada,
  transições sem corte) — `RESEARCH.md` §1.
* Como tecladistas de worship montam o som: piano + pad, drones, camadas que entram por snapshots,
  partes esparsas — §2.
* Swell de prato com baqueta macia, reverse cymbal, arco e raspado — §3.
* EP por FM, tape (wow/flutter), estéreo e mono (Haas × M/S), distância harmônica percebida
  (Tonnetz × voice leading), custo de síntese granular — §4 a §8.
* Técnicas de DSP comparadas em matriz — `DSP_REFERENCE.md`.
* Auditoria completa do motor v0.2 — `ARCHITECTURE.md` §1.
* Bibliotecas CC0 (VSCO 2 CE e VCSL) arquivo por arquivo — `SAMPLE_LICENSES.md`.

## 2. Referências consultadas
Nord (nordkeyboards.com, Sound On Sound, soundtech.co.uk, Sonicstate), Sunday Sounds, The Mystic
Keys, Worship Online, Yamaha Hub, Percussion for Worship, Zildjian, KVR, MusicRadar, Attack
Magazine, Apple Logic Pro (FM), Splice (tape), iZotope e Sound On Sound (Haas e M/S), Milne &
Holland 2016 e Tymoczko (distância harmônica). Links completos em `RESEARCH.md`.

## 3. Técnicas mais promissoras
1. **Eventos musicais condicionais** (maestro com estados por acorde) — o maior ganho esperado de
   "vida" sem acrescentar efeito. [hipótese]
2. **Reprodução de samples one-shot** de instrumentos suaves (piano com pedal, vibrafone de baqueta
   macia) com muito reverb e filtro — o "atmospheric keys" dos tecladistas. [verificado como prática; resultado: hipótese]
3. **FM pré-renderizado** para o EP — timbre clássico, custo zero no áudio. [decisão]
4. **Envelopes de florescimento** sincronizados à troca de acorde. [decisão]
5. **Pan complementar** entre eventos + M/S controlado — largura que sobrevive ao mono. [verificado]

## 4. Técnicas descartadas
| Técnica | Motivo |
|---|---|
| Granular | CPU alta em uso de pad (relatos), sobreposição com o PADsynth |
| Convolução | IRs licenciadas + CPU; os reverbs atuais já cobrem |
| Phase vocoder em tempo real | FFT por bloco no callback; o freeze já é offline |
| Physical modeling | custo de desenvolvimento; samples CC0 dão resultado melhor agora |
| Haas / inversão de fase | quebra o mono (proibido) |
| Ruído filtrado como swell | chiado; o prato real faz o mesmo papel melhor |
| Arpejador / sequência rítmica | chama atenção e compete com a banda |

## 5. Limitações do motor atual
Ver `ARCHITECTURE.md` §1: nenhum evento musical; Evolução sem relação com a harmonia; trocas só
por crossfade; presets que mudam proporções e não papéis; fontes só em loop (sem one-shot nem
reverse); nenhum movimento estéreo.

## 6. Elementos sonoros — curadoria (6 escolhidos)

Matriz (1 a 5; "Risco" = chance de atrapalhar voz/banda, 5 = baixo risco):

| Elemento | Identidade | Encaixe c/ pad | Risco | Utilidade worship | Custo/complexidade (5 = simples) | Decisão |
|---|---|---|---|---|---|---|
| **Piano suave (felt) em notas esparsas** | 5 | 5 | 4 | 5 | 4 | **escolhido** — "atmospheric keys", a textura mais usada por tecladistas |
| **EP (FM próprio)** | 4 | 4 | 4 | 4 | 4 | **escolhido** — base do Vintage e alternativa ao piano |
| **Vibrafone suave (sparkle)** | 4 | 5 | 4 | 4 | 4 | **escolhido** — brilho cristalino tonal, sem a aspereza do glockenspiel |
| **Prato com baqueta macia: swell, wash e arco** | 5 | 4 | 3 | 5 | 3 | **escolhido** — ar e expansão nas transições, como o baterista faz |
| **Motor de reverse/swell** (piano invertido, prato invertido, swell harmônico) | 5 | 4 | 3 | 5 | 3 | **escolhido** — bloom pós-ataque e pre-swell manual |
| **Tape (caráter vintage)** | 3 | 5 | 5 | 3 | 5 | **escolhido** — identidade do preset Vintage, custo mínimo |
| Choir/vocal texture | — | — | — | — | — | **já existe** (Vox PADsynth); recebe papel de segunda camada |
| Strings texture | — | — | — | — | — | **já existe**; recebe papel de segunda camada |
| Glass/crystalline | — | — | — | — | — | **já existe** (Glass); usado também no swell harmônico |
| Soft pluck | 3 | 3 | 2 | 2 | 4 | descartado — parece arpejo, compete com violão/guitarra |
| Analog synth texture | 3 | 3 | 4 | 3 | 4 | descartado — redundante com o Warm aberto |
| Distant crash | 3 | 3 | 2 | 3 | 4 | fundido ao wash de prato (ataque de crash chama atenção) |
| Ride texture | 2 | 2 | 1 | 2 | 4 | descartado — o "ping" é rítmico |
| China filtrada | 2 | 2 | 1 | 1 | 4 | descartado — aspereza mesmo filtrada |
| Metallic shimmer | — | — | — | — | — | coberto pelo prato com arco + shimmer existente |
| Reverse vocal | 3 | 4 | 3 | 3 | 3 | coberto pelo swell harmônico (Glass/Vox crescendo) |
| Noise swell | 2 | 3 | 2 | 3 | 5 | descartado — ruído; o swell vem do prato real |

## 7. Arquitetura proposta
Três peças novas sobre a v0.2, sem substituir nada (`ARCHITECTURE.md` §2): **Conductor**
(maestro: estados do acorde, distância harmônica, decisões), **TexturePlayer** (24 vozes leves de
one-shot, barramentos tonal e metal) e **TapeCharacter**. Parâmetros novos no fim da tabela
(ids 32+), comando novo `Swell`. Regras musicais em `MUSICAL_DESIGN.md`.

## 8. Riscos técnicos
| Risco | Mitigação |
|---|---|
| Eventos soarem "aleatórios" | regras de notas e de tempo; frequência controlada; nada no instante da troca; avaliação de ouvido |
| Texturas embolarem com voz e banda | registro acima de Mi4, nível baixo, filtro, mais reverb que seco |
| Pratos com agudos ásperos | HP 300 Hz, LP ≤ 6 kHz, envio grande ao Cloud (já escuro) |
| Swell manual soar errado se o acorde não mudar | ele "pousa" no acorde atual; partes tonais usam só fundamental e quinta |
| CPU/xRuns | one-shots pré-renderizados; 24 vozes leves; benchmark no aparelho e no ARM64 do CI |
| Memória | piano suave ~15 notas × 9 s, vibrafone 8 × 3 s, pratos ~14 arquivos; reverses gerados só quando a camada está ligada |
| Rollback | branch `versao-0.2-som` antes de qualquer mudança; presets v0.2 mantidos no app |

## 9. Estimativa de CPU e memória [hipótese, a medir]
* CPU: +1 a 2 pontos percentuais de um núcleo ARM no pior caso (texturas + transições), sobre os
  ~10% atuais.
* Memória: +25 a 40 MB quando todas as texturas estão ligadas (piano suave + reverse ~20 MB,
  vibrafone ~3 MB, pratos + reverse ~12 MB, EP + reverse ~6 MB, em int16 estéreo a 48 kHz).
* APK: +~12 MB de FLAC.

## 10. Licenças
Todos os arquivos novos são do VCSL (CC0 1.0, verificado no README e no LICENSE do repositório;
lista exata em `SAMPLE_LICENSES.md`). O EP e os reverses são gerados pelo próprio motor. Nenhuma
dependência de código nova.

## 11. Estratégia de testes e critérios de aceitação
Automáticos (precisam passar para cada etapa avançar):
* tempo real: zero alocações no callback com todas as texturas e transições ativas; nenhum lock;
* notas geradas: 100% pertencem ao acorde (ou 9ª permitida) em 10 000 eventos simulados, em todos
  os tipos de acorde e tons;
* frequência de eventos: com Bloom/BloomChance/atividade em 0 não há eventos; trocas rápidas
  (< 1,5 s) quase não disparam bloom;
* nível: texturas pelo menos 8 dB abaixo do pad no mix padrão; nenhum clipping; true peak ≤ −1 dBTP;
* mono: perda em mono ≤ 3 dB e nenhum vale de cancelamento > 6 dB em bandas de 1/3 de oitava
  (comparando a soma mono com a média de energia L/R), em todos os WAVs de comparação;
* CPU: pior caso abaixo de 25% de um núcleo no ARM64 do CI; estabilidade de 10 minutos com
  texturas e swells;
* xRuns no aparelho: contador visível na tela (medida do usuário).

De ouvido (do usuário): os testes A–M pedidos, gerados em condições equivalentes contra a v0.2.
Critério final: musical, não numérico.
