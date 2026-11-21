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

A Evolução estica ou encurta esses tempos e escala a profundidade de cada mudança. Com Evolução 0
o pad fica praticamente parado e nenhum evento é gerado.

## 2. Distância harmônica (0 = mesmo acorde, 1 = muito distante)

`d = 0,5·(1 − notas comuns / notas do menor acorde) + 0,35·(distância entre fundamentais no círculo de quintas / 6) + 0,15·(mudança maior↔menor)`

Exemplos: G → Em (relativa) ≈ 0,26; G → C ≈ 0,32; G → D ≈ 0,32; G → Bb ≈ 0,78; mesmo acorde = 0.

Quando o acorde muda:
* a "idade" do organismo não volta a zero: `idade ← idade · (1 − d)`. Mudança pequena mantém o
  ambiente; mudança grande recomeça o ciclo;
* a força do bloom é `Bloom · (0,35 + 0,65·d) · fator de duração do acorde anterior` (acordes
  anteriores com menos de 1,5 s quase não disparam nada: em trocas rápidas o maestro fica quieto);
* o bloom só acontece se passar no sorteio de frequência (`BloomChance`, com mais chance quanto
  maior a distância) e se o último bloom foi há mais de 4 s.

## 3. Escolha de notas (texturas tonais)

Regras para **qualquer** nota que o maestro gera:
1. **Só notas do acorde atual.** Cor permitida: a 9ª, apenas em acordes que a admitem (Maior, Menor,
   Sus2, Add9, mAdd9, Maj7, Pad5), apenas com Evolução > 0,4 e só no registro alto (sparkle).
2. **Pad5 (1-5-8)** é ambíguo entre maior e menor: as texturas usam só fundamental, quinta e 9ª,
   nunca a terça — assim nada contradiz o que a banda está tocando.
3. **Nunca** 2ª menor, 9ª menor, trítono que não pertença ao acorde, nem 4ª sobre terça maior.
4. **Registro**: teclas (piano/EP) entre Mi4 e Ré6, acima da região central da voz; brilho (vibrafone)
   entre Mi5 e Dó7. Nada abaixo de Mi4: a região grave pertence ao pad e ao baixo.
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
