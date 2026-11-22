# Relatório da v0.6 — Pads aveludados, encerramento correto e interface final

## 1. Som: pads aveludados
A pedido do usuário, depois de ouvir a v0.5 ("muito abertos e agressivos"):

* **Novo parâmetro Aveludado** (`Velvet`, adicionado no fim da lista de parâmetros; os presets antigos não mudam):
  * O filtro de cada nota abre junto com o volume. A nota nasce escura e floresce: na entrada, o agudo acima de 1,5 kHz cai cerca de 7 dB.
  * Sem ressonância nem saturação no filtro das vozes.
  * Agudos mais no centro: acima de 3 kHz, o lado do estéreo cai cerca de 7 dB.
  * Reverb mais escuro, para a cauda se misturar ao corpo.
* **Os 5 SOFT PADS foram refeitos** com Aveludado e reverb integrado:
  * sem pré-delay e quase sem reflexões separadas;
  * difusão alta e cauda escura;
  * entrada de 4 s e troca de 3,5 s;
  * menos detune L/R e menos largura.
* **Versões da v0.5:** saíram da tela principal e ficaram só em "Avançado".
* **Mono:** a perda caiu de 1,7–2,7 dB para 1,2–1,8 dB.
* **Pacote de escuta:** só as versões novas, nos tons das 9 referências, mais uma progressão em G e versões mono, em −20 LUFS.

O som não foi aprovado por métricas. Os números só mostram que a mudança foi na direção pedida.

## 2. Por que o app não fechava (causas encontradas)
1. **O serviço em primeiro plano nunca parava.** Ele era iniciado na primeira nota com `START_STICKY` e só parava por uma ação de encerrar que não aparecia em lugar nenhum. Resultado: o processo e a notificação continuavam vivos, e com `STICKY` o Android ainda podia recriar o serviço.
2. **O motor nativo nunca era liberado.** Ele era criado uma vez (`by lazy`) e `nativeDestroy` nunca era chamado. Assim, a saída de áudio, o thread de áudio, o carregador de fontes e cerca de 100 MB de samples ficavam ocupados até o Android matar o processo.
3. **A saída de áudio ficava aberta em segundo plano mesmo sem som.** O callback continuava rodando e gastando CPU.
4. **A atualização da tela (a cada 0,5 s) continuava com o app minimizado.**
5. **Reabertura do áudio em segundo plano:** uma troca de fone/Bluetooth durante o encerramento podia reabrir um stream depois de "parar", porque a reabertura não conferia o estado a cada tentativa.
6. **Girar a tela recriava a Activity.**

## 3. O que foi feito
* **Motor liberável.**
  * O `PadEngine` (Kotlin) cria o motor sob demanda e o destrói em `release()`.
  * Antes de destruir, o volume desce suavemente (sem estalo), depois a saída de áudio fecha e o motor é destruído (thread carregador encerrado, samples e buffers liberados).
  * Uma trava de leitura/escrita impede qualquer chamada de usar um motor já destruído.
* **Serviço só enquanto há som.**
  * Ele usa `START_NOT_STICKY` e para sozinho quando as notas terminam.
  * Se a tela não estiver visível nesse momento, a saída de áudio também fecha.
  * A notificação ganhou o botão **Encerrar**.
* **Formas de sair, todas liberando tudo:**
  * botão **Sair** (com confirmação se estiver tocando);
  * **Encerrar** na notificação;
  * **voltar** sem som (voltar com som só minimiza e o som continua, como num player);
  * **remover dos recentes** (`onTaskRemoved`).
* **Segundo plano sem som:** depois de 10 s a saída de áudio fecha (CPU zero). Ao voltar, ela reabre e preset e ajustes são mantidos.
* **Girar a tela** não recria nada; só a interface é remontada.
* **Threads.**
  * O thread de controle termina sozinho 3 s depois da última tarefa.
  * O carregador de fontes agora tem nome (`pad-sources`), para aparecer nos diagnósticos.
  * O teste de criar e destruir o motor 3 vezes confirma que nenhum thread sobra.

## 4. Interface: avaliação e mudanças
**Avaliação da tela anterior (v0.5):**
* No Android 15 o conteúdo ficava atrás da barra de status e da barra de navegação.
* As 12 tonalidades e o PARAR ficavam abaixo da lista de sons: no culto era preciso rolar para tocar.
* Com 5 categorias (13 sons), a lista ficava longa.
* Spinners e controles eram os padrões do Android, com cara de "bancada de teste".
* Não havia indicação clara de "tocando" além da tecla acesa, nem botão de sair.
* Não havia leiaute para celular deitado nem para tablet.

**Nova tela:**
* **Topo:** nome, indicador de estado (ponto verde pulsando quando toca, com som e tom) e **Sair**.
* **Biblioteca:**
  * categorias em abas;
  * sons da categoria em cartões, com nome e descrição curta;
  * som ativo com contorno e texto dourados;
  * descrição completa logo abaixo.
* **Painel de execução sempre visível:**
  * tipo de acorde em botões (em vez de lista suspensa), as 12 tonalidades grandes, SWELL e PARAR;
  * no celular em pé fica embaixo, ao alcance do polegar;
  * deitado ou em tablet fica à direita (em celular deitado, compacto e rolável).
* **Controles:** os 6 controles principais num cartão; "Avançado" recolhido com tudo o que já existia (versões anteriores, todos os parâmetros, desempenho, créditos).
* **Identidade:**
  * grafite escuro, dourado quente como destaque, vermelho apagado só no PARAR, verde só no "tocando";
  * fonte sans-serif-medium nos títulos;
  * cantos arredondados e espaçamentos consistentes;
  * ícone do app no mesmo dourado.
* **Animações discretas:** toque com leve redução, cartões aparecendo ao trocar de categoria, pulso do indicador de "tocando".
* **Áreas seguras** do Android 15 (barras e recorte da câmera) respeitadas.

## 5. Verificação
* **Motor (CI):**
  * testes em x86-64 e ARM64, com ASan/UBSan;
  * novos testes de Aveludado e de criar e destruir o motor.
* **Emulador Android 14 (CI), script `tools/device/check_lifecycle.sh`:**
  * passos: abrir, tocar, minimizar tocando, voltar, PARAR, segundo plano sem som, voltar, girar tocando, Sair, abrir e voltar sem som, tocar e voltar, Encerrar pela notificação;
  * em cada passo confere serviço, tela, abertura e fechamento da saída de áudio, threads, CPU e memória;
  * capturas de tela em 4 resoluções (compacto 720p, padrão, S23 Ultra 1440×3088 e tablet), em pé e deitado;
  * os resultados estão no artefato **PAD-dispositivo-ciclo-de-vida-e-telas**.
* **Limites:**
  * o emulador não substitui o S23 Ultra: o som em si, a latência e a carga real do áudio dependem do aparelho;
  * interrupções como chamada telefônica e foco de áudio de outros apps não foram tratadas nesta versão.
