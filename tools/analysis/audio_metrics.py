#!/usr/bin/env python3
"""PAD Worship — medições objetivas de arquivos WAV (para comparar versões e reverbs).

Medidas:
  - Loudness integrado (LUFS, ITU-R BS.1770-4: filtro K, blocos de 400 ms, gates -70/-10)
  - Faixa de loudness de curto prazo (LRA simplificado: P95 - P10 dos blocos de 3 s)
  - Pico de amostra e true peak (sobreamostragem 4x)
  - Fator de crista (pico/RMS)
  - Distribuição espectral por bandas (% da energia) e centróide
  - Energia acima de 10 kHz (dB em relação ao total) — indicador de aspereza/chiado
  - Ruído entre harmônicos: razão entre o espectro "fora das linhas" e o total em 1–8 kHz
  - Correlação L/R e perda ao somar em mono
  - Amostras clipadas (|x| >= 0,999)

Uso como módulo: from audio_metrics import measure, normalize_to_lufs
Uso direto:     python3 audio_metrics.py arquivo1.wav [arquivo2.wav ...]
"""
import sys
import numpy as np
from scipy import signal
from scipy.io import wavfile


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


def k_weight(x, sr):
    # Coeficientes do BS.1770 (pré-filtro de shelving + passa-alta RLB), recalculados para sr.
    f0, G, Q = 1681.974450955533, 3.999843853973347, 0.7071752369554196
    K = np.tan(np.pi * f0 / sr)
    Vh = 10 ** (G / 20)
    Vb = Vh ** 0.4996667741545416
    a0 = 1 + K / Q + K * K
    b1 = [(Vh + Vb * K / Q + K * K) / a0, 2 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0]
    a1 = [1, 2 * (K * K - 1) / a0, (1 - K / Q + K * K) / a0]
    f0, Q = 38.13547087602444, 0.5003270373238773
    K = np.tan(np.pi * f0 / sr)
    b2 = [1, -2, 1]
    a2 = [1, 2 * (K * K - 1) / (1 + K / Q + K * K), (1 - K / Q + K * K) / (1 + K / Q + K * K)]
    y = signal.lfilter(b1, a1, x, axis=0)
    return signal.lfilter(b2, a2, y, axis=0)


def block_loudness(x, sr, block_s, hop_s):
    y = k_weight(x, sr)
    n, h = int(block_s * sr), int(hop_s * sr)
    out = []
    for s in range(0, len(y) - n + 1, h):
        ms = np.mean(y[s:s + n] ** 2, axis=0).sum()
        out.append(-0.691 + 10 * np.log10(ms + 1e-20))
    return np.array(out)


def integrated_lufs(x, sr):
    L = block_loudness(x, sr, 0.4, 0.1)
    L = L[L > -70]
    if len(L) == 0:
        return -99.0
    rel = 10 * np.log10(np.mean(10 ** (L / 10))) - 10
    L = L[L > rel]
    return float(10 * np.log10(np.mean(10 ** (L / 10))))


def loudness_range(x, sr):
    L = block_loudness(x, sr, 3.0, 1.0)
    L = L[L > -70]
    if len(L) < 3:
        return 0.0
    rel = 10 * np.log10(np.mean(10 ** (L / 10))) - 20
    L = L[L > rel]
    return float(np.percentile(L, 95) - np.percentile(L, 10))


def true_peak_db(x):
    up = signal.resample_poly(x, 4, 1, axis=0)
    return float(20 * np.log10(np.abs(up).max() + 1e-12))


def spectrum(x, sr):
    m = x.mean(axis=1)
    f, p = signal.welch(m, sr, nperseg=1 << 15, noverlap=(1 << 15) * 3 // 4, window="hann")
    return f, p


def harmonic_noise_ratio_db(x, sr):
    """Energia "entre linhas" na faixa 1–8 kHz. Um pad harmônico tem picos estreitos;
    ruído (ex.: o Air antigo) preenche os vales. Mede-se a razão entre o espectro mínimo
    local (envoltória inferior, janela de 1/6 de oitava) e o espectro total."""
    f, p = spectrum(x, sr)
    band = (f > 1000) & (f < 8000)
    pb = p[band]
    fb = f[band]
    floor = np.empty_like(pb)
    for i, fi in enumerate(fb):
        w = (fb > fi * 2 ** (-1 / 12)) & (fb < fi * 2 ** (1 / 12))
        floor[i] = np.percentile(pb[w], 10)
    return float(10 * np.log10(floor.sum() / (pb.sum() + 1e-30) + 1e-30))


def measure(path, skip_s=0.0):
    sr, x = load(path)
    x = x[int(skip_s * sr):]
    f, p = spectrum(x, sr)
    tot = p.sum() + 1e-30
    bands = [(20, 120), (120, 500), (500, 2000), (2000, 5000), (5000, 10000), (10000, sr / 2)]
    rms = np.sqrt(np.mean(x ** 2))
    pk = np.abs(x).max()
    l, r = x[:, 0], x[:, 1]
    corr = float(np.dot(l, r) / np.sqrt(np.dot(l, l) * np.dot(r, r) + 1e-30))
    mono_loss = float(10 * np.log10(np.mean(((l + r) / 2) ** 2) / (np.mean((l ** 2 + r ** 2) / 2) + 1e-30) + 1e-30))
    return {
        "arquivo": path.split("/")[-1],
        "lufs": integrated_lufs(x, sr),
        "lra": loudness_range(x, sr),
        "pico_db": float(20 * np.log10(pk + 1e-12)),
        "true_peak_db": true_peak_db(x),
        "crista_db": float(20 * np.log10(pk / (rms + 1e-12) + 1e-12)),
        "centroide_hz": float((f * p).sum() / tot),
        "bandas_pct": [float(100 * p[(f >= a) & (f < b)].sum() / tot) for a, b in bands],
        "acima_10k_db": float(10 * np.log10(p[f >= 10000].sum() / tot + 1e-30)),
        "ruido_entre_harmonicos_db": harmonic_noise_ratio_db(x, sr),
        "correlacao_lr": corr,
        "perda_mono_db": mono_loss,
        "clip": int((np.abs(x) >= 0.999).sum()),
    }


def normalize_to_lufs(path_in, path_out, target=-20.0, ceiling_db=-1.0):
    """Normaliza para o loudness alvo (volume percebido igual) sem passar do teto de pico.
    Devolve o ganho aplicado em dB e se o teto limitou o ganho."""
    sr, x = load(path_in)
    g_db = target - integrated_lufs(x, sr)
    tp = true_peak_db(x)
    limited = tp + g_db > ceiling_db
    if limited:
        g_db = ceiling_db - tp
    y = x * 10 ** (g_db / 20)
    q = np.clip(np.round(y * 8388607.0), -8388608, 8388607).astype(np.int32) << 8
    wavfile.write(path_out, sr, q)
    return g_db, limited


if __name__ == "__main__":
    hdr = f"{'arquivo':44s} {'LUFS':>6s} {'LRA':>5s} {'TP':>6s} {'crista':>6s} {'centr':>6s} {'>10k':>6s} {'ruido':>6s} {'corr':>5s}"
    print(hdr)
    for p in sys.argv[1:]:
        m = measure(p)
        print(f"{m['arquivo']:44s} {m['lufs']:6.1f} {m['lra']:5.1f} {m['true_peak_db']:6.1f} {m['crista_db']:6.1f} "
              f"{m['centroide_hz']:6.0f} {m['acima_10k_db']:6.1f} {m['ruido_entre_harmonicos_db']:6.1f} {m['correlacao_lr']:5.2f}")
