#!/usr/bin/env python3
"""Monta o pacote de comparação antes (v0.1) × depois (v0.2).

Entrada: duas pastas geradas pelo pad_scenarios (uma compilada com -DPAD_V1 contra a
tag v0.1.0-motor e outra contra a versão atual).
Saída (pasta destino):
  normalizado/antes/*.wav, normalizado/depois/*.wav  — mesmos cenários com loudness
      igualado em -20 LUFS (teto de true peak -1 dBTP), para comparar de ouvido sem que
      o mais alto pareça "melhor";
  original/antes, original/depois                    — os arquivos como saíram do motor;
  espectrogramas/*.png                               — antes em cima, depois embaixo;
  medicoes.csv e medicoes.md                         — medidas de cada arquivo original.
Uso: make_comparison.py <antes> <depois> <destino>
"""
import csv
import os
import shutil
import sys

import numpy as np
from scipy import signal

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "analysis"))
import audio_metrics as am  # noqa: E402

import matplotlib  # noqa: E402
matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

SILENT_DB = -90.0


def rms_db(path):
    sr, x = am.load(path)
    return 20 * np.log10(np.sqrt(np.mean(x ** 2)) + 1e-12)


def spectro(ax, path, title):
    sr, x = am.load(path)
    m = x.mean(axis=1)
    f, t, S = signal.spectrogram(m, sr, nperseg=4096, noverlap=3072, window="hann")
    SdB = 10 * np.log10(S + 1e-14)
    ax.pcolormesh(t, f, SdB, vmin=SdB.max() - 90, vmax=SdB.max(), shading="auto", cmap="magma")
    ax.set_yscale("symlog", linthresh=200)
    ax.set_ylim(30, 20000)
    ax.set_ylabel("Hz")
    ax.set_title(title, fontsize=10)


def main():
    a_dir, b_dir, out = sys.argv[1:4]
    for sub in ("normalizado/antes", "normalizado/depois", "original/antes", "original/depois", "espectrogramas"):
        os.makedirs(os.path.join(out, sub), exist_ok=True)
    names = sorted(f for f in os.listdir(b_dir) if f.endswith(".wav"))
    rows = []
    for n in names:
        pb = os.path.join(b_dir, n)
        pa = os.path.join(a_dir, n)
        has_a = os.path.exists(pa) and rms_db(pa) > SILENT_DB  # camadas que não existiam na v0.1 saem mudas
        for tag, p in (("antes", pa), ("depois", pb)):
            if tag == "antes" and not has_a:
                continue
            shutil.copy(p, os.path.join(out, "original", tag, n))
            impulse = n.startswith("impulso")
            if not impulse:
                g, lim = am.normalize_to_lufs(p, os.path.join(out, "normalizado", tag, n), -20.0, -1.0)
            else:
                g, lim = 0.0, False
            m = am.measure(p, 1.0 if not impulse else 0.0)
            m.update(versao=tag, ganho_normalizacao_db=g, limitado_pelo_pico=lim)
            rows.append(m)
        if has_a:
            fig, ax = plt.subplots(2, 1, figsize=(12, 7), sharex=True)
            spectro(ax[0], pa, f"ANTES (v0.1) — {n}")
            spectro(ax[1], pb, f"DEPOIS (v0.2) — {n}")
            ax[1].set_xlabel("s")
            fig.tight_layout()
            fig.savefig(os.path.join(out, "espectrogramas", n.replace(".wav", ".png")), dpi=70)
            plt.close(fig)
        elif not n.startswith("impulso"):
            fig, ax = plt.subplots(1, 1, figsize=(12, 4))
            spectro(ax, pb, f"DEPOIS (v0.2, novo) — {n}")
            fig.tight_layout()
            fig.savefig(os.path.join(out, "espectrogramas", n.replace(".wav", ".png")), dpi=70)
            plt.close(fig)

    keys = ["arquivo", "versao", "lufs", "lra", "pico_db", "true_peak_db", "clip", "crista_db", "centroide_hz",
            "acima_10k_db", "ruido_entre_harmonicos_db", "correlacao_lr", "perda_mono_db", "bandas_pct",
            "ganho_normalizacao_db", "limitado_pelo_pico"]
    with open(os.path.join(out, "medicoes.csv"), "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(keys)
        for r in rows:
            w.writerow([(";".join(f"{v:.1f}" for v in r[k]) if isinstance(r[k], list) else
                         (f"{r[k]:.2f}" if isinstance(r[k], float) else r[k])) for k in keys])
    with open(os.path.join(out, "medicoes.md"), "w") as fh:
        fh.write("| arquivo | versão | LUFS | LRA | true peak | clip | crista | centróide | >10 kHz | ruído entre harmônicos | correl. L/R | perda em mono | graves/médios/agudos % |\n")
        fh.write("|---|---|---|---|---|---|---|---|---|---|---|---|---|\n")
        for r in rows:
            b = r["bandas_pct"]
            fh.write(f"| {r['arquivo']} | {r['versao']} | {r['lufs']:.1f} | {r['lra']:.1f} | {r['true_peak_db']:.1f} | {r['clip']} | "
                     f"{r['crista_db']:.1f} | {r['centroide_hz']:.0f} Hz | {r['acima_10k_db']:.1f} dB | {r['ruido_entre_harmonicos_db']:.1f} dB | "
                     f"{r['correlacao_lr']:.2f} | {r['perda_mono_db']:.1f} dB | {b[0]+b[1]:.0f}/{b[2]+b[3]:.0f}/{b[4]+b[5]:.0f} |\n")
    print(f"{len(rows)} medições, {len(names)} cenários -> {out}")


if __name__ == "__main__":
    main()
