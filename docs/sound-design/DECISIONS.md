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

## v0.5 (pads de fundo, a partir de 9 referências do usuário)
- D29 **Nova categoria SOFT PADS com 5 presets** (Soft Pad, Shimmer Pad, Deep Pad, Wide Pad, Guitar Pad) — 8 das 9 referências são pads de quinta, escuros, estáveis e largos, e nenhum preset da v0.4 chegava perto (diferença de timbre de 6 a 25 dB) — mexer nos presets v0.4 (perderia o que já foi aprovado).
- D30 **Voicing "quinta por baixo"** (`VoicingStyle`): 5ª abaixo da fundamental quando ela não desce além de Sol2, cores uma oitava acima; mesmas classes de altura — é a montagem medida nas referências 1, 3 e 9 — dobrar a camada Low (grave demais, embola).
- D31 **Abafar** (`ToneCut`): passa-baixa de 4 polos em duas seções escalonadas (fc e 2,8·fc) depois do reverb, de 18 kHz a ~420 Hz — as referências caem devagar a partir de ~500 Hz–1 kHz; um corte de parede deixava os médios intactos (testado) — fechar só o filtro das vozes (o reverb e o shimmer continuavam claros).
- D32 Microeventos desligados nos pads de fundo (só o Shimmer Pad tem um harmônico raro) — nenhuma referência tem eventos.
- D33 **Guitar Pad**: guitarra limpa por síntese própria (Karplus-Strong com passa-tudo fracionário, um zero de perda, compressor de sustentação 3:1, captador do braço), em swells com 2 ecos de tempo sorteado (0,38–0,62 s, não BPM) — nenhuma guitarra CC0 adequada e o swell esconde o ataque — samples de guitarra comerciais (licença).
- D34 Notas da guitarra: do acorde + 9ª (diatônica no maior e no menor da fundamental) + 6ª maior só com tonalidade estimada; nunca 4ª nem 7ª; limite de 6 vozes tonais (cada swell traz 2 ecos) e retorno 0,5–1,7 s depois da troca — a guitarra é o próprio pad nesse preset.
- D35 Abafar entrou nos controles principais (6 controles) — é o ajuste que mais muda entre as referências; o brilho fino continua em Avançado.

## v0.6 (pads aveludados, estabilidade e interface)
- D36 **Aveludado** (`Velvet`, novo parâmetro no fim): o filtro de cada nota abre junto com o volume (a nota nasce escura e "floresce"), o ladder perde ressonância e saturação, a saturação do barramento sai, os agudos do lado são cortados (acima de ~2 kHz a imagem fica mais no centro) e o envio do reverb fica mais escuro (7,5 → 3 kHz). Pedido do usuário depois de ouvir a v0.5: "muito abertos e agressivos" — mudar a curva de todos os presets antigos.
- D37 Pads de fundo refeitos sobre o Aveludado, com reverb integrado (sem pré-delay, reflexões quase zero, difusão 0,92, cauda escura), entrada de 4 s, troca de 3,5 s, menos detune L/R e largura menor. Os da v0.5 foram para "Versões anteriores".
- D38 **Encerramento**: o motor nativo pode ser destruído (antes era criado uma vez e nunca liberado); o serviço em primeiro plano só existe enquanto há som (antes ficava vivo para sempre com START_STICKY depois da primeira nota) e para sozinho; em segundo plano sem som a saída de áudio fecha em 10 s; "Sair", "Encerrar" na notificação, voltar sem som e remover dos recentes liberam tudo.
- D39 Girar a tela não recria a Activity (configChanges): a interface é remontada, o áudio não é tocado.
- D40 Verificação de ciclo de vida e de telas num emulador Android 14 no CI (script `tools/device/check_lifecycle.sh`), com capturas em 4 resoluções e retrato/paisagem.
