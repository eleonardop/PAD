# PAD Worship — Arquitetura do motor de áudio (Etapa 1)

Este documento registra as decisões técnicas do motor, para que qualquer desenvolvedor possa continuar o projeto.

## 1. Visão geral

```
 Interface Kotlin (qualquer thread)                    Thread de áudio (Oboe/AAudio, tempo real)
 ─────────────────────────────────                     ───────────────────────────────────────────
 PadEngine.kt ──JNI──► PadEngine (C++)                 OboePlayer::onAudioReady
   playChord / setKey / stop  ──► fila SPSC sem trava ──►  handleCommands()
   setParam                   ──► std::atomic<float>  ──►  updateControl() (suavização, 1x/32 amostras)
   stats()                    ◄── atômicos de telemetria ◄─ VoiceManager.render() → barramentos
                                                            → cadeia master → limitador → saída
```

* **`engine/`** — núcleo C++17 sem dependência de Android. O mesmo código é usado pelo app, pelos testes e pelo gerador de WAV.
* **`android/`** — app Kotlin + JNI + Oboe. Só a camada de saída de áudio e a ponte de controle são específicas de Android.
* **`tools/`** — gerador de WAV e script de espectrograma.

### Estrutura de pastas

| Caminho | Conteúdo |
|---|---|
| `engine/src/PadEngine.*` | API pública, cadeia master, threads |
| `engine/src/core/VoiceManager.*` | alocação de vozes, voice leading, voice stealing |
| `engine/src/core/Voice.*` | uma nota com as 5 camadas |
| `engine/src/core/SampleBank.*` | banco de samples da camada Strings/Texture, loops |
| `engine/src/core/RingBuffer.h` | fila SPSC sem trava |
| `engine/src/core/Params.h`, `Presets.*` | parâmetros (faixas seguras) e presets |
| `engine/src/core/MusicTheory.*` | tonalidades, acordes, voicings |
| `engine/src/core/WavIO.*` | leitura/escrita de WAV (fora do callback) |
| `engine/src/dsp/` | filtros SVF, wavetables, envelope, crossfade, reverb FDN, pitch shifter, limitador |
| `engine/tests/` | 26 testes automatizados + ferramentas de medição |
| `tools/wavgen/` | gerador de 21 WAVs de avaliação + relatório |
| `android/app/src/main/cpp/` | `OboePlayer`, `JniBridge` |
| `android/app/src/main/java/.../engine/` | API Kotlin (`PadEngine`), serviço em primeiro plano |
| `android/app/src/main/java/.../app/` | bancada de teste provisória (não é a interface final) |

## 2. Regras do thread de áudio

`PadEngine::process()` é o único código executado no callback. Ele **não** faz:
mutex, alocação de memória, leitura/escrita de arquivo, chamada JNI, log, ou qualquer operação de tempo imprevisível.

* Comandos (acorde, tom, parar) chegam por `SpscRingBuffer` (256 posições, sem trava). Se vários threads Java enviarem comandos, um `std::mutex` serializa **apenas o lado produtor**; o callback nunca toca nesse mutex.
* Parâmetros são `std::atomic<float>` lidos 1 vez por sub-bloco de 32 amostras e suavizados (sem "zipper noise").
* Todas as estruturas (48 vozes, linhas de atraso, barramentos) são alocadas em `prepare()`.
* O teste `motor_consumo_de_recursos` substitui o `operator new` global e confirma **zero alocações** durante `process()`.
* Denormais são desligados no callback (`ScopedNoDenormals`: FTZ/DAZ no x86, FZ no ARM64), evitando picos de CPU nas caudas do reverb.
* A troca de banco de samples em tempo de execução (`replaceSampleBank`) é feita por ponteiro atômico, só quando não há vozes; o banco antigo é liberado fora do callback (`collectGarbage`).

## 3. Camadas sonoras (síntese híbrida)

Cada voz (= uma nota do acorde) renderiza todas as camadas; o volume de cada camada é independente.

| Camada | Técnica | Por quê |
|---|---|---|
| **Warm / Body** | 3 osciladores wavetable (centro + par desafinado, um deles com timbre "oco"), passa-baixa SVF com key tracking | Corpo quente e encorpado, agudos controlados pelo Brightness |
| **Air** | Ruído estéreo independente filtrado em 2 passa-bandas estreitos (Q 7–9) centrados em **harmônicos inteiros da nota** | "Ar" afinado com o acorde, em vez de chiado genérico |
| **Strings / Texture** | **Samples** lidos com interpolação Hermite, duas cabeças de leitura (L/R) em pontos distantes do loop | Textura rica de ensemble que seria cara demais em tempo real |
| **Shimmer** | Harmônicos 2 e 3 quase senoidais, levemente desafinados + envio extra ao reverb shimmer | Brilho limpo, sem aspereza |
| **Low / Body** | Wavetable de grave redondo, **só na fundamental do acorde**, entre A1 e G#2 (55–104 Hz), mono | Fundamentação sem embolar; não briga com bumbo nem baixo |

### Wavetables sem aliasing
Tabelas de 4096 pontos geradas por síntese aditiva, com 10 níveis de mipmap (um por oitava). Em cada nível só entram harmônicos abaixo de 0,45·fs. Medido: componentes não harmônicas abaixo de −126 dB.

### Banco de samples (Strings/Texture)
Não havia gravações licenciadas disponíveis nesta etapa. O banco padrão é **gerado pelo próprio motor** em `prepare()` (fora do callback, em várias threads):
* ensemble de 4 "músicos" desafinados (−6,5 a +7 cents), até 40 parciais por nota (até ~160 osciladores por nota);
* deriva lenta de afinação (±2,2 cents), vibrato de ensemble (~5 Hz, ±3 cents), evolução independente da amplitude de cada parcial;
* resposta de "corpo" com ressonâncias largas, conteúdo limitado a 10 kHz;
* 15 zonas (notas MIDI 34 a 90, a cada 4 semitons), loops de 6 s, 17,3 MB.

**Loops sem emenda:** todas as frequências e moduladores do bake são múltiplos inteiros de 1/T (T = duração do loop). O sinal é exatamente periódico; a junção é matematicamente contínua (teste `samples_loop_sem_emenda`).

**Para gravações reais (futuro):** `SampleBank::addZoneFromAudio()` remove DC, normaliza e cria o loop com crossfade cuja lei depende da **correlação medida** entre o fim e o começo (ver §5). `readWav()` lê PCM 16/24/32 bits e float.

**Limite de transposição:** zonas a cada 4 semitons → deslocamento máximo de ±2 semitons (fator 0,89–1,12). Formantes mudam no máximo ~12% e, como o conteúdo vai até 10 kHz, a leitura acelerada não gera aliasing audível (só acima de ~21 kHz).

## 4. Acordes, tonalidades e voice leading

* 12 tonalidades × 10 tipos de acorde (`Pad5` = 1-5-8-12, Maior, Menor, Sus2, Sus4, Maj7, m7, 7, Add9, m(add9)).
* Voicing aberto: fundamental perto de Eb3 (entre A#2 e A3, evitando saltos de registro ao trocar de tom), quinta, e notas de cor uma oitava acima.
* **Troca de acorde/tom:**
  * notas comuns ao acorde anterior **continuam soando na mesma voz** (sem retrigger e sem duplicar o sinal);
  * notas que saem fazem fade-out e as que entram fazem fade-in, ambos **equal-power** (seno/cosseno);
  * uma nota que sai e volta logo depois é "revivida" a partir do ganho atual;
  * a camada Low tem rampa própria quando a fundamental muda de papel.
* Ganho por nota = 0,9/√(nº de notas): acordes de 4 ou 6 notas têm volume parecido.

## 5. Crossfade e correlação

Para dois sinais de mesma potência com correlação ρ, a potência da soma é `g1² + g2² + 2ρ·g1·g2`.
* ρ ≈ 0 (notas diferentes, ruídos independentes): equal-power mantém o volume.
* ρ = 1 (mesmo sinal): equal-power daria +3 dB no meio; o correto é linear.

`crossfadeGains(t, ρ)` parte da curva equal-power e normaliza pela correlação, mantendo a potência constante para qualquer ρ entre 0 e 1 (testado: desvio ≤ 0,09 dB).

Aplicação no motor:
* **Notas diferentes** são descorrelacionadas → equal-power puro.
* **Notas iguais** (correlação potencialmente 1 ou com cancelamento de fase): o motor **não** faz crossfade de uma nota com ela mesma; a voz é mantida. Isso elimina a causa de picos e quedas.
* **Loops de WAVs externos:** a correlação entre os trechos é medida e usada na lei.
* Trocas rápidas sucessivas: cada voz guarda a própria posição de fade, então as rampas continuam do ponto atual, sem saltos. O limitador final protege contra acúmulo de energia em casos extremos.

## 6. Vozes e polifonia

* 48 vozes pré-alocadas; limite lógico de 42 + 6 de reserva para roubo sem clique.
* **Voice stealing:** vítima recebe fade de 15 ms. Prioridade: vozes em Release/saída com menor ganho → mais antigas. A fundamental e as notas do acorde atual nunca são roubadas.
* Medição (teste `motor_voice_stealing`): com os voicings reais, mesmo com 120 trocas em 6 s e crossfade de 10 s, o máximo foi **20 vozes**. Isso acontece porque notas repetidas reaproveitam a voz e vozes que mal entraram saem rápido. O roubo só ocorre em cenários artificiais (ciclos tocar/parar com Release de 20 s em 6 oitavas), onde funcionou sem cliques e sem nenhum "roubo forçado".
* **Polifonia recomendada: 48.** Custa só memória (as vozes livres não consomem CPU), e a margem cobre Release longo com trocas rápidas. O número definitivo deve ser confirmado com o benchmark no aparelho (ver relatório).

## 7. Envelope e transições

* **Attack:** curva smoothstep a partir do nível atual (entrada sem clique).
* **Release:** (1−t)³ a partir do nível atual. Cai de forma natural e chega exatamente a zero no tempo pedido.
* Todos os ganhos são calculados por sub-bloco de 32 amostras e interpolados linearmente por amostra.

## 8. Cadeia master

```
vozes ─► barramento Pad (Warm+Strings+Air+Shimmer) ─► passa-alta 70 Hz ─┐
      ─► barramento Low ─► passa-alta 28 Hz ─► passa-baixa 260 Hz ─────┴► soma
   ─► Low shelf 140 Hz (−12..+5 dB)        [Low]
   ─► Bell 320 Hz + High shelf 6,5 kHz     [Warmth]
   ─► High shelf 3 kHz (−7..+5 dB)         [Brightness]
   ─► High shelf 11 kHz (−2..+4 dB)        [Air]
   ─► saturação suave (ímpar, proporcional ao Warmth) ─► bloqueio de DC 8 Hz
   ─► Reverb FDN (+ envio extra da camada Shimmer) ─► mix seco/molhado [Reverb]
   ─► largura M/S [Width] ─► volume [Master] × headroom −5,2 dB
   ─► limitador lookahead 3 ms, teto −1 dBFS ─► saída
```

### Reverb FDN
8 linhas (37–97 ms, mutuamente "primas"), matriz de Householder (sem perdas), difusão de entrada com 4 allpass por canal, pré-delay de 22 ms, **modulação lenta e independente de cada linha** (0,07–0,31 Hz, leitura Hermite) para evitar o som metálico, damping de 1 polo com ganho calculado para o RT60 exato. Medido: RT60 configurado de 3 s → queda de 20,0 dB/s (teórico: 20).

### Shimmer
Pitch shifter granular de duas cabeças (+12 semitons, janela de ~80 ms, janelas sin² que somam exatamente 1) na realimentação do reverb, filtrado entre 700 Hz e 5,2 kHz. Cada passagem sobe uma oitava; em no máximo ~3 passagens o conteúdo sai da banda do filtro. Por isso o sistema é estável mesmo com ganho de laço alto. No máximo, acrescenta cerca de +10 dB na faixa de 1–6 kHz sem acionar o limitador.

### Limitador
Mínimo deslizante do ganho necessário + média móvel do mesmo tamanho: o ganho já está reduzido quando o pico sai do atraso (sem overshoot, sem distorção de degrau). Release exponencial de 180 ms. Os presets não dependem dele (teste: ganho mínimo = 1,00 em todos). No pior caso (tudo no máximo) ele segura a saída em −1 dBFS.

## 9. Parâmetros

| Parâmetro | Faixa | Função |
|---|---|---|
| Layer Warm/Air/Strings/Shimmer/Low | 0–1 | volume de cada camada |
| Warmth | 0–1 | médios-graves, agudos arredondados, saturação suave |
| Brightness | 0–1 | abertura dos filtros das vozes + shelf de agudos |
| Air | 0–1 | faixa 10–16 kHz + altura do ruído afinado |
| Low | 0–1 | graves do conjunto (0,5 = neutro) |
| Width | 0–1,5 | 0 = mono, 1 = natural |
| Movement | 0–1 | deriva lenta de afinação (≤ ±2,2 cents), filtro, volume (≤ ±1,2 dB), pan |
| Detune | 0–1 | 1 a 14 cents entre osciladores |
| Reverb, Decay, Size, Damping | — | espaço |
| Shimmer | 0–1 | realimentação de oitava |
| Attack / Release / Crossfade Time | segundos | transições |
| Master Volume | 0–1 | volume final (não é alterado pelos presets) |

Valores fora da faixa são limitados; NaN/infinito são ignorados.

## 10. Presets de fábrica

Warm, Deep, Air, Shimmer, Cinematic e Ambient, cada um com equilíbrio próprio de camadas, espaço e transições. Os volumes dos 6 presets ficam dentro de 2,5 dB entre si (trocar de preset não causa susto de volume) e todos são compatíveis com mono (correlação L/R > −0,25), o que importa para mesas e PAs mono.

## 11. Android

* **Oboe 1.9.3** via prefab: AAudio quando disponível, `LowLatency`, `Exclusive` (cai para `Shared`), float estéreo, 48 kHz. Se a saída usar outra taxa, o Oboe converte (qualidade média) e o motor continua a 48 kHz.
* Buffer de 4 bursts (~10–20 ms): para pads, continuidade vale mais que latência mínima.
* Fone conectado/desconectado e troca de Bluetooth: `onErrorAfterClose` reabre o stream sem perder o estado do motor (o acorde continua).
* **Segundo plano:** `PadPlaybackService` (serviço em primeiro plano, tipo `mediaPlayback`) mantém o processo vivo com a tela desligada. A notificação tem o botão "Parar".
* O motor nativo é compilado com `-O3` mesmo no APK de debug.
