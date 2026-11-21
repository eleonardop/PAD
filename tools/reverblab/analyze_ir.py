#!/usr/bin/env python3
"""Análise objetiva das respostas ao impulso geradas pelo pad_reverblab.

Medidas (por arquivo):
  - tempo de mistura: quando a densidade de ecos normalizada (NED, Abel & Huang 2006,
    janela de 20 ms) atinge 0,9 — antes disso a cauda soa granulada/"metálica";
  - RT60 por banda de oitava (250 Hz, 1 kHz, 4 kHz), integração de Schroeder (T20);
  - coloração modal: desvio padrão (dB) do espectro fino da cauda em torno da média em
    1/3 de oitava, 200 Hz–4 kHz. Ruído gaussiano ideal dá ≈5,6 dB; acima disso há picos
    (ressonâncias que "cantam" em certas notas);
  - pior pico modal acima da média local (dB);
  - IACC da cauda (correlação cruzada máxima L/R em ±1 ms): menor = mais largo;
  - fração de energia acima de 6 kHz no primeiro segundo (aspereza).
Uso: analyze_ir.py <pasta> [saida.csv]
"""
import glob
import math
import os
import sys

import numpy as np
import soundfile as sf
from scipy.signal import butter, sosfiltfilt
from scipy.special import erfc


def ned(x, sr, win=0.02):
    n = int(win * sr)
    out = []
    for s in range(0, len(x) - n, n // 2):
        seg = x[s:s + n]
        sd = seg.std() + 1e-20
        out.append((np.abs(seg) > sd).mean() / erfc(1 / math.sqrt(2)))
    return np.array(out), n // 2 / sr


def rt60_band(x, sr, fc):
    sos = butter(4, [fc / math.sqrt(2), fc * math.sqrt(2)], btype="band", fs=sr, output="sos")
    y = sosfiltfilt(sos, x)
    e = np.cumsum((y ** 2)[::-1])[::-1]
    e_db = 10 * np.log10(e / e[0] + 1e-30)
    i5 = np.argmax(e_db <= -5)
    i25 = np.argmax(e_db <= -25)
    if i25 <= i5:
        return float("nan")
    t = np.arange(i5, i25) / sr
    slope = np.polyfit(t, e_db[i5:i25], 1)[0]
    return -60.0 / slope


def coloration(x, sr):
    seg = x[int(0.3 * sr):int(1.3 * sr)]
    seg = seg * np.hanning(len(seg))
    spec = 20 * np.log10(np.abs(np.fft.rfft(seg)) + 1e-20)
    f = np.fft.rfftfreq(len(seg), 1 / sr)
    dev = []
    for k in np.where((f > 200) & (f < 4000))[0]:
        lo, hi = f[k] / 2 ** (1 / 6), f[k] * 2 ** (1 / 6)
        band = spec[(f >= lo) & (f <= hi)]
        dev.append(spec[k] - 10 * np.log10(np.mean(10 ** (band / 10))))
    dev = np.array(dev)
    return float(dev.std()), float(dev.max())


def iacc(l, r, sr):
    a = l[int(0.1 * sr):int(1.0 * sr)]
    b = r[int(0.1 * sr):int(1.0 * sr)]
    m = int(0.001 * sr)
    den = math.sqrt((a ** 2).sum() * (b ** 2).sum()) + 1e-30
    return max(abs(np.dot(a[m:-m], b[m + k:len(b) - m + k])) for k in range(-m, m + 1)) / den


def analyze(path):
    x, sr = sf.read(path)
    l, r = x[:, 0], x[:, 1]
    m = 0.5 * (l + r)
    d, hop = ned(m, sr)
    above = np.where(d >= 0.9)[0]
    mix_ms = 1000 * above[0] * hop if len(above) else float("nan")
    rts = [rt60_band(m, sr, fc) for fc in (250, 1000, 4000)]
    col_sd, col_pk = coloration(m, sr)
    first = m[: int(sr)]
    F = np.abs(np.fft.rfft(first)) ** 2
    f = np.fft.rfftfreq(len(first), 1 / sr)
    hf = 10 * math.log10(F[f > 6000].sum() / F.sum())
    return dict(arquivo=os.path.basename(path), mistura_ms=mix_ms, rt250=rts[0], rt1k=rts[1], rt4k=rts[2],
                coloracao_db=col_sd, pico_modal_db=col_pk, iacc=iacc(l, r, sr), acima6k_db=hf)


def main():
    folder = sys.argv[1]
    rows = [analyze(p) for p in sorted(glob.glob(os.path.join(folder, "ir_*.wav")))]
    keys = list(rows[0].keys())
    print(" | ".join(keys))
    for row in rows:
        print(" | ".join(f"{row[k]:.2f}" if isinstance(row[k], float) else row[k] for k in keys))
    if len(sys.argv) > 2:
        with open(sys.argv[2], "w") as fh:
            fh.write(",".join(keys) + "\n")
            for row in rows:
                fh.write(",".join(f"{row[k]:.3f}" if isinstance(row[k], float) else row[k] for k in keys) + "\n")


if __name__ == "__main__":
    main()
