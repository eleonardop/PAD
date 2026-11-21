# Técnicas de síntese e DSP — comparação para o PAD

Notas: 1 (ruim) a 5 (ótimo). "CPU" = custo no callback (5 = barato). Custos medidos quando
indicado; os demais são estimativas **[hipótese]**.

| Técnica | Qualidade p/ pad/textura | Complexidade | CPU | Encaixe no PAD | Situação |
|---|---|---|---|---|---|
| PADsynth (tabelas pré-geradas) | 5 — corpo largo, sem batimento de chorus | 2 | 5 (leitura de tabela; ~0,85% de núcleo por voz medido na v0.2) | já é a base de Warm/Vox/Air/Glass | **mantido** |
| Wavetable sem aliasing | 3 — limpo, mas estático | 2 | 5 | Low e cordas sintéticas (v0.1) | **mantido** |
| Reprodução de samples (Hermite, ponto fixo) | 5 para instrumentos reais | 2 | 5 | cordas, piano, órgão; base das texturas novas | **ampliado** (one-shots e reverse) |
| FM (EP, sinos) | 4 — EP e sinos convincentes, timbre muda com a dinâmica | 3 | 4 em tempo real; 5 se pré-renderizado | EP sem samples de terceiros | **adotado, pré-renderizado** |
| Granular | 4 para texturas "nuvem" | 4 | 2 (muitos grãos por voz; ver RESEARCH §8) | sobreposição com PADsynth, que já dá o "freeze" sem custo | **descartado** nesta etapa |
| Processamento espectral em tempo real (phase vocoder) | 4 | 5 | 1–2 (FFT por bloco) | o freeze do piano já é feito offline | **descartado** no callback; usado offline |
| Convolução | 5 para salas reais | 3 | 2 (IR longas exigem FFT particionada) | precisaria de IRs licenciadas; Cloud/Plate já cobrem | **descartado** |
| Reverb algorítmico (Cloud Seed, Dattorro, FDN) | 5 / 4 / 3 | — | 10% / 5% / 6% de núcleo ARM (medido) | já existe; ganha "evolução" do envio e do tamanho | **mantido** |
| Chorus/ensemble | 4 | 2 | 5 | já existe | **mantido** |
| Physical modeling (cordas, metais) | 3–5 | 5 | 3 | custo de desenvolvimento alto; samples CC0 cobrem melhor | **descartado** |
| Envelopes complexos (multiestágio) | — | 2 | 5 | base do florescimento ("bloom") e dos swells | **adotado** |
| Filtros (ladder ZDF, SVF) e saturação | — | 2 | 5 | já existem; usados nas texturas | **mantido** |
| Estéreo M/S, pan de potência constante | — | 1 | 5 | largura sem cancelamento | **adotado** |
| Haas / inversão de fase | largura falsa | 1 | 5 | some ou muda o timbre em mono | **proibido** |
| Decorrelação por allpass | 3 | 2 | 4 | o reverb já descorrelaciona; allpass no seco muda transientes | **não usado** |
| Wow/flutter (linha de atraso modulada) + saturação | 3 — caráter vintage | 2 | 5 | preset Vintage; mesma modulação nos 2 canais (seguro em mono) | **adotado** |
| Ruído filtrado (noise swell) | 2 — chiado, contraria o objetivo | 1 | 5 | o swell "não tonal" vem de prato real | **descartado** como fonte |

## Por que pré-renderizar quase tudo
O callback tem orçamento fixo e o motor já roda a ~10% de um núcleo ARM. Tudo que não precisa
reagir em tempo real (EP por FM, reverse de piano e de prato, freeze espectral) é calculado
**uma vez no thread carregador** e vira uma tabela tocada como sample. No áudio sobra só leitura
com interpolação, envelope, filtro e pan, com custo previsível.
