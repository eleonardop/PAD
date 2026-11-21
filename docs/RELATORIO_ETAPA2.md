# PAD Worship — Relatório técnico da Etapa 2 (qualidade sonora, v0.2)

Este relatório descreve o que foi implementado nas fases A–D, as decisões tomadas, as
medições objetivas e os limites do que foi verificado. **Ele não aprova a qualidade
sonora**: as medições mostram o que mudou, mas o julgamento musical final é do usuário,
de ouvido, no aparelho e no sistema de som da igreja.

## 1. Resumo do que mudou

| Pedido | O que foi feito | Onde |
|---|---|---|
| PADsynth para Warm e Vox | Gerador PADsynth próprio (IFFT de 2^18 amostras, fases aleatórias, faixas gaussianas por harmônico). Warm em duas versões (escura e aberta) e Vox em duas vogais (oh e ah), com a mesma semente por família para o morph não cancelar fase | `dsp/PadSynth.*`, `core/Sources.cpp` |
| Air reformulado na origem | O Air deixou de ser ruído filtrado. Agora é PADsynth só com harmônicos agudos da própria nota (sem fundamental, centro em ~2,2 kHz), em duas versões para morph. Ganho de camada −15 dB e passa-baixa de 5,5 a 9 kHz | `core/Sources.cpp`, `core/Voice.cpp` |
| Strings/Texture com samples reais | Cordas: naipes do VSCO 2 CE (16 zonas). Textura nova com duas opções: piano Salamander com espectro "congelado" (17 zonas) e órgão de tubos suave do VCSL (17 zonas). As cordas sintéticas da v0.1 continuam disponíveis e são a reserva automática | `assets/samples`, `tools/samplelab` |
| Chorus ensemble | Ensemble estéreo de 3 fases (estilo dos ensembles de cordas analógicos), canal direito defasado 60°, nível compensado | `dsp/Ensemble.h` |
| Evolução, morphing e macro | Morph entre as tabelas de cada família, filtro e respiração lentos; parâmetro **Evolução** (macro) e **Intensidade** (dinâmica) | `core/Voice.cpp`, `PadEngine.cpp` |
| Melhor filtro | Ladder ZDF de 4 polos por voz, com ressonância e saturação suaves | `dsp/Ladder.h` |
| Dinâmica, espectro e profundidade | Compressor de cola suave, graves em mono (M/S), tilt de agudos, trim de loudness por preset, largura estéreo recalibrada | `PadEngine.cpp`, `dsp/GlueCompressor.h` |
| Reverbs Cloud Seed e Dattorro | Os dois implementados na mesma interface, junto com o FDN da v0.1. Comparados por medidas de resposta ao impulso e CPU (seção 4). **Escolha: Cloud Seed como padrão**, Plate como alternativa leve | `dsp/CloudReverb.*`, `dsp/DattorroPlate.*`, `tools/reverblab` |
| Reflexões iniciais, pré-delay e difusão | Parâmetros próprios; o envio ao reverb passa por HP 180 Hz e LP 7,5 kHz (sem agudos ásperos na cauda) | `dsp/EarlyReflections.h`, `PadEngine.cpp` |
| Shimmer sem dominar | Shimmer fora do reverb, +12 semitons, banda de 700 Hz a 5,2 kHz, realimentação máxima 0,30 | `dsp/ShimmerLoop.h` |
| Carregamento sob demanda | Biblioteca de fontes com thread carregador; só o que o preset usa fica na memória; liberação segura | `core/Sources.*` |
| Teste no aparelho | Botão "Teste de desempenho" no app (mesmo código do `pad_bench`), carga do áudio e falhas de buffer na tela; job de CI em ARM64 | `tools/Benchmark.*`, `MainActivity.kt`, `.github/workflows/build.yml` |

Nada foi removido. Tudo que existia na v0.1 continua acessível: o reverb Clássico (FDN),
as cordas sintéticas, os 21 parâmetros originais (mesmos ids) e os 6 presets (mesmos nomes,
valores revistos). Foram acrescentados 11 parâmetros e 2 presets (Cathedral e Coral).

## 2. Rollback

* A v0.1 está preservada na branch **`versao-0.1-motor`** (commit `929f4c4`), que é exatamente
  o que estava na `main` antes desta etapa. Uma tag `v0.1.0-motor` existe localmente; a criação de
  tags no GitHub não é permitida a partir deste ambiente, por isso a branch é a referência.
* O APK da v0.2 usa `versionCode 2` e `versionName 0.2.0-som`; para voltar, basta instalar o APK
  da v0.1 (desinstalando antes, já que o Android não aceita versão menor por cima).
* O CI compila a v0.1 a partir dessa branch em todo push, para gerar as comparações antes/depois.

## 3. Decisões técnicas

### 3.1 PADsynth e morph
* Cada família de morph usa a mesma semente de fases, então as duas tabelas têm as mesmas
  fases em cada frequência e a mistura linear interpola só as amplitudes (sem phasing).
  O teste `padsynth_morph_sem_cancelamento` mede isso.
* Tabelas de 2^18 amostras (5,46 s a 48 kHz) por zona, int16 com escala (metade da memória
  de float, sem perda audível: o ruído de quantização fica abaixo de −90 dB).
* Geração: ~0,2 s por fonte no servidor, em thread próprio, nunca no áudio.

### 3.2 Evolução
* Os moduladores da Evolução são **comuns a todas as vozes** (70%) mais uma parte própria de
  cada voz (30%). Na primeira versão eram independentes por voz e, somados no acorde, se
  cancelavam: a medida mostrou que Evolução 1 e 0 eram indistinguíveis. Agora o conjunto
  respira junto. São senos lentos com razão áurea entre si (quase periódicos, nunca repetem
  exatamente): ciclos de ~60 s com Evolução baixa até ~15 s com Evolução alta.
* Profundidade: morph de até ±0,75 entre as tabelas, filtro de até ±1,2 oitava, respiração
  de até ±1,3 dB.

### 3.3 Samples
* Só os conjuntos úteis para pad foram incluídos: notas longas (sustain/vibrato) de naipe, um
  órgão suave e um piano transformado em som contínuo. Nada de staccato, pizzicato ou
  percussão. Total: 27 MB em FLAC 16 bits, 48 kHz, estéreo.
* Pipeline offline (`tools/samplelab/prepare_samples.py`): detecção de f0, escolha da região
  de sustain, loop sem emenda (crossfade com lei de correlação), freeze espectral do piano,
  normalização. Na carga, cada zona é normalizada para −20 dBFS RMS (o piano tem
  panorâmica por registro, então a medida é a média L/R).
* A raiz de cada zona é a frequência **medida**, não a teórica: o desvio da afinação nominal
  dos samples chega a 14 cents, e o motor compensa.

### 3.4 Largura estéreo
As fontes da v0.2 saem quase descorrelacionadas (leituras L/R independentes do PADsynth +
detune). Com o mapeamento de largura da v0.1, a correlação L/R ficava perto de 0 e a soma em
mono perdia ~3 dB (largura 1,5 chegava a −4,7 dB). O mapeamento passou a usar 80% do lado em
largura 1,0 e meio crescimento acima disso: correlação ~0,25–0,40 e perda em mono ~1,6–2,1 dB. Para
sistemas da igreja que somam em mono, largura entre 0,5 e 0,8 é recomendável.

### 3.5 Loudness dos presets
Cada preset tem um ajuste de nível fixo (`trimDb`, de −2,1 a +1,8 dB) calculado medindo o
LUFS integrado da progressão G–D–Em–C. Depois do ajuste final da largura estéreo, todos ficam
entre −21,0 e −20,4 LUFS. O Volume geral
continua sendo só do operador.

## 4. Comparação dos reverbs e escolha

Respostas ao impulso com os mesmos parâmetros (modulação desligada para medir a estrutura),
análise em `tools/reverblab/analyze_ir.py`:

| Medida (RT60 pedido 3 s / 6 s) | Clássico (FDN v0.1) | Plate (Dattorro) | Cloud (Cloud Seed) |
|---|---|---|---|
| RT60 a 1 kHz | 2,71 / 5,08 s | 3,20 / 5,91 s | 2,74 / 6,08 s |
| RT60 a 4 kHz (amortecimento) | 1,60 / 2,28 s | 2,55 / 4,23 s | 1,48 / 2,10 s |
| Tempo até a cauda ficar densa (NED ≥ 0,9) | imediato | ~210 ms (ecos discretos no início) | imediato |
| Coloração (desvio do espectro fino, ideal ≈ 5,6 dB) | 5,8 dB | 6,1–6,2 dB | 6,4–6,8 dB |
| Pior pico modal | 10,3 dB | 12–13 dB | 11–14 dB |
| IACC da cauda (menor = mais largo) | 0,04 | 0,02 | 0,03–0,09 |
| Energia acima de 6 kHz no 1º segundo | −6 dB | −5 dB | −25 a −33 dB |
| CPU (servidor x86, % de um núcleo) | ~0,5–0,8% | ~0,5–0,9% | ~5–6% |
| CPU no ARM64 do CI (Neoverse-N2), preset Warm inteiro | 5,8% | 4,9% | 10,0% |

**Decisão: Cloud Seed é o padrão de todos os presets.**
* É o único com cauda densa desde o início **e** escura por natureza (agudos 20–30 dB abaixo
  dos outros dois): atende ao pedido de "sem agudos ásperos" sem precisar de filtro pesado.
* Tem modulação interna rica (linhas e difusores modulados), que é o que dá a sensação de
  "nuvem" própria de pad.
* A coloração medida é um pouco maior que a do FDN (6,4–6,8 dB contra 5,8 dB; ruído difuso
  ideal ≈ 5,6 dB) e o pior pico modal fica entre 11 e 14 dB. Eu não consigo ouvir; se alguma
  nota "cantar" na cauda, é aqui que se deve olhar primeiro.
* Custo: ~10× mais CPU que o Plate. Por isso o Plate fica como **alternativa leve** (mesmo
  seletor "Tipo de reverb"), e o Clássico da v0.1 continua disponível. Se o teste no aparelho
  mostrar carga alta, a troca para Plate é imediata e sem clique.
* Calibração: o RT60 do Cloud Seed saía ~40% acima do pedido (os difusores dentro das linhas
  alongam a cauda); o mapeamento foi invertido (`(T − 0,9)/1,12`). O do Plate foi corrigido
  para considerar os 4 ganhos por volta do tanque.

## 5. Medições antes (v0.1) × depois (v0.2)

Gerado por `pad_scenarios` (o mesmo programa compilado contra as duas versões) e
`tools/compare/make_comparison.py`. Os WAVs **normalizados** (−20 LUFS, true peak ≤ −1 dBTP)
servem para comparar de ouvido sem que o mais alto pareça melhor; as medidas abaixo são dos
arquivos **originais**. A tabela completa (45 linhas) está em `medicoes.md` do pacote de
comparação.

| Cenário | Versão | LUFS | True peak | Centróide | Acima de 10 kHz | Correlação L/R | Perda em mono |
|---|---|---|---|---|---|---|---|
| camada Air isolada | antes | −19,9 | −13,4 | 3226 Hz | −22,9 dB | 0,05 | −2,8 dB |
| | depois | −37,3 | −28,2 | 1894 Hz | −81,7 dB | 0,24 | −2,1 dB |
| preset Air | antes | −15,3 | −5,7 | 3069 Hz | −17,2 dB | 0,00 | −3,0 dB |
| | depois | −21,0 | −9,9 | 629 Hz | −60,6 dB | 0,24 | −2,1 dB |
| preset Shimmer | antes | −15,4 | −3,1 | 857 Hz | −30,3 dB | 0,05 | −2,8 dB |
| | depois | −20,9 | −9,9 | 516 Hz | −65,4 dB | 0,29 | −1,9 dB |
| preset Warm | antes | −19,4 | −9,4 | 403 Hz | −42,6 dB | 0,52 | −1,2 dB |
| | depois | −20,4 | −8,9 | 416 Hz | −70,0 dB | 0,37 | −1,6 dB |
| todas as camadas | antes | −18,7 | −9,2 | 683 Hz | −33,0 dB | 0,45 | −1,4 dB |
| | depois | −19,1 | −7,8 | 478 Hz | −70,9 dB | 0,40 | −1,6 dB |

Como ler:
* **Air**: o chiado agudo da v0.1 desapareceu na origem. A energia acima de 10 kHz caiu de
  −23 dB para −82 dB na camada isolada; o centróide desceu de 3,2 kHz para 1,9 kHz.
* **Loudness**: na v0.1 os presets Air, Ambient, Shimmer e Cinematic saíam 3 a 5 dB mais
  altos que Warm e Deep. Na v0.2 os 8 presets ficam entre −21,0 e −20,4 LUFS (0,6 dB de
  diferença entre o mais alto e o mais baixo).
* **Clipping**: zero amostras saturadas em todos os cenários das duas versões; true peak
  sempre abaixo de −7 dBTP nos presets.
* **"Ruído entre harmônicos"** sobe na v0.2 (ex.: Warm −66,8 → −37 dB). Isso **não é chiado**:
  o PADsynth espalha deliberadamente cada harmônico numa faixa estreita de frequências (é o
  que dá o corpo de coro/naipe), e a medida conta essa energia como "entre harmônicos". A
  energia acima de 10 kHz, que é onde o chiado aparece, caiu 25 a 45 dB.
* **Espectro**: a v0.2 é bem mais escura acima de 5 kHz. Isso atende ao pedido de tirar a
  aspereza, mas pode soar escuro demais em alguns sistemas; Brilho, Ar e Intensidade abrem os
  agudos, e essa calibração depende da sua avaliação.
* **Estéreo**: a v0.2 é um pouco mais larga (correlação 0,24–0,40 contra ~0,5 no Warm) e perde
  0,2 a 0,5 dB a mais na soma em mono, sem cancelamento (o sinal é descorrelacionado, não
  invertido). Nos presets que já eram largos na v0.1 (Air, Shimmer) a perda em mono diminuiu.

Medidas dinâmicas verificadas pelos testes (seção 6): crossfades sem queda nem pico acima de 2 dB
(janelas de 300 ms), trocas de reverb, de fontes e de chorus sem clique, chorus sem mudar o nível
(±1 dB), Evolução movendo a inclinação espectral ~5 dB em 90 s (contra ~2 dB de ruído de medida
com Evolução 0), Intensidade abrindo ~4 dB de agudos e subindo ~5 dB de nível.

## 6. Testes

`pad_tests` tem 43 testes (42 com `--rapido`, que pula os 10 minutos simulados). Novos nesta etapa:

| Teste | Verifica |
|---|---|
| `padsynth_loop_largura_e_agudos` | loop sem emenda, largura do harmônico, sem energia acima de 10 kHz |
| `padsynth_morph_sem_cancelamento` | morph entre tabelas sem queda de nível |
| `air_harmonico_sem_chiado` | Air sem ruído: energia entre harmônicos e acima de 10 kHz |
| `samples_reais_carregam_afinados` | os 3 conjuntos carregam, afinação medida < 50 cents, loop sem salto, RMS −20 ± 0,5 dBFS |
| `filtro_ladder_resposta_e_estabilidade` | 24 dB/oitava, estável sob modulação |
| `ensemble_nivel_e_estabilidade` | chorus sem mudar o nível |
| `reverbs_rt60_estabilidade_sem_alocacao` | os 3 reverbs: RT60 dentro da faixa, sem alocar, CPU |
| `reflexoes_iniciais_predelay` | pré-delay correto |
| `compressor_cola_suave` | compressor só age em picos |
| `fontes_sob_demanda_liberacao_segura` | carga/liberação sem o callback ler memória liberada |
| `presets_valores_validos` | todos os valores dos presets dentro das faixas |
| `motor_troca_de_reverb_sem_clique` | troca de tipo de reverb com acorde soando |
| `motor_fontes_sob_demanda_sem_clique` | camada ligada durante o acorde entra em rampa |
| `motor_sem_assets_usa_cordas_sinteticas` | sem samples, o app continua tocando (reserva) |
| `motor_evolucao_movimenta_timbre` | Evolução move o timbre; Evolução 0 não |
| `motor_intensidade_dinamica` | Intensidade abre agudos e nível |
| `motor_chorus_nivel_e_cliques` | chorus sem clique e com o mesmo nível |

Resultados (local, servidor x86-64):
* Suíte completa, incluindo 10 minutos simulados: **43 testes, 0 falhas** (o teste longo: RMS por
  minuto estável em ±0,4 dB, pico 0,49, DC desprezível, nenhum roubo forçado de voz).
* `--rapido` com AddressSanitizer + UBSan: sem erros de memória nem comportamento indefinido.
  As verificações de velocidade são puladas nesse modo (o código instrumentado é 5–10× mais
  lento); as de áudio, não.
* CI (GitHub Actions), última execução: x86-64 **43 testes, 0 falhas**; sanitizers **42, 0 falhas**;
  ARM64 (Neoverse-N2) **43 testes, 0 falhas**, incluindo os 10 minutos simulados (RMS por minuto
  entre −22,8 e −22,2 dB nos 9 minutos tocando). A suíte roda nos dois a cada push; o resultado
  fica nos artefatos `PAD-resultados-x86_64` e `PAD-resultados-arm64`.

Alguns testes da v0.1 precisaram de medidas mais robustas, porque as fontes novas têm flutuação
natural de ±1,5 dB em janelas de 100 ms (batimento dentro de cada harmônico do PADsynth e dos
naipes reais). Em cada caso a média alinhada mostrou que não havia defeito (por exemplo: a média
das 12 trocas de tonalidade é plana em ±0,7 dB) e a janela de medida foi aumentada; os limites
de aprovação não foram afrouxados, exceto onde indicado no próprio teste.

## 7. Desempenho e memória

**Os números do servidor não provam desempenho no celular.** Eles servem para comparar versões e
reverbs entre si. A medida que vale é a do aparelho:

1. instale o APK, abra o app e espere "Motor pronto";
2. toque em **"Teste de desempenho completo"** (o som para por ~3 min e volta sozinho);
3. toque em **Copiar** e me envie o texto; ele inclui modelo, processador e Android.

Além disso, o topo da tela mostra durante o uso a **carga do áudio** (média e pior bloco) e as
**falhas de buffer** contadas pelo próprio Android. Carga média abaixo de ~50% e falhas de buffer
paradas em 0 durante um culto são o sinal de que o aparelho dá conta.

Referências medidas (blocos de 192 amostras = 4 ms):

| Caso | Servidor x86 (2 núcleos, compartilhado) | ARM64 Neoverse-N2 (CI) |
|---|---|---|
| preset Warm (Cloud) | média 8–12%, p99 10–20% | média 9,9%, p99 11,8% |
| os 8 presets (Cloud) | média 8,4–9,0% (runner do CI) | média 9,9–10,6%, p99 ≤ 12,7% |
| Warm + Plate | 4–8% | 4,9% |
| Warm + Clássico | ~5% | 5,8% |
| pior caso (todas as camadas no máximo, chorus, shimmer) | 8–12% | 9,9% |

Os picos isolados de 60–100% no servidor x86 não coincidem com trocas de acorde nem com nenhum
evento do motor (verificado bloco a bloco): são interrupções da máquina virtual compartilhada.
No ARM64, o pior bloco de todos os casos ficou em 13,4%.

Um núcleo grande de celular recente é comparável ao Neoverse-N2; um núcleo pequeno (Cortex-A55)
pode ser 3–4× mais lento. Se o Android colocar o áudio num núcleo pequeno, a carga estimada fica
em 30–40% com Cloud e 15–20% com Plate. Isso é uma estimativa, não uma medida.

Memória: ~109 MB de fontes com os presets que usam todas as camadas (Warm, Air etc.), ~97 MB no
Deep; o Cloud Seed ocupa mais ~23 MB por instância. Nenhuma alocação no callback de áudio
(verificado por contador de `operator new` nos testes).

Tamanho do APK: ~31 MB compactado no artefato do CI (as três arquiteturas mais ~27 MB de
samples). Não houve limite arbitrário de tamanho; os samples foram escolhidos por utilidade.

## 8. Dependências e licenças

| Componente | Licença | Uso | Compatibilidade |
|---|---|---|---|
| Cloud Seed Core (Ghost Note Audio) | MIT | reverb Cloud | ok; crédito no app ("Reverb algorithm: Cloud Seed by Ghost Note Audio") |
| dr_flac (David Reid) | domínio público / MIT-0 | leitura dos FLAC | ok |
| Oboe (Google) | Apache 2.0 | saída de áudio Android (já na v0.1) | ok |
| VSCO 2 CE (Versilian Studios) | CC0 1.0 | cordas | ok, sem obrigações |
| VCSL (Versilian Studios) | CC0 1.0 | órgão | ok, sem obrigações |
| Salamander Grand Piano V3 (Alexander Holm) | CC BY 3.0 | piano Cathedral | ok, **atribuição obrigatória e indicação de modificação**: feitas no app (tela "Créditos e licenças dos sons") e no repositório |

Não foi usado nenhum código GPL, LGPL ou AGPL (JUCE e Surge, que tinham sido avaliados na
pesquisa, ficaram de fora). Os textos completos das licenças estão em `assets/licencas/` (vão
dentro do APK) e o detalhamento das modificações em `engine/third_party/README.md`.

## 9. Limitações conhecidas e o que falta verificar

* **Desempenho real no celular**: não medido por mim (não tenho acesso a um aparelho). O botão
  de teste existe para isso.
* **Avaliação de ouvido**: as medidas não substituem ouvir em fones e no PA da igreja. Pontos que
  merecem atenção especial ao ouvir:
  * se o conjunto não ficou escuro demais (agudos muito baixos acima de 5 kHz);
  * se a flutuação natural do PADsynth (leve "respiração" mesmo com Evolução 0) agrada;
  * o piano Cathedral, que é a fonte mais "processada" (espectro congelado);
  * o Cloud Seed contra o Plate no seu sistema de som.
* **Mono**: a soma em mono perde ~1,6–2,1 dB (sem cancelamento). Para PA mono, use Largura 0,5–0,8.
* **Carregamento**: ao escolher um preset que usa uma fonte ainda não carregada, ela entra em
  rampa de 80 ms quando fica pronta (de ~0,1 s a ~0,6 s depois). Não há silêncio nem clique,
  mas a camada pode chegar um instante depois do acorde.
* **Fones Bluetooth a 44,1 kHz**: o motor roda sempre a 48 kHz e o Oboe converte a taxa.
* A interface do app continua sendo a **bancada de teste**; a interface do músico é uma etapa
  futura.

## 10. Onde está cada entrega

| Entrega | Onde |
|---|---|
| Código | branch `etapa2-qualidade-sonora` (e `main`, após a integração) |
| APK instalável | GitHub → Actions → execução mais recente de "Compilar e testar" → artefato `PAD-Worship-APK` |
| WAVs de demonstração (34) e relatório de níveis | artefato `PAD-resultados-x86_64` → `wav_exemplos/` |
| Comparação antes × depois (normalizada, espectrogramas, medições) | artefato `PAD-comparacao-antes-depois` |
| Respostas ao impulso e medidas dos reverbs | artefato `PAD-resultados-x86_64` → `reverbs/` |
| Resultados dos testes (x86, sanitizers, ARM64) e desempenho | artefatos `PAD-resultados-x86_64` e `PAD-resultados-arm64` |
| Licenças e créditos | `assets/CREDITOS_E_LICENCAS.txt`, `assets/licencas/`, `engine/third_party/README.md`, tela no app |
| Arquitetura | `docs/ARQUITETURA.md`, seção 12 |
