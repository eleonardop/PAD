# Relatório da v0.4 — Refinamento geral

Objetivos: criatividade dentro do som (microeventos e evolução), identidade real entre os presets, biblioteca menor e interface limpa. **Nada aqui declara a qualidade sonora aprovada** — os números abaixo só mostram que as regras técnicas foram cumpridas; a decisão é do ouvido, no S23 Ultra.

## 1. Auditoria (resumo)
* Os presets da v0.3 eram parecidos porque o corpo Warm dominava todos e 15 de 16 usavam o reverb Cloud. A distância de timbre entre pares ficava entre 0,8 e 1,5 dB, e o índice de movimento entre 6,7 e 7,4 para todos.
* Os microeventos já existiam (texturas do maestro), mas estavam baixos e usavam uma paleta única. Eles não conheciam a tonalidade, e notas do acorde anterior continuavam soando depois da troca.
* A interface era uma bancada com 32 parâmetros e 16 presets em uma lista só.

## 2. O que foi reaproveitado
SourceLibrary (carregamento sob demanda), Conductor (estados do acorde e distância harmônica), TexturePlayer (vozes leves, grupos, envelopes), TapeCharacter, os samples one-shot da v0.3 (piano, EP por FM, vibrafone, pratos, reverses) e as três reverbs. Nenhum sample novo e nenhuma dependência nova.

## 3. O que foi implementado
* **Microeventos com paleta por preset** (`MicroPalette`: sino, piano, pluck, harmônico, reverse, metal, arco de cordas, rajada de fita; frequência média; inclinação de registro).
* **Estimativa de tonalidade** para liberar 7ª/9ª só no agudo e só quando a cor pertence à escala.
* **Soltura na troca de acorde**: microeventos com nota fora do novo acorde terminam em 0,6 s.
* **Arco de cordas**: a camada de cordas cresce por alguns segundos e volta. **Rajada de fita**: variação curta do wow, igual em L e R, sem ruído.
* **Semente reproduzível** (`setSeed`). Cada sessão do app começa com uma semente diferente.
* **8 presets novos** em 4 categorias; os 16 anteriores continuam no motor, em "Avançado → Versões anteriores".
* **Interface nova** e categoria do preset exposta pelo JNI.
* Ferramenta de escuta `pad_scenarios_refino` + `make_refino_package.py` (no CI).
* Correção: a linha melódica do maestro podia repetir a mesma nota 3 vezes seguidas; agora repete no máximo 2.

## 4. Como os microeventos funcionam
Detalhes em `docs/sound-design/MUSICAL_DESIGN.md §7`.

**Quando acontecem.** Não há BPM nem grade. O maestro acumula "unidades de evento" proporcionais à taxa do preset × controle Microeventos e dispara ao atingir um alvo sorteado (log-normal, às vezes uma pausa longa). Os bloqueios são:
* menos de 1,2 s de acorde;
* menos de 2,5 s desde o último evento;
* 3 microeventos tonais já soando;
* cooldown por tipo, de 4 a 22 s;
* 20% de chance de não acontecer.

Em trocas rápidas a taxa cai para um quarto.

**O que tocam.** Só notas do acorde atual, perto da nota anterior. Cada família só aparece se a camada dela estiver presente no preset, e o registro já ocupado pesa menos.

**Desligar.** Com o controle Microeventos em zero, nada acontece.

## 5. Presets mantidos, fundidos e removidos

| v0.4 (tela principal) | Função | Veio de |
|---|---|---|
| Warm | FOUNDATION | Warm Worship + Warm (v0.2), mais estreito, Plate curto |
| Strings | FOUNDATION | novo — cordas na frente (antes eram só uma camada de fundo) |
| Air | ATMOSPHERE | Ethereal + Air (v0.2) |
| Ambient | ATMOSPHERE | Ambient + Cathedral (cauda longa, ataque lento) |
| Vintage | CHARACTER | Vintage (EP + órgão + fita) |
| Cinematic | CHARACTER | Cinematic + Deep Worship + Coral (graves e vozes) |
| Soft Piano | TEXTURES | Modern Worship (as notas de piano viraram o assunto principal) |
| Reverse Bloom | TEXTURES | o bloom da v0.3 transformado em preset |

* **Fora da tela principal, mas preservados:** Shimmer (v0.2), Deep (v0.2) e os demais v0.2 e v0.3 ficam em "Avançado → Versões anteriores", sem mudança.
* **Nada foi apagado.** Os branches `versao-0.1-motor`, `versao-0.2-som` e `versao-0.3-ambient` guardam cada versão.

## 6. Como a identidade foi diferenciada
A diferença está na estrutura de cada preset, não só no EQ: qual camada é o corpo, o ataque (1,2 a 7 s), o tipo e o tamanho do reverb (Plate em Warm e Vintage, Cloud com 14 s no Ambient), a largura (0,75 no Warm, aberta no Air), a fita, o registro e a paleta de microeventos.
* A menor distância de timbre entre dois presets subiu de 0,8 dB para cerca de 1,9 dB; a média é 4,6 dB.
* Centróide espectral no acorde sustentado: de 367 Hz (Warm) a 804 Hz (Strings).
* Loudness: todos perto de −20,7 LUFS no teste interno, por trim e sem aumentar o volume geral.

## 7. Interface
* Título e linha de status.
* Quatro seções (FOUNDATION, ATMOSPHERE, CHARACTER, TEXTURES), cada uma com 2 botões grandes; o preset ativo fica com contorno e texto dourados. A descrição do preset aparece logo abaixo.
* Acorde, grade de 12 tonalidades, SWELL e PARAR.
* CONTROLES: Volume, Reverb, Intensidade, Evolução, Microeventos.
* **Avançado ▸** (recolhido): versões anteriores dos presets, todos os parâmetros agrupados, desempenho real do áudio (carga, xRuns, vozes, eventos, memória), teste de desempenho e créditos.

## 8. Testes e desempenho
* **Testes:** 65 testes passam em x86-64 (local e CI) e em ARM64 (CI), incluindo a simulação de 10 minutos. ASan + UBSan sem erros. Os 8 testes novos da v0.4 são:

| Teste | Resultado |
|---|---|
| Harmonia | 1090 notas, 76 cores, 0 fora do acorde ou da regra |
| Cooldown e densidade | 193 eventos em 10 min, 0 violações |
| Sementes | mesma semente = eventos idênticos; sementes diferentes = eventos diferentes |
| Microeventos em zero | 0 eventos |
| Trocas a cada 1 s × 10 s | 0 × 16 eventos em 3 min |
| Troca de acorde durante evento | 2 → 1 (a nota fora do acorde saiu) |
| Biblioteca | 8 presets, 2 por categoria |
| Tempo real, mono e identidade | 0 alocações, pior perda em mono −2,4 dB, menor distância de timbre 1,9 dB |

* **Desempenho em ARM64 no CI (servidor, só referência):** os presets da v0.4 usam de 4,2% (Warm) a 10,8% (Cinematic) de média de um núcleo, com p99 ≤ 12,5%. Ficam no mesmo nível ou abaixo dos presets da v0.3, e o pior caso continua 11,5%.
* **Desempenho no celular:** **ainda não foi medido** — depende do teste no próprio celular (Avançado → Teste de desempenho). Na v0.2 o S23 Ultra mediu 7,6–8,5%.

## 9. WAVs
Artefato **PAD-v04-refino-escuta** do CI (60 arquivos, 48 kHz / 24 bits):
* `1_sustentado_*`: Sol maior por 18 s em cada preset novo.
* `2_antigo_sustentado_*`: o mesmo acorde nos 16 presets antigos.
* `3_progressao_worship_*` (G–D–Em–C…), `4_progressao_menor_*` (Am–F–C–G) e `5_progressao_pad5_sus_*`.
* `6_semente_*`: o mesmo acorde com 3 sementes em Soft Piano, Air, Cinematic e Reverse Bloom.

Pastas:
* `estereo/`: −20 LUFS, TP ≤ −1 dBTP.
* `mono/`: soma (L+R)/2, sem renormalizar.
* `originais/`: sem normalização.

`loudness.md` traz LUFS e true peak antes e depois da normalização, o ganho aplicado, o LUFS em mono e a perda em mono por arquivo. Os ganhos de normalização ficaram entre −1,0 e +1,7 dB, o maior true peak final foi −5,3 dBTP e a perda em mono ficou entre −1,0 e −2,5 dB.

## 10. APK
Artefato **PAD-Worship-APK** do CI, versão 0.4.0-refino (versionCode 4).

## 11. O que depende da escuta
* Se cada preset "existe por um motivo" ao alternar com o mesmo acorde (`1_sustentado_*`).
* Se os microeventos soam musicais ou distraem: frequência, volume, registro e se o arco de cordas e a rajada de fita são percebidos como vida ou como defeito.
* Se as cores de 7ª/9ª no agudo combinam com a música real.
* Se Air (+7 dB de trim) e Soft Piano ficam presentes sem chamar atenção demais.
* A carga real do áudio no S23 Ultra.
* A clareza da nova tela durante o culto.
