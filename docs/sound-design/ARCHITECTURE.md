# Arquitetura sonora — auditoria da v0.2 e organização da v0.3

## 1. Auditoria da v0.2 (o que existe e funciona)

| Parte | Onde | Estado | Observação |
|---|---|---|---|
| Callback | `PadEngine::process` → `handleCommands` → sub-blocos de 32 amostras (`updateControl` + `renderSubBlock`) | sólido | sem alocação, sem trava; carga ~10% de um núcleo ARM (medido no S23 Ultra: 7,6–8,5%) |
| Comandos | fila SPSC (`RingBuffer`) com PlayChord/Stop/Panic | sólido | ponto natural para Swell manual |
| Vozes | `VoiceManager`: 48 vozes, voice leading, crossfade equal-power, stealing com reserva | sólido | cada voz lê 6 camadas de tabela + Low; o maior consumidor de CPU |
| Fontes | `SourceLibrary`: thread carregador, ponteiros atômicos, liberação após 2 blocos sem uso | sólido | amostras todas em loop; **não há one-shots nem reverse** |
| Parâmetros | 32 atômicos + suavização por parâmetro | sólido | acrescentar no fim mantém os ids |
| Presets | 8, com `trimDb` | ok | **musicalmente parecidos** entre si: todos são "pad + um pouco de cada camada"; nenhum tem uma segunda parte musical |
| Evolução | 3 moduladores globais (senos de razão áurea) + 30% por voz: morph, filtro, respiração | ok | **puramente temporal**: não sabe nada de acorde, idade do acorde ou troca |
| Transições | crossfade entre acordes | ok | nenhuma transição musical (bloom, swell) |
| Reverb/Chorus/Shimmer | Cloud/Plate/Clássico, ensemble, shimmer de banda limitada | sólido | o envio ao reverb é fixo durante o acorde |
| Estéreo | leituras L/R independentes + detune + M/S com graves mono | ok | correlação ~0,25–0,40; nenhuma camada se move no panorama |
| JNI/Oboe | `JniBridge.cpp`, `OboePlayer` (AAudio, buffer = 4 bursts) | sólido | acrescentar `nativeSwell` |
| Testes | 43 (`pad_tests`), benchmark, cenários, ReverbLab | sólido | precisam de testes para eventos, texturas e mono |

**Limitações que explicam a avaliação "falta criatividade" [hipótese a confirmar de ouvido]:**
1. tudo o que soa é sustentado e simultâneo: não existe nenhum evento musical (nada "acontece");
2. a Evolução é um movimento contínuo e cíclico, sem relação com a música;
3. as trocas de acorde só fazem crossfade: não há sensação de "novo ambiente";
4. os presets mudam proporções, não papéis.

## 2. Organização da v0.3 (sem fragmentar o código)

As sete responsabilidades pedidas foram mapeadas sobre o que existe, com **três peças novas**:

| Papel | Implementação | Novo? |
|---|---|---|
| PAD | Warm + Vox + Strings + Texture sustentada (as camadas das vozes da v0.2) | não |
| LOW | camada Low | não |
| AIR | camada Air + envio ao reverb | não (o envio passa a evoluir) |
| SPARKLE | Glass (PADsynth) + notas de vibrafone geradas pelo maestro | parcialmente |
| TEXTURE | notas esparsas de piano suave ou EP, geradas pelo maestro | **sim** (`TexturePlayer`) |
| TRANSITION | bloom pós-ataque, pre-swell manual, pratos | **sim** (`TexturePlayer` + `Conductor`) |
| MOTION | ciclo de vida do acorde (estados), Evolução, deriva estéreo | **sim** (`Conductor`) |

* **`Conductor`** (maestro, `core/Conductor.*`): uma máquina de estados por acorde que roda no
  thread de áudio em taxa de controle. Decide *quando* e *o quê*; não gera áudio. Publica ganhos
  extras por camada (segunda camada que cresce), deslocamentos de brilho, envio e largura, e
  pedidos de nota para o `TexturePlayer`.
* **`TexturePlayer`** (`core/TexturePlayer.*`): 24 vozes leves de reprodução (one-shot, sem loop),
  separadas das 48 vozes do pad. Cada uma: leitura Hermite em ponto fixo, avanço normal ou
  invertido, envelope de 3 estágios, SVF passa-baixa/passa-alta, pan de potência constante.
  Saída em dois barramentos: tonal (vai para o corpo, sem ensemble) e metal (não tonal, com
  filtros próprios e envio maior ao reverb).
* **`TapeCharacter`** (`dsp/TapeCharacter.h`): wow + flutter + saturação + perda de agudos no
  barramento do pad, com a mesma modulação nos dois canais.

As fontes novas (piano suave, vibrafone, pratos, EP e reverses) entram na mesma `SourceLibrary`,
com carregamento sob demanda e liberação segura: o `TexturePlayer` publica o uso das fontes do
mesmo jeito que as vozes.

## 3. Orçamento de vozes
As notas de textura **não usam vozes do pad**: tocam no `TexturePlayer`, que custa uma leitura de
sample estéreo + filtro por voz (~1/6 de uma voz de pad, que lê 6 camadas). As 48 vozes do pad
continuam como estão. Estimativa **[hipótese]**: com 8–12 texturas soando ao mesmo tempo (bloom +
notas + prato), +1 a 2 pontos percentuais de CPU no ARM. Expansão para 64–96 vozes de pad **não é
necessária**: o pior caso da v0.2 (trocas rápidas) não passou de 12 vozes ativas.

Roubo no `TexturePlayer`: prioridade swell > prato > teclas > brilho; a vítima é a voz de menor
prioridade e menor nível, com fade de 30 ms.
