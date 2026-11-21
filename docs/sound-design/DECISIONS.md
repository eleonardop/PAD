# Decisões de arquitetura e de sound design (registro)

Formato: decisão — motivo — alternativa descartada.

## v0.2 (já tomadas)
- D01 PADsynth para Warm/Vox/Air/Glass — corpo largo sem batimentos — supersaw/chorus.
- D02 Morph com mesma semente de fases por família — interpolação sem cancelamento — tabelas independentes.
- D03 Cloud Seed como reverb padrão; Plate leve; FDN v0.1 mantido — medidas de densidade, agudos e CPU (ver `docs/RELATORIO_ETAPA2.md` §4).
- D04 Fontes sob demanda com thread carregador e liberação após 2 blocos sem uso — memória sem travar o áudio.
- D05 Graves mono abaixo de 140 Hz; largura 1,0 = 80% do lado — compatibilidade mono.
- D06 Trim de loudness por preset — comparação justa e volume estável no culto.
- D07 Nenhum código GPL/LGPL/AGPL (JUCE e Surge fora).

## v0.3
- D08 **Criatividade = eventos musicais esparsos + camadas que entram com o tempo**, não mais efeito — é o que os tecladistas fazem com snapshots e partes de piano esparsas (RESEARCH §2) — mais modulação aleatória.
- D09 **Maestro (Conductor) com estados por acorde** e distância harmônica (notas comuns + círculo de quintas + modo, aproximação do Tonnetz, RESEARCH §7) — reação musical sem lookahead — sequenciador/arpejador (ritmo chama atenção).
- D10 Notas geradas só do acorde atual; 9ª apenas em registro alto e em acordes que a admitem; Pad5 nunca recebe terça — nenhuma dissonância contra a banda — escalas inteiras (risco de nota de passagem).
- D11 `TexturePlayer` separado das vozes do pad (24 vozes leves) — não roubar notas sustentadas; CPU previsível — aumentar o pool para 64–96 vozes.
- D12 EP por FM pré-renderizado — sem dependência de gravação de hardware comercial; custo zero no áudio — samples TX81Z do VCSL, FM em tempo real.
- D13 Pratos reais de baqueta macia (VCSL, CC0), sempre filtrados (HP 300 Hz, LP ≤ 6 kHz) e majoritariamente no reverb — ar e expansão sem chiado — ruído filtrado sintético.
- D14 Reverse gerado no carregamento a partir das amostras (piano e prato) — sem I/O nem processamento pesado no callback — arquivos reversos prontos no APK (dobraria o tamanho).
- D15 Bloom pós-ataque automático com controle de intensidade (Bloom) e frequência (BloomChance), mais pre-swell manual por botão — os dois independentes; nenhum dispara em toda troca.
- D16 Tape como caráter (wow/flutter/saturação/agudos), sem chiado — Vintage sem ruído.
- D17 Granular, convolução, phase vocoder em tempo real e physical modeling fora desta etapa (DSP_REFERENCE).
- D18 Presets v0.2 mantidos no app com o sufixo "(v0.2)", sem texturas, para comparação e rollback sonoro; 8 presets novos com papéis musicais diferentes.
- D19 Estéreo das texturas por pan complementar entre eventos e deriva lenta em velocidades diferentes; largura extra só durante o bloom (+12% de lado) — sem Haas, sem inversão de fase.

## v0.4 (refinamento)
- D20 **Microeventos por paleta do preset**, não uma paleta global — cada preset diz quais famílias usa (sino, piano, pluck, harmônico, reverse, metal, arco de cordas, rajada de fita), com que frequência média e em que registro — é o que faz dois presets com o mesmo acorde soarem por motivos diferentes — mais tipos de evento iguais para todos.
- D21 **Relógio em "unidades de evento"** (taxa × atividade acumulada até um alvo log-normal, 12% de chance de pausa longa) — sem BPM, sem intervalos regulares, e a suavização dos controles não gera intervalos absurdos — sortear um intervalo em segundos no momento da troca.
- D22 **Tonalidade estimada** (pesos de classes de altura com decaimento × 12 escalas maiores, com limiar de confiança) só para liberar 7ª/9ª em registro alto quando a cor pertence à escala; sem confiança, só notas do acorde — cor sem risco de nota estranha à música — análise da banda por microfone (latência e privacidade).
- D23 Cooldown por tipo (5–22 s), no máximo 3 microeventos tonais soando, 2,5 s mínimo entre eventos, nada antes de 1,2 s do acorde, quase nada em trocas rápidas (taxa × 0,25 quando os acordes duram < 2,5 s) e 20% de chance de pular — esparsidade garantida por construção, não por sorte.
- D24 Na troca de acorde, microeventos tonais cujas notas não pertencem ao novo acorde são soltos em 0,6 s; os que pertencem continuam — sem nota velha contra acorde novo, sem corte seco.
- D25 **Biblioteca de 8 presets em 4 funções** (FOUNDATION, ATMOSPHERE, CHARACTER, TEXTURES), diferenciados na estrutura (camada de corpo, ataque, tipo de reverb, largura, fita, paleta) e não só no EQ — os 16 presets anteriores continuam no motor, fora da tela principal (Avançado → Versões anteriores) — apagar presets antigos (perderia a comparação) ou mantê-los na lista principal (confuso no culto).
- D26 Trims recalculados para os 8 novos (≈ −20,7 LUFS no teste interno, ±0,05) — nenhum volume geral aumentado.
- D27 Estalos orgânicos **não** implementados — o pedido os tornava opcionais, e ruído de vinil/fita foi descartado em D16; a "rajada de fita" (variação lenta e curta do wow, idêntica em L e R) dá a imperfeição sem ruído.
- D28 Interface: 4 categorias × 2 botões grandes, 5 controles principais (Volume, Reverb, Intensidade, Evolução, Microeventos) e o resto em "Avançado" recolhido.
