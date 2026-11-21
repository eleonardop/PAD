# Comportamento musical — regras do "maestro" (Conductor) da v0.3

O maestro roda no thread de áudio, na taxa de controle (a cada 32 amostras), sem alocar memória.
Ele conhece o acorde atual e o anterior, quando cada um começou, a Evolução e os controles de
atividade do preset. Não há linha do tempo futura: **nada antecipa o próximo acorde** e não há
latência de lookahead.

## 1. Vida de um acorde (estados)

| Estado | Tempo típico (Evolução 0,5) | O que acontece |
|---|---|---|
| Attack | 0 – attack do preset | só o pad entra (attack ou crossfade) |
| Establishment | até ~4 s | o florescimento ("bloom") pode aparecer; no máximo 1 nota de textura |
| Expansion | ~4 – 14 s | a segunda camada (cordas/coral do preset) cresce alguns dB; notas de textura esparsas; o envio do reverb e a largura sobem um pouco |
| Transformation | ~14 – 40 s | o timbre do pad se desloca (morph e filtro); camada de brilho ("sparkle") pode entrar; o reverb fica maior |
| Sustain | > ~40 s | tudo continua vivo, com densidade de eventos um pouco menor (para não cansar) |
| Dissolve | ao parar | texturas soltam com release longo; nada novo é disparado |

A Evolução estica ou encurta esses tempos (escala de 1,6× a 0,7×) e escala a profundidade de cada
mudança. Com Evolução 0 o organismo fica parado (sem segunda camada, sem deslocamento de timbre,
espaço ou largura); as notas de textura continuam dependendo só da Atividade, que pode ser zerada.

## 2. Distância harmônica (0 = mesmo acorde, 1 = muito distante)

`d = 0,5·(1 − notas comuns / notas do menor acorde) + 0,35·(distância no círculo de quintas / 6) + 0,15·(mudança maior↔menor)`

No círculo de quintas o acorde menor usa a posição da relativa maior (Em ≈ G), como no Tonnetz.
Valores medidos (teste `maestro_distancia_harmonica`): G → G = 0; G → Em = 0,32; G → C = 0,39;
G → D = 0,39; G → Bb = 0,51; G → C# = 0,85.

Quando o acorde muda:
* a "idade" do organismo não volta a zero: `idade ← idade · (1 − d)`. Mudança pequena mantém o
  ambiente; mudança grande recomeça o ciclo;
* a força do bloom é `Bloom · (0,35 + 0,65·d) · fator de duração do acorde anterior` (o fator vai
  de 0 em 1 s a 1 em 4 s: em trocas rápidas o maestro fica quieto);
* o bloom só acontece se passar no sorteio de frequência (`BloomChance · (0,4 + 0,6·d)`) e se o
  último bloom foi há mais de 4 s. No primeiro acorde depois do silêncio, d vale 0,6.

## 3. Escolha de notas (texturas tonais)

Regras para **qualquer** nota que o maestro gera:
1. **Só notas do acorde atual.** Cor permitida: a 9ª, apenas em acordes que a admitem (Maior, Menor,
   Sus2, Add9, mAdd9, Maj7, Pad5), apenas com Evolução > 0,4 e só no registro alto (sparkle).
2. **Pad5 (1-5-8)** é ambíguo entre maior e menor: as texturas usam só fundamental, quinta e 9ª,
   nunca a terça — assim nada contradiz o que a banda está tocando.
3. **Nunca** 2ª menor, 9ª menor, trítono que não pertença ao acorde, nem 4ª sobre terça maior.
4. **Registro**: teclas (piano/EP) entre Mi4 e Ré6, acima da região central da voz; brilho (vibrafone)
   entre Dó5 e Mi6. Nada abaixo de Mi4: a região grave pertence ao pad e ao baixo.
5. **Linha melódica**: cada nota nova é escolhida entre as candidatas mais próximas da anterior
   (movimento de até uma 5ª, preferindo grau conjunto), sem repetir a mesma nota mais de duas vezes
   seguidas. Isso cria uma linha que "respira", sem motivo rítmico.
6. **Tempo**: sem grade rítmica. Intervalos aleatórios de distribuição log-normal (média 4–9 s
   conforme a atividade), com espaçamento mínimo de 1,8 s. Nunca no mesmo instante da troca de acorde
   (a primeira nota vem 0,6–2,5 s depois).
7. **Dinâmica**: sempre suave (sem acentos), com variação de ±3 dB.

## 4. Transições

**A. Bloom pós-ataque (automático, controlado):** depois que o novo acorde começa, sobe por ~0,7 s e
desce em ~3 s: (a) um pouco mais de envio ao reverb e de largura; (b) um swell harmônico — as notas do
novo acorde uma oitava acima, no timbre Glass; (c) se a distância for grande e a força alta, um rulo
suave de prato filtrado; (d) a primeira nota de textura.

**B. Pre-swell manual (botão):** o músico aperta "Swell" alguns segundos antes de trocar o acorde.
Durante `SwellTime` (2–8 s) crescem: piano invertido na fundamental e na quinta do acorde atual (as
notas mais estáveis), prato invertido e o swell harmônico. O pico cai no fim do tempo.
* Se o acorde mudar durante o swell, as partes tonais saem em 0,4 s (nenhuma nota do acorde antigo
  fica soando contra o novo) e o prato completa o crescimento, emendando no bloom do novo acorde.
* Se o acorde não mudar, o swell "pousa" no próprio acorde e se dissolve.

Os dois são independentes: Bloom e BloomChance controlam o automático; o swell manual só acontece
quando o músico pede.

## 5. Estéreo
* Cada evento de textura ganha uma posição complementar à anterior (se a última nota veio da
  esquerda, a próxima vem do lado oposto, com variação), entre 25% e 70% do panorama.
* A camada de brilho e as teclas derivam devagar em velocidades diferentes (ciclos de ~40 s e
  ~65 s).
* Durante o bloom a largura sobe até +12% (M/S) e volta.
* Graves sempre mono; nenhum atraso entre canais; cada textura também soa completa em mono.

## 6. O que o maestro nunca faz
* Não dispara eventos em toda troca de acorde.
* Não gera notas fora do acorde.
* Não cria ritmo: sem grade, sem repetição periódica.
* Não aumenta o volume geral: as texturas entram por baixo do pad e o nível total é compensado
  pelo trim do preset.

## 7. Microeventos (v0.4)
Pequenos acontecimentos dentro do som: uma nota de sino ou de piano, um harmônico, um reverse curto, um toque de prato com arco, um arco de cordas que cresce e volta, uma rajada de fita. Podem não acontecer — e muitas vezes não acontecem.

**Quando:** o maestro acumula "unidades de evento" (taxa do preset × controle Microeventos) até um alvo sorteado (log-normal; às vezes uma pausa longa). Sem BPM, sem grade, sem repetição. Bloqueios: menos de 1,2 s de acorde; menos de 2,5 s desde o último evento; 3 microeventos tonais já soando; o mesmo tipo ainda em cooldown (sino 5 s, piano 4 s, pluck 5 s, harmônico 9 s, reverse 12 s, arco de cordas 14 s, rajada de fita 16 s, metal 22 s); 20% das vezes o evento simplesmente não acontece. Com trocas de acorde rápidas (média < 2,5 s) a taxa cai para um quarto.

**O quê:** sorteio ponderado pela paleta do preset × o nível da camada correspondente (sem cordas, não há arco de cordas) × a "história" do acorde (no começo, eventos discretos; com o acorde parado, os mais abertos) × espaço espectral (registro já ocupado pesa menos) × penalidade para o mesmo tipo duas vezes seguidas.

**Notas:** só notas do acorde atual, preferindo vizinhas da última (linha melódica sem saltos maiores que uma 5ª e sem a mesma nota três vezes). 7ª e 9ª só acima do Mi5 (MIDI 76), só se a tonalidade estimada tiver confiança e a cor pertencer à escala; 9ª nunca em Sus4, 7ª nunca em Pad5/Sus2/Sus4. `highBias` do preset puxa para o agudo (Air) ou para o médio (Warm, Vintage).

**Troca de acorde:** notas que não pertencem ao novo acorde são soltas em 0,6 s.

**Semente:** cada sessão começa com uma semente diferente; a mesma semente reproduz exatamente os mesmos eventos (usado nos testes e nos WAVs).

## 8. Identidade dos 8 presets (v0.4)
| Preset | Função | Por que existe | Microeventos |
|---|---|---|---|
| Warm | FOUNDATION | Corpo quente, estreito, Plate curto — base que não compete com a banda | raros: piano grave, arco |
| Strings | FOUNDATION | Cordas na frente, ataque lento — emoção, crescendo | arco de cordas, harmônicos, piano |
| Air | ATMOSPHERE | Aberto, agudo, sem peso — para cima da banda cheia | sinos e harmônicos no agudo |
| Ambient | ATMOSPHERE | Cauda de 14 s, ataque muito lento — momentos de oração | reverse, metal, harmônicos |
| Vintage | CHARACTER | EP + órgão + fita — calor imperfeito | piano/EP, pluck, rajada de fita |
| Cinematic | CHARACTER | Grave e vozes, cauda longa — abertura, clímax | reverse, arco, metal, piano |
| Soft Piano | TEXTURES | Pad atrás de notas de piano esparsas | piano frequente, sino raro |
| Reverse Bloom | TEXTURES | O acorde "floresce" depois do ataque | reverse, harmônicos |
