# PAD Worship v0.3 — Relatório técnico (instrumento de ambient worship)

Este relatório descreve o que a v0.3 acrescentou, como foi medido e o que **não** foi
verificado. **Ele não diz que o som ficou melhor**: as medidas mostram o que mudou; o julgamento
musical é seu, de ouvido, no celular e no som da igreja.

A pesquisa e as decisões que levaram a esta implementação estão em
`docs/RELATORIO_PREPARACAO_V03.md` e em `docs/sound-design/` (RESEARCH, DSP_REFERENCE,
MUSICAL_DESIGN, SAMPLE_LICENSES, ARCHITECTURE, DECISIONS).

## 1. O que mudou, em uma frase por parte

| Papel | O que faz na v0.3 | Fonte do som |
|---|---|---|
| PAD | o corpo da v0.2, sem mudança de timbre | PADsynth (sintetizado) + cordas VSCO (sample) |
| MOTION | **maestro**: a vida do acorde passa por estabelecimento → expansão → transformação → sustentação; a segunda camada (cordas, coral, glass) cresce com o tempo; o brilho, o envio ao reverb e a largura evoluem | — (controle) |
| TEXTURE | notas esparsas, sem ritmo, **só do acorde**, em linha melódica que respira; piano suave com pedal ou EP | piano: sample VCSL; EP: **FM próprio** (sintetizado) |
| SPARKLE | vibrafone de baqueta macia no registro alto + Glass que cresce em acordes longos | vibrafone: sample VCSL; Glass: PADsynth |
| TRANSITION | **bloom** depois da troca (pela distância harmônica e pela duração do acorde anterior) e **Swell manual** (botão) com pico no fim do tempo escolhido | piano/EP invertido e prato invertido (gerados pelo app), rulo crescente de prato (VCSL), Glass |
| AIR | Air da v0.2 + envio ao reverb que evolui e cresce no bloom | PADsynth |
| LOW | igual à v0.2 | wavetable |
| (caráter) | **Fita** para o Vintage: wow/flutter, saturação e perda de agudos, sem chiado | processamento |

Nada da v0.2 foi removido: PADsynth, Warm/Vox, Air harmônico, Strings, Texture, Chorus,
Evolução, ladder ZDF, Cloud Seed, Plate, Shimmer, voice leading, 48 vozes e a arquitetura de
tempo real continuam iguais. Os 8 presets da v0.2 estão no app com "(v0.2)" no nome, sem
texturas: soam como na v0.2.

## 2. Curadoria: 6 elementos escolhidos, 11 descartados

Escolhidos (justificativa completa e matriz em `RELATORIO_PREPARACAO_V03.md` §6):
1. **Piano suave em notas esparsas** ("atmospheric keys") — a textura mais usada por tecladistas de worship.
2. **EP por FM próprio** — base do Vintage; sem depender de gravação de hardware comercial.
3. **Vibrafone suave (sparkle)** — brilho tonal sem a aspereza de glockenspiel ou triângulo.
4. **Pratos de baqueta macia e arco** — ar e expansão, como o baterista faz nos swells.
5. **Motor de reverse/swell** — bloom automático e swell manual, com piano e prato invertidos.
6. **Fita** — caráter vintage com custo quase zero.

Descartados: soft pluck (vira arpejo), analog synth texture (redundante com o Warm aberto),
distant crash (o ataque chama atenção; fundido ao wash), ride (ping rítmico), china (aspereza),
noise swell (ruído), reverse vocal (coberto pelo swell harmônico), metallic shimmer separado
(coberto pelo prato com arco + shimmer), granular, convolução e physical modeling
(custo/benefício; `DSP_REFERENCE.md`). Choir, strings e glass já existiam e ganharam o papel de
segunda camada.

## 3. Como o maestro decide (resumo de `MUSICAL_DESIGN.md`)

* **Distância harmônica** (0–1): notas comuns + círculo de quintas (menores na posição da
  relativa maior, como no Tonnetz) + troca de modo. Medido: G→Em 0,32; G→C 0,39; G→D 0,39;
  G→Bb 0,51; G→C# 0,85.
* **Idade do organismo** não volta a zero na troca: `idade × (1 − distância)`. Mudança pequena
  mantém o ambiente; mudança grande recomeça o ciclo.
* **Bloom**: força `Bloom · (0,35 + 0,65·d) · fator de duração` (0 para acordes de até 1 s,
  1 a partir de 4 s); sorteio `BloomChance · (0,4 + 0,6·d)`; no mínimo 4 s entre blooms. Inclui
  um pouco mais de reverb e largura, notas do acorde no Glass entrando por baixo e, em mudanças
  grandes, um rulo suave de prato.
* **Swell manual**: piano/EP invertido na fundamental e na quinta, rulo crescente de prato real,
  prato invertido e Glass, todos alinhados para o pico cair no fim de SwellTime. Se o acorde
  mudar antes, as partes tonais saem em 0,4 s e o bloom do novo acorde é forçado; se não
  mudar, o swell pousa no próprio acorde.
* **Notas**: só do acorde; 9ª apenas no vibrafone e em acordes que a admitem; Pad5 nunca recebe
  terça; teclas entre Mi4 e Ré6, vibrafone entre Dó5 e Mi6; saltos de até uma 5ª; nunca a
  mesma nota três vezes seguidas; nunca antes de 0,6 s depois da troca; intervalos
  log-normais (sem grade rítmica).
* **Estéreo**: pan complementar entre eventos, deriva lenta em velocidades diferentes (teclas
  ~40 s, vibrafone ~65 s), +12% de lado só durante o bloom; nada de Haas nem inversão de fase.

## 4. Controles novos (ids 32–40, no fim da tabela)

| Controle | Faixa | O que faz |
|---|---|---|
| Teclas esparsas | 0–1 | volume das notas de piano/EP |
| Tipo de teclas | piano suave / EP | fonte das teclas e do reverse |
| Brilho de vibrafone | 0–1 | volume do sparkle |
| Pratos | 0–1 | volume dos pratos (bloom, arco) |
| Fita | 0–1 | profundidade do caráter de fita |
| Atividade | 0–1 | frequência das notas (média de ~11 s a ~3 s entre notas) |
| Bloom (intensidade) | 0–1 | força das transições automáticas |
| Bloom (frequência) | 0–1 | chance de transição em cada troca |
| Tempo do Swell | 1,5–8 s | tempo até o pico do swell manual |
| **Botão SWELL** | — | dispara o pre-swell manual no acorde atual |

## 5. Presets da v0.3

| Preset | Identidade | Camadas que definem o papel |
|---|---|---|
| Warm Worship | pad quente, cordas suaves, piano esparso, expansão natural | Warm alto, cordas 0,40, teclas 0,45, poucas transições |
| Deep Worship | escuro, grave presente, coral que cresce, órgão ao fundo | Low 0,80, coral 0,55, órgão, sala de 9 s |
| Cinematic | cordas à frente, piano, pratos, transições expressivas | cordas 0,85, Bloom 0,8, pratos 0,6, 10 s de cauda |
| Ambient | atmosférico, lento, vibrafone, prato com arco ao longe | Evolução 0,85, Atividade 0,35, brilho 0,45, 12 s |
| Vintage | EP de FM, órgão, cordas de máquina, fita, plate curto | EP, órgão, cordas sintéticas, Fita 0,6, Plate |
| Cathedral | coral e cordas numa sala enorme, órgão discreto | coral 0,75, cordas 0,65, 11 s, reflexões 0,5 |
| Ethereal | glass, harmônicos e vibrafone no alto, quase sem grave | Glass 0,55, Air 0,60, vibrafone 0,6, Low 0,25 |
| Modern Worship | equilíbrio de tudo, para o culto inteiro | pad + piano + cordas + brilho + pratos + transições |

Trims de loudness: os 16 presets ficam entre −20,9 e −20,4 LUFS na progressão de teste.

## 6. Comparação v0.2 × v0.3 (testes A–M)

Gerados por `tools/compare/ScenariosV3.cpp`, **o mesmo programa** compilado contra a v0.2
(branch `versao-0.2-som`) e contra a v0.3, com os mesmos acordes, durações e blocos. Nos testes
A–D, G e E a referência é **Warm (v0.2)** contra **Modern Worship (v0.3)**; no F cada preset novo
é comparado com o mais parecido da v0.2 (Ethereal ↔ Air; Vintage e Modern ↔ Warm). Os WAVs de
escuta estão normalizados em −20 LUFS com um ganho único por arquivo (dinâmica e timbre
intactos), e cada um tem uma versão mono.

Medidas dos arquivos originais (tabela completa com os 41 arquivos em `medicoes.md` do pacote):

| Teste | Versão | LUFS | LRA | True peak | Centróide | > 10 kHz | Correl. L/R | Perda em mono | Pior banda mono |
|---|---|---|---|---|---|---|---|---|---|
| A sustentado 60 s | v0.2 | −20,3 | 1,0 | −8,9 | 501 Hz | −66,4 dB | 0,34 | −1,9 dB | −2,2 dB |
| | v0.3 | −19,6 | 0,9 | −7,8 | 559 Hz | −60,7 dB | 0,30 | −2,1 dB | −2,3 dB |
| B progressão 92 s | v0.2 | −20,5 | 0,9 | −9,1 | 416 Hz | −69,0 dB | 0,36 | −1,8 dB | −2,1 dB |
| | v0.3 | −20,0 | 1,2 | −9,1 | 467 Hz | −64,3 dB | 0,34 | −2,0 dB | −2,3 dB |
| D troca rápida | v0.2 | −20,9 | 1,1 | −9,9 | 431 Hz | −68,3 dB | 0,36 | −1,8 dB | −2,2 dB |
| | v0.3 | −21,0 | 2,5 | −10,1 | 465 Hz | −64,0 dB | 0,36 | −1,9 dB | −2,3 dB |
| E Evolução 0% | v0.2 | −20,6 | 1,0 | −9,0 | 430 Hz | −68,9 dB | 0,34 | −1,9 dB | −2,2 dB |
| | v0.3 | −20,7 | 1,2 | −9,7 | 451 Hz | −64,7 dB | 0,36 | −2,0 dB | −2,3 dB |
| E Evolução 100% | v0.2 | −20,6 | 1,1 | −9,4 | 426 Hz | −68,1 dB | 0,36 | −1,9 dB | −2,2 dB |
| | v0.3 | −19,3 | 1,7 | −8,0 | 523 Hz | −61,8 dB | 0,31 | −2,0 dB | −2,4 dB |
| L mix completo | v0.3 | −20,1 | 1,1 | −8,6 | 471 Hz | −63,9 dB | 0,35 | −2,0 dB | −2,3 dB |
| J pratos isolados | v0.3 | −31,7 | 21,7 | −12,4 | 816 Hz | −69,5 dB | 0,43 | −1,5 dB | −2,0 dB |
| K reverse/swell isolado | v0.3 | −32,4 | 22,1 | −15,9 | 1033 Hz | −61,6 dB | 0,32 | −1,8 dB | −2,9 dB |

Como ler:
* **Evolução agora tem efeito musical mensurável**: na v0.2 o centróide ficava em ~430 Hz em
  qualquer Evolução; na v0.3 sobe de 451 Hz (0%) a 523 Hz (100%) e a faixa dinâmica (LRA) de 1,2
  para 1,7 LU — é a segunda camada crescendo e o timbre se transformando com o tempo do acorde.
* **Acorde sustentado (A)**: em 60 s o mesmo acorde ganha brilho e corpo (centróide +58 Hz),
  sem ficar mais alto depois do ajuste de loudness.
* **Trocas rápidas (D)**: o maestro fica quieto (nenhum bloom em trocas de 1,5 s); o LRA maior vem
  das notas de textura.
* **Texturas discretas**: as notas de piano/vibrafone ficam com pico ~7 dB abaixo do pad (janelas
  de 400 ms) e ~20 dB abaixo na média; por isso a diferença de loudness entre G, H, I e L é
  menor que 0,1 dB. Se ficaram discretas demais ou de menos, o controle "Teclas esparsas" e
  "Brilho de vibrafone" ajustam — é um dos pontos que mais dependem do seu ouvido.
* **Agudos**: a energia acima de 10 kHz subiu ~5 dB (de −69 para −64 dB), ainda muito abaixo da
  v0.1 (−17 a −43 dB). Vem do piano, do vibrafone e do Glass das transições.
* **Clipping**: zero amostras saturadas em todos os 41 arquivos; true peak sempre ≤ −7,8 dBTP.

### Mono (teste M)
Em todos os 41 arquivos (estéreo × mono):
* perda de volume em mono entre −1,4 e −2,3 dB (a v0.2 ficava entre −1,8 e −2,2 dB);
* **nenhuma banda de 1/3 de oitava cancela mais que 2,9 dB** (pior caso: K, reverse isolado);
  um cancelamento real por fase apareceria como um vale de 6 dB ou mais;
* desvio de timbre em mono (o quanto uma banda muda além da perda média) até 2,2 dB.
O teste automático `mono_v03_sem_cancelamento` exige perda > −3 dB e nenhuma banda abaixo de
−6 dB no mix completo (medido: −1,93 dB e −2,40 dB no ARM64).

## 7. Tempo real, CPU e memória

**Duas medidas diferentes, que não se substituem:**
1. **Benchmark fora do callback** (`pad_bench`, botão "Teste de desempenho" no app): renderiza
   fora do tempo real num thread comum.
2. **Carga real do callback** (topo da tela do app: "Carga do áudio", média e pior bloco) e
   **xRuns** contados pelo próprio Android ("falhas de buffer"). Essas só existem no aparelho.

Benchmark no ARM64 do CI (Neoverse-N2, blocos de 192 amostras):

| Caso | Média | p99 | Pior bloco | Vozes (pad + texturas) | Memória |
|---|---|---|---|---|---|
| presets v0.2 (8) | 10,0–10,7% | ≤ 12,9% | ≤ 13,4% | 10–12 | 97–109 MB |
| presets v0.3 (8) | 7,3–11,3% | ≤ 13,0% | ≤ 14,0% | 11–17 | 111–160 MB |
| pior caso v0.3 (tudo no máximo + texturas + swell em cada troca) | 11,7% | 13,8% | 14,5% | 28 | 169 MB |

* Custo das texturas e transições: +0,5 a 1 ponto percentual nos presets e +1,8 no pior caso.
  As 48 vozes do pad **não** foram aumentadas: o `TexturePlayer` tem 24 vozes leves próprias (+4
  de reserva); no pior caso usou 19–28 delas, sem nenhum roubo nos testes normais.
* Memória: +2 a 51 MB conforme o preset (as fontes só carregam se o preset as usa). Maior:
  Deep Worship, 160 MB; pior caso 169 MB.
* O carregamento das fontes novas leva até ~1,2 s no ARM64 (Vintage: o EP por FM é calculado
  na hora); uma fonte que chega depois do acorde entra com rampa.
* Nenhuma alocação no callback (contador de `operator new` nos testes, com todas as texturas,
  swells e trocas rápidas); nada de travas, I/O ou JNI no áudio; reverses e EP gerados no
  thread carregador.

**No seu S23 Ultra (v0.2)** o mesmo benchmark deu 7,6–8,5% para os presets: espero algo
como 8–10% para os presets da v0.3, mas isso é estimativa — rode o teste de novo.

## 8. Testes automatizados

57 testes (56 com `--rapido`). Novos da v0.3:

| Teste | Verifica | Resultado |
|---|---|---|
| `maestro_distancia_harmonica` | ordem das distâncias | ok |
| `maestro_notas_pertencem_ao_acorde` | 12 tons × 10 tipos × 40 s: notas fora do acorde, fora do registro, antes de 0,5 s, terças no Pad5 | 1766 notas, 0 erros |
| `maestro_linha_melodica_sem_saltos_grandes` | saltos > 5ª, 3 repetições | 0 e 0 |
| `maestro_controles_zerados_sem_eventos` | Atividade/Bloom = 0 | 0 eventos |
| `maestro_bloom_condicionado` | trocas rápidas e distância | 0 blooms em 75 trocas rápidas; distantes > vizinhas |
| `ep_fm_afinado` | afinação e fim em silêncio do EP | desvio 0,04 cent |
| `reverse_cresce_ate_o_fim` | reverse com pico no fim | ok |
| `texturas_tempo_real_sem_alocacao_e_sem_clip` | mix completo, swells, trocas de 0,35 s | 0 alocações, pico 0,44 |
| `texturas_abaixo_do_pad` | texturas ≥ 8 dB abaixo do pad | ok |
| `swell_manual_pico_no_fim` | pico a 3 s e a 6,5 s | 2,9 s e 6,4 s |
| `mono_v03_sem_cancelamento` | perda e bandas em mono | −1,93 / −2,40 dB |
| `tape_seguro_em_mono` | fita não muda a relação L/R | ok |
| `texturas_fontes_sob_demanda` | carrega e libera piano/pratos | +55 MB e volta |
| `v03_estabilidade_2min` | 2 min com swells a cada 20 s | RMS ±0,3 dB, 0 alocações |

Resultados: x86-64 57/57, sanitizers (ASan + UBSan) 56/56, ARM64 57/57, incluindo os 10
minutos simulados. Um teste da v0.2 (`motor_fontes_sob_demanda_sem_clique`) teve o coral
aumentado de 0,8 para 1,0 para ficar acima do ruído de medida; os limites não mudaram.

## 9. Licenças

Novos sons: só VCSL (Versilian Studios), **CC0 1.0** — piano Steinway B com pedal, vibrafone de
baqueta macia, pratos suspensos (rulo crescente, rulo pp, arco, ataque pp). Lista arquivo por
arquivo, commit de origem e condições em `docs/sound-design/SAMPLE_LICENSES.md`. O EP e todos os
reverses são gerados pelo app. Nenhuma dependência de código nova. Créditos atualizados em
`assets/CREDITOS_E_LICENCAS.txt` (tela "Créditos e licenças dos sons").

## 10. Como testar no aparelho

1. Instale o APK novo (`PAD-Worship-APK`, versão 0.3.0-ambient). Os presets da v0.3 aparecem
   primeiro na lista; os da v0.2 vêm depois, com "(v0.2)".
2. **Modern Worship**, toque um acorde e **deixe 40–60 s**: o som deve ir abrindo aos poucos
   (cordas, depois coral e glass), com notas de piano esparsas.
3. **Progressão** (ex.: G–D–Em–C, 6–8 s por acorde): repare no bloom depois das trocas maiores e
   no silêncio do maestro em trocas rápidas.
4. **SWELL**: com um acorde soando, aperte SWELL e troque o acorde quando o swell chegar ao pico
   (tempo padrão 4 s; ajuste em "Tempo do Swell"). Teste também apertar e não trocar.
5. **Vintage** (EP e fita), **Ethereal** (vibrafone e glass), **Deep Worship** (coral crescendo).
6. Os controles de "Texturas e transições (v0.3)" ajustam volume das teclas, vibrafone e pratos,
   a atividade e a intensidade/frequência das transições.
7. Desempenho: "Teste de desempenho completo" (benchmark) e, durante o uso, "Carga do áudio" e
   "falhas de buffer" no topo da tela (carga real do callback). Me mande os dois.
8. Mono: se puder, ouça pela caixa da igreja somada em mono, ou compare as pastas `estereo/` e
   `mono/` do pacote de comparação.

## 11. Limites conhecidos e o que precisa do seu ouvido

* **Se as texturas soam musicais ou "aleatórias"** — é o critério central e nenhum teste mede
  isso. As regras evitam notas erradas e ritmo, mas a escolha das notas e dos intervalos pode
  precisar de ajuste.
* **Nível das texturas**: calibrado para ~7 dB abaixo do pad no pico. Pode estar discreto demais
  num celular ou com a banda tocando.
* **Swell manual**: depende de o músico trocar o acorde perto do pico; sem lookahead não há como
  o motor saber quando a troca vem.
* **Pratos**: filtrados de 300 Hz a ~6 kHz; avaliar se ainda há aspereza no PA.
* **Piano invertido** usa só fundamental e quinta do acorde atual; se o novo acorde não tiver
  essas notas, elas saem em 0,4 s — avaliar se a saída é perceptível.
* **EP por FM**: timbre próprio, não um Rhodes gravado; pode soar "digital" demais.
* **Memória** maior (até 160–169 MB): sem problema no S23 Ultra; aparelhos com 3–4 GB podem
  sentir.
* A interface continua sendo a bancada de teste.

## 12. Rollback

* v0.1: branch `versao-0.1-motor`. v0.2: branch `versao-0.2-som` (exatamente a `main` antes desta
  etapa). Os APKs antigos continuam instaláveis (desinstalar antes, por causa do versionCode).
* Dentro da própria v0.3, os 8 presets "(v0.2)" tocam sem texturas nem transições.
