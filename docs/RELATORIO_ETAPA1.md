# Relatório da Etapa 1 — Motor de áudio

Data: 21/11/2026. Medições feitas no servidor de desenvolvimento (Linux x86-64, 2 núcleos), com o mesmo código que vai no app.

## 1. Situação dos critérios de conclusão

| Critério | Situação |
|---|---|
| Motor C++ implementado | **Concluído**: camadas, vozes, DSP, efeitos, presets |
| Integração JNI/Kotlin funcional | **Implementada, compilação pendente de confirmação.** O código C++ da ponte e do Oboe foi verificado com os cabeçalhos reais do Oboe 1.9.3. O APK só pode ser compilado no GitHub Actions (o ambiente de desenvolvimento não tem acesso ao Android SDK). Ver §5 |
| Áudio reproduzido de verdade | **Concluído no motor** (WAVs gerados pelo mesmo código). No celular, depende do APK |
| Camadas combináveis | **Concluído**: 5 camadas com volume independente |
| Acordes e tonalidades com transição suave | **Concluído e medido** (ver §2) |
| Efeitos implementados | **Concluído**: reverb FDN, shimmer, filtros de tom, width, saturação suave, limitador |
| Testes automatizados executando | **Concluído**: 26 testes, todos aprovados. Também rodam com AddressSanitizer e UBSan, sem nenhum erro |
| Arquivos WAV gerados | **Concluído**: 21 arquivos + relatório de medições |
| Sem placeholders | Nenhuma função essencial está vazia ou simulada |
| Documentação | `docs/ARQUITETURA.md` e este relatório |

## 2. Resultados dos testes (os 15 itens pedidos + extras)

| # | Item pedido | Teste | Resultado medido |
|---|---|---|---|
| 1 | Inicialização e encerramento | `motor_inicializacao_e_encerramento` | Silêncio antes do `prepare`; taxa inválida é recusada; funciona a 44,1 e a 48 kHz; o motor é destruído com segurança mesmo com vozes tocando |
| 2 | Reprodução contínua | `motor_reproducao_continua` | 30 s: volume dentro de 3,5 dB (janelas de 0,5 s), índice de clique 1,86 (limite 3) |
| 3 | Alteração de tonalidades | `motor_troca_de_tonalidade` | 12 trocas pelo ciclo de quintas: pior queda 0,98 dB, pior pico +1,04 dB, sem cliques |
| — | Afinação | `motor_afinacao_12_tons` | Maior erro nos 12 tons: **0,06 cent** |
| 4 | Mudança de acordes | `motor_troca_de_acordes_voice_leading` | C → Am: a nota comum (Mi) continua na mesma voz, sem retrigger; 4 notas saem e 4 entram |
| 5 | Crossfade entre sinais | `crossfade_lei_correlacao`, `motor_crossfade_nivel_e_picos` | Lei com correlação: desvio ≤ 0,09 dB (ρ=0) e 0,00 dB (ρ=1). No motor, com crossfades de 0,25, 1 e 3 s, o nível fica dentro da faixa do acorde parado, com pico máximo de +0,46 dB |
| 6 | Camadas simultâneas | `motor_camadas_individuais_e_combinadas` | Cada camada isolada tem nível útil (−22 a −24 dB) e região própria (Low 61 Hz, Warm 612 Hz, Shimmer 916 Hz, Air 4 kHz de centroide); juntas: −16,3 dB, sem clipping |
| 7 | Gerenciamento de vozes | `motor_gerenciamento_de_vozes` | Contagem exata; todas as vozes são liberadas após o Release; tocar de novo durante o Release reaproveita as vozes |
| 8 | Voice stealing | `motor_voice_stealing` | 63 roubos, máximo de 46 de 48 vozes, nenhum roubo forçado, nenhuma nota do acorde atual roubada, sem cliques (1,77), zero alocações |
| 9 | Trocas rápidas e sucessivas | `motor_trocas_rapidas_sucessivas` | 119 trocas aleatórias (15–250 ms) com o preset Shimmer: sem clipping, sem cliques, volume não passa de 3 dB acima do normal |
| 10 | Ring buffer | `ring_buffer_spsc_estabilidade` | 2 milhões de comandos entre 2 threads, todos em ordem; fila cheia e vazia tratadas sem bloqueio |
| 11 | Ausência de clipping | `motor_sem_clipping_pior_caso` | Todos os parâmetros no máximo + Add9 + trocas a cada 0,5 s: pico 0,891 (−1 dBFS), **nenhuma** amostra ≥ 0,999 |
| 12 | Controle de picos | `limitador_sem_overshoot`, `motor_crossfade_nivel_e_picos` | Limitador sem overshoot com excesso de +13 dB; picos nos crossfades ≤ +0,46 dB |
| 13 | Processamento estéreo | `motor_processamento_estereo` | Width 0 → mono exato (correlação 1,000); 1 → 0,38; 1,5 → −0,10. Perda ao somar em mono ≤ 3,4 dB |
| 14 | Consumo de recursos | `motor_consumo_de_recursos` | **Zero alocações** no callback. Acorde típico: 2,3% de um núcleo; ~13 vozes com todas as camadas: 4,7%; custo por voz 0,25%; efeitos ~1%; banco de samples 17,3 MB |
| 15 | Reprodução prolongada | `motor_reproducao_prolongada_10min` | 10 min com trocas aleatórias e 10 trocas de preset: volume por minuto entre −21 e −18 dB, DC 7·10⁻⁷, todas as vozes liberadas no fim |
| — | Interpolação | `interpolacao_hermite_vs_linear` | Hermite 62,3 dB × linear 37,1 dB de SNR (+25 dB), o que justifica a escolha |
| — | Aliasing dos osciladores | `wavetable_sem_aliasing` | Componentes não harmônicas abaixo de −126 dB |
| — | Loops de samples | `samples_loop_sem_emenda` | Salto na junção igual ao de qualquer amostra vizinha; WAV externo com 0,16 dB de variação no crossfade |
| — | Reverb | `reverb_decaimento_e_shimmer_estavel` | RT60 exato (20,0 dB/s para 3 s); estável com shimmer máximo |
| — | Presets | `motor_presets_equilibrio` | Os 6 presets ficam dentro de 2,5 dB entre si e nenhum aciona o limitador |
| — | Parâmetros | `motor_parametros_limites_seguros` | Faixas e valores inválidos (NaN) tratados; 10 controles mudando a cada 250 ms sem cliques |
| — | Detector de cliques | `detector_de_clique_valido` | O detector usado nos testes acusa um degrau real de −3 dB (5,4) e não acusa o som limpo (1,5) |

O log completo de cada execução fica disponível no GitHub Actions (artefato `PAD-wav-exemplos-e-testes`).

## 3. Arquivos WAV para avaliação auditiva

Ficam no artefato `PAD-wav-exemplos-e-testes` (pasta `wav_exemplos/`, PCM 24 bits, 48 kHz):

* `01`–`05`: cada camada isolada.
* `06`–`07`: combinações de camadas.
* `08`: os 12 tons pelo ciclo de quintas.
* `09`: progressão G–D–Em–C (I–V–vi–IV) com crossfade.
* `10`: crossfades rápidos (troca a cada 0,3 s).
* `11`–`16`: os 6 presets com a mesma progressão.
* `17`–`21`: efeitos (reverb, shimmer, largura, brilho/calor, movimento).
* `relatorio.txt`: RMS, pico, correlação L/R e atuação do limitador de cada arquivo.

**Importante:** os testes verificam o áudio matematicamente, mas a qualidade final precisa ser julgada de ouvido, de preferência em fones bons e no sistema de som da igreja. Eu não tenho como ouvir os arquivos.

## 4. Limitações conhecidas (com proposta)

1. **Samples de cordas são sintéticos.** Não havia gravações licenciadas. O banco gerado pelo motor é original e livre de direitos, mas uma gravação real de qualidade pode soar mais orgânica.
   *Proposta:* gravar ou licenciar loops sustentados (1 nota a cada 4 semitons, ≥ 8 s, 48 kHz) e carregá-los com `addZoneFromAudio()`, que já está pronto e testado.
2. **Tempo de preparação.** O bake do banco leva ~1,8 s no servidor (2 núcleos). Num celular com 8 núcleos deve ser parecido. Ele acontece uma vez ao abrir o app.
   *Proposta:* gravar o banco em cache no armazenamento do app na primeira execução.
3. **Polifonia no aparelho.** O custo por voz (0,25% de um núcleo no servidor) deve ser de 3 a 5 vezes maior num núcleo de celular. No uso real (até ~20 vozes) isso fica abaixo de ~25% de um núcleo, com folga.
   *Proposta:* medir no Galaxy (a carga do callback já é calculada em `PadEngine.stats().load`) e, se necessário, otimizar com NEON.
4. **Foco de áudio.** O app não pede foco de áudio, para não ser pausado por notificações durante o culto. Outro app que toque mídia vai se misturar com o pad. Isso é intencional, mas deve ser decidido junto com a interface.
5. **APK não compilado aqui.** Ver §5.

## 5. Como obter o APK e os WAVs

A cada envio ao repositório, o GitHub Actions executa o fluxo **"Compilar e testar"**:
* **Motor C++:** compila, roda os 26 testes, roda os testes com sanitizers e gera os WAVs (artefato `PAD-wav-exemplos-e-testes`);
* **App Android:** compila o APK (artefato `PAD-Worship-APK`).

Se o job do Android falhar na primeira execução, o log mostra a linha exata do erro. Nesse caso, a correção é pontual na configuração do Gradle.

## 6. Próxima etapa sugerida

* Interface do músico sobre a API `PadEngine` (Kotlin), incluindo seleção de tom, tipo de acorde, presets e os controles principais.
* Cache do banco de samples e, se houver material licenciado, troca por gravações reais.
* Medição de CPU/latência no aparelho real e ajuste fino dos presets de ouvido.
