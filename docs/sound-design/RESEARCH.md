# Pesquisa — referências de sound design para ambient/modern worship

Base da v0.3. Cada afirmação está marcada como **[verificado]** (fonte pública citada),
**[hipótese]** (inferência minha, a confirmar de ouvido) ou **[decisão]** (escolha de projeto).

## 1. Nord Stage / Electro: o que é público e o que importa para o PAD

* **[verificado]** O Nord Stage 4 tem três motores (Piano, Organ, Synth) com 7 camadas no total
  (2 Organ, 2 Piano, 3 Synth), cada uma com fader próprio, e efeitos completos por camada
  ([soundtech.co.uk](https://soundtech.co.uk/music-retail/nord/news/nord-stage-4-whats-new),
  [nordkeyboards.com](https://www.nordkeyboards.com/products/nord-stage-4/specifications/)).
  A geração anterior usava o conceito "Panel A/B" com duas instâncias de cada motor
  ([Guitar Center / Nord](https://www.guitarcenter.com/riffs/product-demos/keyboards--midi/nord-stage-4-stage-keyboard)).
* **[verificado]** O synth é baseado no Nord Wave 2 (analógico virtual, samples, FM e wavetable)
  ([nordkeyboards.com](https://nordkeyboards.com/products/nord-stage-4)); o Electro 6 traz uma
  seção "Sample Synth" com Attack, Decay/Release e controle dinâmico, e transições sem corte de
  notas ao trocar de som ("Seamless Transitions")
  ([nordkeyboards.com](https://nordkeyboards.com/products/nord-electro-6),
  [Sonicstate](https://www.sonicstate.com/news/2018/04/27/official-nord-electro-6-releaee-video/)).
* **[verificado]** O Nord Piano 6 e o Stage 4 têm "Unison" no sample synth (várias vozes de sample
  sobrepostas para um som de conjunto) e "Tru-Vibrato"
  ([nordkeyboards.com](https://www.nordkeyboards.com/products/nord-piano-6/)).

**O que isso ensina ao PAD [decisão]:** o "som Nord" de worship não é um timbre secreto; é a
**arquitetura de camadas independentes com fader e efeito próprios**, usada para empilhar fontes
de natureza diferente (piano + pad + strings) em volumes muito diferentes, e a continuidade nas
trocas. Não há informação pública sobre o conteúdo interno das amostras ou dos presets Nord, e
nada disso foi copiado ou presumido.

## 2. Como tecladistas de worship constroem o som

* **[verificado]** Piano com um pad suave por baixo é a combinação base; a camada contínua faz as
  transições soarem ligadas ([The Mystic Keys](https://themystickeys.com/?p=44708)). Pads somam bem
  ao piano principalmente com volume baixo ([Yamaha Hub](https://hub.yamaha.com/keyboards/k-how-to/layering-digital-keyboard-sounds)).
* **[verificado]** Combinações recorrentes: piano + strings, piano + pads, synth + choir, EP +
  texturas ambientes, órgão + elementos orquestrais ([The Mystic Keys](https://themystickeys.com/?p=44807)).
* **[verificado]** Um "drone" por baixo permite tocar o piano de forma esparsa sem a música "morrer"
  ([Worship Online, Matt Stanfield](https://worshiponline.com/using-drones-and-playing-keys-in-worship-with-matt-stanfield/)).
* **[verificado]** Patches profissionais de keys são montados com camadas que entram por
  "snapshots" ao longo da música: pads mais altos e novos synths entrando na construção, e um final
  só com piano e pad ([Sunday Sounds](https://sundaysounds.com/blogs/news/the-ultimate-ready-for-anything-worship-keys-patch)).
  Músicas aparentemente "piano + pad" costumam ter duas partes de piano e um pad "colando" a banda
  ([Sunday Sounds](https://sundaysounds.com/blogs/news/keys-sounds-for-modern-worship-sunday-keys-app-piano-tutorial)).
* **[verificado]** Excesso de piano numa banda completa embola o som; sozinho ou atrás de uma fala, sons
  mais diretos funcionam ([David Santistevan](https://www.davidsantistevan.com/worship-keyboard/)).

**Consequências [decisão]:**
1. A "criatividade" que falta não é mais efeito: é **uma segunda parte musical esparsa** (notas de
   piano/EP/mallet isoladas, sem ritmo marcado) e **camadas que entram e saem ao longo do tempo**,
   como os snapshots de um tecladista.
2. As texturas ficam **bem abaixo** do pad, com muito reverb e filtro, para não disputar com voz e
   instrumentos.

## 3. Pratos, swells e reverse

* **[verificado]** O swell real de prato é feito com rulo de baquetas macias (lã/feltro) perto da
  borda, em prato fino que responde em volume baixo; "use com moderação"
  ([Percussion for Worship](https://percussionforworship.blogspot.com/2021)). Baquetas de lã dão
  "washes" cheios com toques leves e crash forte com toques fortes
  ([Guitar Center / Zildjian](https://www.guitarcenter.com/Zildjian/Cymbal-Mallets-Black-1273887990017.gc)).
* **[verificado]** Um prato tocado ao contrário (reverse crash) não é a mesma coisa que um swell:
  é a cauda invertida de um ataque ([KVR](https://www.kvraudio.com/forum/viewtopic.php?p=4021553));
  reverses com reverb ou delay soam mais "flutuantes" ([MusicRadar](https://www.musicradar.com/tuition/tech/10-steps-to-more-effective-transitions-605656)).
* **[verificado]** O raspado e o arco no prato funcionam em texturas suaves; o sino do prato dá brilho
  colorístico ([Percussion for Worship](https://percussionforworship.blogspot.com/2018/12)).

**Consequências [decisão]:** usar gravações reais de **rulo crescente com baqueta macia**, **rulo pp**
e **prato com arco** (as três existem no VCSL, CC0). O reverse cymbal é derivado de um ataque pp
invertido. Tudo filtrado (passa-alta ~300 Hz, passa-baixa ≤ 6 kHz) e enviado mais ao reverb do que
direto.

## 4. EP, piano suave e mallets

* **[verificado]** A síntese FM é especialmente boa em sinos, timbres metálicos e no "tine" de piano
  elétrico ([Apple Logic Pro](https://help.apple.com/logicpro/mac/10.4/en.lproj/lgsife418213.html)); o EP
  clássico usa um modulador de razão alta para o ataque metálico do tine
  ([Attack Magazine](https://attackmagazine.com/?p=22387)); o que fazia o EP interessante era a resposta
  de timbre à dinâmica, não imitar piano ([Sound On Sound via muzines](https://www.muzines.co.uk/articles/practically-fm/3945?theme=1)).
  A patente do FM expirou em 1995 ([B&H](https://www.bhphotovideo.com/explora/node/61626)).

**Consequências [decisão]:** EP **sintetizado por FM próprio** (dois pares de operadores: corpo 1:1 e
"tine" de razão alta com decaimento rápido), renderizado em tabelas fora do áudio. Piano suave e
vibrafone de baqueta macia vêm de samples CC0 (VCSL).

## 5. Tape

* **[verificado]** Wow = variação lenta de velocidade (ondas), flutter = variações rápidas
  ([Splice](https://splice.com/blog/?p=28861)); um atraso modulado de ~10 ms com LFO de 0,01–1 Hz
  produz o wow ([MusicRadar](https://musicradar.com/how-to/analogue-tape-effects-ableton-live-delay));
  a fita atenua os agudos ([Attack Magazine](https://attackmagazine.com/?p=86844)).

**Consequência [decisão]:** "Tape" é um **caráter** (wow + flutter leves, saturação suave e perda de
agudos), sem chiado: ruído de fita contraria o objetivo de não ter ruído.

## 6. Mono e estéreo

* **[verificado]** Atrasos Haas somem ou mudam de timbre em mono por filtragem em pente; largura por
  Mid/Side não sofre esse problema ([Sound On Sound](https://www.soundonsound.com/node/4911610),
  [iZotope](https://www.izotope.com/en/learn/what-is-the-haas-effect)). Lado demais = buraco no meio e
  mono fraco; graves devem ficar no centro (~100–150 Hz)
  ([Sound On Sound](https://www.soundonsound.com/node/4904836),
  [Sound On Sound](https://www.soundonsound.com/sound-advice/q-how-can-create-fake-ms-setup-mono-compatible)).

**Consequência [decisão]:** nenhuma técnica Haas nem inversão de fase. Largura vem de fontes
diferentes em posições complementares (pan de potência constante), da descorrelação natural do
reverb e de M/S controlado; os graves continuam mono abaixo de 140 Hz.

## 7. Distância harmônica

* **[verificado]** Em testes com ouvintes, a distância "Tonnetz" e a distância espectral de classes
  de altura previram a distância percebida entre tríades melhor que uma simples contagem de notas
  comuns, e a distância de voice leading foi pior que essa contagem
  ([Milne & Holland 2016](https://core.ac.uk/download/82982228.pdf)). No Tonnetz, acordes vizinhos
  têm duas notas comuns ([Tymoczko, MTO](https://mtosmt.org/issues/mto.10.16.1/mto.10.16.1.tymoczko.html)).

**Consequência [decisão]:** a distância harmônica do PAD combina **notas comuns** com **distância no
círculo de quintas entre as fundamentais** e **mudança de modo** (uma aproximação do Tonnetz). Ela
decide o tamanho da transição.

## 8. Síntese granular

* **[verificado]** Motores granulares são conhecidos pelo alto custo de CPU em uso de pad
  ([KVR](https://www.kvraudio.com/forum/viewtopic.php?p=8890256),
  [KVR](https://www.kvraudio.com/forum/viewtopic.php?p=5006911)) e tendem a sons "nebulosos, congelados
  ou fragmentados" ([Sound On Sound](https://www.soundonsound.com/node/4929457)).

**Consequência [decisão]:** granular fica de fora nesta etapa (ver `DSP_REFERENCE.md`).
