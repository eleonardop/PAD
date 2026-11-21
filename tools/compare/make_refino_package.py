#!/usr/bin/env python3
"""Pacote de escuta da etapa de refinamento (v0.4).

Entrada: a pasta gerada pelo pad_scenarios_refino. Saída (pasta destino):
  originais/*.wav             — arquivos como o motor gerou (sem normalização), preservados.
  estereo/*.wav               — loudness igualado em -20 LUFS integrado, true peak ≤ -1 dBTP
                                (ganho único por arquivo: timbre e dinâmica interna não mudam).
  mono/*.wav                  — soma (L+R)/2 dos arquivos normalizados, SEM renormalizar.
  loudness.md / loudness.csv  — LUFS e true peak antes e depois da normalização, ganho aplicado,
                                LUFS em mono, perda em mono e pior banda de 1/3 de oitava.
Uso: make_refino_package.py <pasta_cenarios> <destino>
"""
import csv
import os
import shutil
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "analysis"))
import audio_metrics as am  # noqa: E402
from make_comparison_v3 import mono_metrics, write24  # noqa: E402


def process_one(args):
    n, src, out = args
    p = os.path.join(src, n)
    shutil.copy2(p, os.path.join(out, "originais", n))
    sr, x = am.load(p)
    before = am.measure(p, 1.0)
    pn = os.path.join(out, "estereo", n)
    g, lim = am.normalize_to_lufs(p, pn, -20.0, -1.0)
    after = am.measure(pn, 1.0)
    y = x * 10 ** (g / 20)
    mm = 0.5 * (y[:, 0] + y[:, 1])
    pm = os.path.join(out, "mono", n)
    write24(pm, sr, np.stack([mm, mm], axis=1))
    mono = am.measure(pm, 1.0)
    loss, worst, _ = mono_metrics(x[int(sr):], sr)
    print(n, flush=True)
    return dict(arquivo=n, lufs_antes=before["lufs"], tp_antes=before["true_peak_db"], ganho_db=g,
                limitado_pelo_pico=lim, lufs_depois=after["lufs"], tp_depois=after["true_peak_db"],
                lufs_mono=mono["lufs"], perda_mono_db=loss, pior_banda_mono_db=worst,
                centroide_hz=before["centroide_hz"], lra=before["lra"])


def main():
    import multiprocessing as mp
    src, out = sys.argv[1:3]
    for sub in ("originais", "estereo", "mono"):
        os.makedirs(os.path.join(out, sub), exist_ok=True)
    names = sorted(f for f in os.listdir(src) if f.endswith(".wav"))
    with mp.get_context("spawn").Pool(processes=1, maxtasksperchild=1) as pool:
        rows = list(pool.imap(process_one, [(n, src, out) for n in names]))
    keys = list(rows[0].keys())
    with open(os.path.join(out, "loudness.csv"), "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(keys)
        for r in rows:
            w.writerow([f"{r[k]:.2f}" if isinstance(r[k], float) else r[k] for k in keys])
    with open(os.path.join(out, "loudness.md"), "w") as fh:
        fh.write("Loudness antes (arquivo original do motor) e depois da normalização (-20 LUFS, TP ≤ -1 dBTP).\n")
        fh.write("Mono = (L+R)/2 do arquivo normalizado, sem renormalizar.\n\n")
        fh.write("| arquivo | LUFS antes | TP antes | ganho | LUFS depois | TP depois | LUFS mono | perda mono | pior banda mono | LRA | centróide |\n")
        fh.write("|---|---|---|---|---|---|---|---|---|---|---|\n")
        for r in rows:
            fh.write(f"| {r['arquivo']} | {r['lufs_antes']:.1f} | {r['tp_antes']:.1f} | {r['ganho_db']:+.1f} dB"
                     f"{' (pico)' if r['limitado_pelo_pico'] else ''} | {r['lufs_depois']:.1f} | {r['tp_depois']:.1f} | "
                     f"{r['lufs_mono']:.1f} | {r['perda_mono_db']:.1f} dB | {r['pior_banda_mono_db']:.1f} dB | "
                     f"{r['lra']:.1f} | {r['centroide_hz']:.0f} Hz |\n")
    print(f"{len(rows)} arquivos -> {out}")


if __name__ == "__main__":
    main()
