#!/usr/bin/env python3
"""Gera espectrograma (PNG) e métricas de um WAV do motor PAD.
Uso: python3 spectrogram.py arquivo.wav [saida.png]
Requer numpy, scipy e matplotlib (apenas para análise fora do app)."""
import sys
import numpy as np
from scipy.io import wavfile
from scipy import signal
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

def load(path):
    sr, x = wavfile.read(path)
    if x.dtype == np.int32:
        x = x / 2147483648.0
    elif x.dtype == np.int16:
        x = x / 32768.0
    x = np.asarray(x, dtype=np.float64)
    if x.ndim == 1:
        x = np.stack([x, x], axis=1)
    return sr, x

def main():
    path = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else path.rsplit(".", 1)[0] + ".png"
    sr, x = load(path)
    mono = x.mean(axis=1)
    f, t, S = signal.spectrogram(mono, sr, nperseg=4096, noverlap=3072, window="hann")
    SdB = 10 * np.log10(S + 1e-14)
    win = int(0.05 * sr)
    n = len(mono) // win
    rms = 20 * np.log10(np.sqrt((mono[: n * win].reshape(n, win) ** 2).mean(axis=1)) + 1e-9)
    corr = np.corrcoef(x[:, 0], x[:, 1])[0, 1]
    fig, ax = plt.subplots(2, 1, figsize=(12, 7), gridspec_kw={"height_ratios": [3, 1]})
    ax[0].pcolormesh(t, f, SdB, vmin=SdB.max() - 100, vmax=SdB.max(), shading="auto", cmap="magma")
    ax[0].set_yscale("symlog", linthresh=200)
    ax[0].set_ylim(30, sr / 2)
    ax[0].set_ylabel("Hz")
    ax[0].set_title(f"{path}  pico {20*np.log10(np.abs(x).max()+1e-12):.1f} dBFS  correlacao L/R {corr:.2f}")
    ax[1].plot(np.arange(n) * 0.05, rms)
    ax[1].set_ylabel("RMS dB (50 ms)")
    ax[1].set_xlabel("s")
    ax[1].grid(alpha=0.3)
    fig.tight_layout()
    fig.savefig(out, dpi=80)
    print(out)

if __name__ == "__main__":
    main()
