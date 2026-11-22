# Relatório da v0.5 — Pads de fundo

A v0.5 parte de 9 referências de pads de fundo enviadas pelo usuário. Os números abaixo medem espectro, largura e loudness. **Eles não dizem se o som ficou bom; quem decide é o ouvido.** Os arquivos de referência não fazem parte do repositório.

## 1. O que as referências mostraram
Das 9 referências, 8 têm as mesmas características:
* quinta pura, às vezes com 9ª;
* a 5ª forte e muitas vezes por baixo;
* corpo entre 125 e 500 Hz e agudo só como brilho distante;
* estabilidade, no máximo com uma ondulação lenta;
* estéreo de médio a muito largo.

A exceção é a referência 4, que é uma guitarra com swells e eco tocando notas da tonalidade.

Nenhum preset da v0.4 chegava perto dos pads escuros. A medida usada é a diferença de timbre: a média das diferenças em bandas de 1/3 de oitava entre 90 Hz e 5 kHz, com o corpo (125–630 Hz) alinhado. Por ela, o preset v0.4 mais próximo de cada pad escuro ficava a 6–25 dB.

## 2. O que foi feito
* **Categoria SOFT PADS** (no topo da tela) com 5 presets novos. Os 8 da v0.4 não mudaram.
* **Quinta por baixo**: um novo tipo de montagem de acorde. A 5ª fica abaixo da fundamental quando não desce além de Sol2, e as cores sobem uma oitava. As notas do acorde são as mesmas.
* **Abafar**: um filtro de 4 polos aplicado ao som inteiro, depois do reverb. Vai de 18 kHz (desligado) até cerca de 420 Hz e cai de forma suave, como nas referências. Está na tela principal.
* **Guitarra limpa por síntese própria** (Karplus-Strong com afinação fracionária, perda de agudos, compressor de sustentação e captador do braço), sem samples nem código de terceiros.
  * Toca em swells (volume subindo) com 2 ecos. O tempo do eco é sorteado a cada swell, entre 0,38 e 0,62 s, então não segue BPM.
  * As notas são do acorde, mais a 9ª e a 6ª maior. A 6ª só entra quando a tonalidade foi estimada. Nunca toca 4ª nem 7ª; no Pad5, nunca a terça.
* Testes novos (abaixo), pacote de escuta no CI, versão 0.5.0 (versionCode 5).

## 3. Presets novos

| Preset | Pensado para | Diferença de timbre: v0.4 mais próximo → v0.5 |
|---|---|---|
| Soft Pad | ref. 5 (B) | 3,0 dB (Soft Piano) → 3,6 dB |
| Shimmer Pad | ref. 1 (C), 3 (G), 9 (D) | 6,4 / 13,3 / 11,9 → 4,0 / 4,5 / 6,0 dB |
| Deep Pad | ref. 2 (G), 6 (Em7), 7 (F) | 18,7 / 13,7 / 24,7 → 6,7 / 9,0 / 5,0 dB |
| Wide Pad | ref. 8 (F) | 15,9 → 2,6 dB |
| Guitar Pad | ref. 4 (E) | 5,5 (Ambient) → 9,6 dB |

Observações:
* O Soft Pad não ficou mais próximo da referência 5 do que o Soft Piano da v0.4, que já estava perto. A diferença dele é ser um pad de quinta, sem notas de piano.
* O Guitar Pad tem **espectro médio mais distante** da referência 4 do que o Ambient. A referência é uma guitarra gravada, com notas mais agudas e mais densas. O que esse preset traz são os swells e as notas de guitarra, e só a escuta diz se isso está no caminho certo.
* Nos pads de fundo os microeventos ficam desligados. A exceção é o Shimmer Pad, que tem um harmônico raro.

## 4. Testes
* **Testes novos, todos passando localmente:**
  * `voicing_quinta_por_baixo`: 120 acordes, 0 problemas.
  * `abafar_desligado_neutro_e_fecha`: com Abafar em 0,75, a energia acima de 2 kHz cai de −23 para −40 dB.
  * `guitarra_afinada_e_sustentada`: 13 zonas, pior afinação 0,02 cents.
  * `guitarra_notas_seguras`: 221 notas, 0 problemas.
  * `presets_v05_pads_de_fundo`: 0 alocações, pior pico 0,37, pior perda em mono −2,3 dB, nível a ±1 dB do Warm; acima de 2 kHz o Deep Pad está 21 dB abaixo do Warm.
* **Testes antigos ajustados:** os da biblioteca v0.4 passaram a considerar só as 4 categorias antigas.
* **Loudness no pacote:** todos os arquivos ficaram entre −19,2 e −21,4 LUFS antes da normalização.
* **Mono:**
  * O Wide Pad é o mais largo: perde 2,7 dB em mono, e a pior banda cai 3,3 dB. A referência 8 perde 2,85 dB.
  * Os outros perdem de 1,7 a 2,2 dB.

## 5. Desempenho (CI ARM64, servidor — só referência, não é o celular)
Os pads de fundo usam 8,5% (Guitar Pad) a 10,3% (Wide Pad) de média de um núcleo, com p99 ≤ 11,7%: no nível dos presets da v0.4. A memória das fontes é bem menor (24–66 MB contra 131 MB do Warm), porque eles não usam cordas reais nem piano.

## 6. O que depende da escuta
* Se cada pad novo soa como a referência para a qual foi pensado, nos mesmos tons (pacote `ref*_v05_*` × `ref*_v04_*`).
* Se a quinta por baixo soa cheia ou embola nos tons graves (F e G).
* Até onde o Abafar é útil, e se o Deep Pad ficou fechado demais.
* Se a guitarra sintética soa como guitarra e se os ecos combinam com o culto.
* A carga do áudio no S23 Ultra. A guitarra é gerada no carregamento, então não deve pesar mais que o piano.

## 7. Versões preservadas
Branches `versao-0.1-motor`, `versao-0.2-som`, `versao-0.3-ambient` e `versao-0.4-refino`.
