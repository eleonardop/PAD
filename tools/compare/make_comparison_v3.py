#!/usr/bin/env python3
"""Pacote de comparação v0.2 × v0.3 (testes A–M), estéreo e mono.

Entrada: as pastas geradas pelo pad_scenarios_v3 compilado contra a v0.2 (-DPAD_V2) e contra a
versão atual. Saída (pasta destino):
  estereo/v0.2/*.wav, estereo/v0.3/*.wav  — loudness igualado em -20 LUFS (true peak ≤ -1 dBTP).
      A normalização é um ganho único por arquivo: dinâmica interna e timbre não mudam.
  mono/v0.2/*.wav, mono/v0.3/*.wav        — soma mono (L+R)/2 dos arquivos normalizados, SEM
      renormalizar: a perda de volume em mono fica audível como é.
  espectrogramas/*.png                    — v0.2 em cima, v0.3 embaixo (ou só v0.3, nos novos).
  medicoes.csv / medicoes.md              — medidas dos arquivos originais, incluindo mono:
      perda em mono, pior banda de 1/3 de oitava (cancelamento) e mudança de timbre em mono
      (maior desvio de uma banda em relação à perda média).
Uso: make_comparison_v3.py <pasta_v02> <pasta_v03> <destino>
"""
import csv
import os
import sys

import numpy as np
from scipy import signal
from scipy.io import wavfile

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "analysis"))
import audio_metrics as am  # noqa: E402

import matplotlib  # noqa: E402
matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402


def write24(path, sr, x):
    q = np.clip(np.round(x * 8388607.0), -8388608, 8388607).astype(np.int32) << 8
    wavfile.write(path, sr, q)


def mono_metrics(x, sr):
    """Compara a soma mono com a média de energia L/R por banda de 1/3 de oitava (100 Hz–10 kHz)."""
    l, r = x[:, 0], x[:, 1]
    m = 0.5 * (l + r)
    f, pl = signal.welch(l, sr, nperseg=16384)
    _, pr = signal.welch(r, sr, nperseg=16384)
    _, pm = signal.welch(m, sr, nperseg=16384)
    pst = 0.5 * (pl + pr)
    sel = (f >= 100) & (f <= 10000)
    total = 10 * np.log10(pm[sel].sum() / pst[sel].sum())
    edges = 100 * 2 ** (np.arange(0, 21) / 3)
    bands = []
    for a, b in zip(edges[:-1], edges[1:]):
        s = (f >= a) & (f < b)
        if pst[s].sum() > 0:
            bands.append(10 * np.log10(pm[s].sum() / pst[s].sum()))
    bands = np.array(bands)
    return float(total), float(bands.min()), float(np.abs(bands - total).max())


def spectro(ax, path, title):
    sr, x = am.load(path)
    mm = x.mean(axis=1)
    f, t, S = signal.spectrogram(mm, sr, nperseg=4096, noverlap=3072, window="hann")
    SdB = 10 * np.log10(S + 1e-14)
    ax.pcolormesh(t, f, SdB, vmin=SdB.max() - 90, vmax=SdB.max(), shading="auto", cmap="magma")
    ax.set_yscale("symlog", linthresh=200)
    ax.set_ylim(30, 20000)
    ax.set_ylabel("Hz")
    ax.set_title(title, fontsize=10)


def main():
    a_dir, b_dir, out = sys.argv[1:4]
    for sub in ("estereo/v0.2", "estereo/v0.3", "mono/v0.2", "mono/v0.3", "espectrogramas"):
        os.makedirs(os.path.join(out, sub), exist_ok=True)
    names = sorted(f for f in os.listdir(b_dir) if f.endswith(".wav"))
    rows = []
    for n in names:
        pairs = [("v0.3", os.path.join(b_dir, n))]
        pa = os.path.join(a_dir, n)
        if os.path.exists(pa):
            pairs.insert(0, ("v0.2", pa))
        for tag, p in pairs:
            sr, x = am.load(p)
            g, lim = am.normalize_to_lufs(p, os.path.join(out, "estereo", tag, n), -20.0, -1.0)
            y = x * 10 ** (g / 20)
            mm = 0.5 * (y[:, 0] + y[:, 1])
            write24(os.path.join(out, "mono", tag, n), sr, np.stack([mm, mm], axis=1))
            met = am.measure(p, 1.0)
            loss, worst, timbre = mono_metrics(x[int(sr):], sr)
            met.update(versao=tag, ganho_normalizacao_db=g, limitado_pelo_pico=lim, mono_perda_db=loss,
                       mono_pior_banda_db=worst, mono_desvio_timbre_db=timbre)
            rows.append(met)
        rows_n = len(pairs)
        fig, ax = plt.subplots(rows_n, 1, figsize=(12, 3.6 * rows_n), sharex=True, squeeze=False)
        for i, (tag, p) in enumerate(pairs):
            spectro(ax[i][0], p, f"{tag} — {n}")
        ax[-1][0].set_xlabel("s")
        fig.tight_layout()
        fig.savefig(os.path.join(out, "espectrogramas", n.replace(".wav", ".png")), dpi=65)
        plt.close(fig)
        print(n)

    keys = ["arquivo", "versao", "lufs", "lra", "true_peak_db", "clip", "crista_db", "centroide_hz", "acima_10k_db",
            "correlacao_lr", "mono_perda_db", "mono_pior_banda_db", "mono_desvio_timbre_db", "bandas_pct",
            "ganho_normalizacao_db", "limitado_pelo_pico"]
    with open(os.path.join(out, "medicoes.csv"), "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(keys)
        for r in rows:
            w.writerow([(";".join(f"{v:.1f}" for v in r[k]) if isinstance(r[k], list) else
                         (f"{r[k]:.2f}" if isinstance(r[k], float) else r[k])) for k in keys])
    with open(os.path.join(out, "medicoes.md"), "w") as fh:
        fh.write("| arquivo | versão | LUFS | LRA | true peak | clip | centróide | >10 kHz | correl. L/R | perda mono | pior banda mono | desvio de timbre mono |\n")
        fh.write("|---|---|---|---|---|---|---|---|---|---|---|---|\n")
        for r in rows:
            fh.write(f"| {r['arquivo']} | {r['versao']} | {r['lufs']:.1f} | {r['lra']:.1f} | {r['true_peak_db']:.1f} | {r['clip']} | "
                     f"{r['centroide_hz']:.0f} Hz | {r['acima_10k_db']:.1f} dB | {r['correlacao_lr']:.2f} | {r['mono_perda_db']:.1f} dB | "
                     f"{r['mono_pior_banda_db']:.1f} dB | {r['mono_desvio_timbre_db']:.1f} dB |\n")
    print(f"{len(rows)} medições -> {out}")


if __name__ == "__main__":
    main()
