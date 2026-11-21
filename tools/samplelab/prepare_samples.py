#!/usr/bin/env python3
"""PAD Worship — preparação offline dos samples reais (Fase C).

Este script NÃO roda no app. Ele transforma gravações de bibliotecas com licença
compatível em loops sustentados prontos para o motor:

  strings          VSCO 2 CE (CC0): naipes de violoncelo, viola e violino, sustain com vibrato
  piano_cathedral  Salamander Grand Piano V3 (CC-BY 3.0) ou VCSL Steinway B (CC0):
                   nota de piano "congelada" no espectro, com evolução lenta e periódica
  organ            VCSL Pipe Organ, manual quieto (CC0): órgão de tubos suave

Saída: assets/samples/<conjunto>/*.flac (48 kHz, 16 bits com dither TPDF, estéreo)
       assets/samples/<conjunto>/manifest.txt
       (uma linha por zona: arquivo, nota MIDI da raiz, frequência medida da raiz em Hz)

Uso:
  python3 tools/samplelab/prepare_samples.py --vsco <VSCO-2-CE> --vcsl <VCSL> \
      --salamander <SalamanderGrandPiano> --out assets/samples [--piano salamander|vcsl]

Requisitos: numpy, scipy, soundfile.
Todas as decisões de processamento estão documentadas nas funções abaixo.
"""
import argparse
import glob
import os
import re
import sys

import numpy as np
import soundfile as sf
from scipy import signal

SR = 48000
NOTE_INDEX = {"C": 0, "C#": 1, "D": 2, "D#": 3, "E": 4, "F": 5, "F#": 6, "G": 7, "G#": 8, "A": 9, "A#": 10, "B": 11}
TARGET_RMS_DB = -20.0
RNG = np.random.default_rng(20261121)


def midi_to_hz(m):
    return 440.0 * 2.0 ** ((m - 69) / 12.0)


def hz_to_midi(f):
    return 69 + 12 * np.log2(f / 440.0)


def parse_note(name):
    m = re.search(r"([A-G]#?)(-?\d)", name)
    if not m:
        raise ValueError(name)
    return NOTE_INDEX[m.group(1)] + 12 * (int(m.group(2)) + 1)  # convenção C4 = 60


def load(path):
    x, sr = sf.read(path, dtype="float64", always_2d=True)
    if x.shape[1] == 1:
        x = np.repeat(x, 2, axis=1)
    x = x[:, :2]
    if sr != SR:
        g = np.gcd(SR, sr)
        x = signal.resample_poly(x, SR // g, sr // g, axis=0, window=("kaiser", 12.0))
    return x


def estimate_f0(mono, expected_midi):
    """Mede a fundamental. Testa a nota esperada e as oitavas vizinhas (as bibliotecas usam
    convenções de oitava diferentes) e escolhe a que melhor explica a série harmônica.
    Refina pela média ponderada das posições dos 6 primeiros harmônicos."""
    n = 1 << 17
    seg = mono[: min(len(mono), n)]
    win = signal.windows.blackmanharris(len(seg))
    spec = np.abs(np.fft.rfft(seg * win, n))
    freqs = np.fft.rfftfreq(n, 1.0 / SR)
    best = None
    for octv in (-12, 0, 12):
        f = midi_to_hz(expected_midi + octv)
        score = 0.0
        for h in range(1, 7):
            band = (freqs > f * h * 0.97) & (freqs < f * h * 1.03)
            if band.any():
                score += np.log10(spec[band].max() + 1e-12) / h
        # penaliza candidatos cuja "fundamental" é mais fraca que a metade dela (erro de oitava)
        band_half = (freqs > f * 0.485) & (freqs < f * 0.515)
        if band_half.any() and f / 2 > 30:
            score -= 0.15 * max(0.0, np.log10(spec[band_half].max() + 1e-12) - np.log10(
                spec[(freqs > f * 0.97) & (freqs < f * 1.03)].max() + 1e-12))
        if best is None or score > best[0]:
            best = (score, f)
    f = best[1]
    ests, ws = [], []
    for h in range(1, 7):
        band = np.where((freqs > f * h * 0.97) & (freqs < f * h * 1.03))[0]
        if len(band) < 3:
            continue
        k = band[np.argmax(spec[band])]
        a, b, c = np.log(spec[k - 1] + 1e-12), np.log(spec[k] + 1e-12), np.log(spec[k + 1] + 1e-12)
        off = 0.5 * (a - c) / (a - 2 * b + c)
        ests.append((k + off) * SR / n / h)
        ws.append(spec[k] / h)
    return float(np.average(ests, weights=ws))


def envelope_db(mono, win=0.05):
    w = int(win * SR)
    n = len(mono) // w
    e = np.sqrt(np.mean(mono[: n * w].reshape(n, w) ** 2, axis=1) + 1e-20)
    return 20 * np.log10(e), w


def sustain_region(x, attack_skip=0.35, release_margin=0.25):
    """Trecho estável: depois do ataque e antes do release (envelope dentro de 4 dB da mediana)."""
    mono = x.mean(axis=1)
    env, w = envelope_db(mono)
    ref = np.median(env[env > env.max() - 20])
    ok = np.where(env > ref - 4.0)[0]
    start = max(ok[0] * w + int(attack_skip * SR), int(0.5 * SR))
    end = ok[-1] * w - int(release_margin * SR)
    return start, end


def flatten_envelope(x, amount=0.6, win=1.2):
    """Reduz (sem eliminar) as oscilações lentas de volume do trecho sustentado.
    Evita que o loop "respire" sempre no mesmo ponto; o movimento natural fica por conta do motor."""
    mono = np.abs(x).mean(axis=1)
    w = int(win * SR)
    k = signal.windows.hann(w)
    k /= k.sum()
    env = np.sqrt(np.convolve(mono ** 2, k, mode="same") + 1e-12)
    env /= np.median(env)
    g = env ** (-amount)
    return x * g[:, None]


def seamless_loop(x, xfade):
    """Loop sem emenda: o trecho após o fim do loop é cruzado com o começo usando a lei
    de crossfade dependente da correlação (a mesma do motor, engine/src/dsp/Crossfade.h)."""
    L = len(x) - xfade
    a = x[:xfade].mean(axis=1)
    b = x[L:L + xfade].mean(axis=1)
    rho = float(np.dot(a, b) / np.sqrt(np.dot(a, a) * np.dot(b, b) + 1e-20))
    rho = min(max(rho, -0.5), 1.0)
    t = (np.arange(xfade) + 0.5) / xfade
    gi = np.sin(t * np.pi / 2)
    go = np.cos(t * np.pi / 2)
    p = gi ** 2 + go ** 2 + 2 * rho * gi * go
    n = 1.0 / np.sqrt(np.maximum(p, 0.5))
    out = x[:L].copy()
    out[:xfade] = x[L:L + xfade] * (go * n)[:, None] + x[:xfade] * (gi * n)[:, None]
    return out, rho


def highpass(x, fc, order=2):
    """Remove ruído grave (ar-condicionado, ressonâncias do prédio) abaixo da fundamental."""
    sos = signal.butter(order, fc / (SR / 2), btype="highpass", output="sos")
    return signal.sosfiltfilt(sos, x, axis=0)


def smear_cents(mag, cents=14.0, passes=2):
    """Alarga cada parcial em largura constante em cents (média móvel de largura proporcional
    à frequência, aplicada 2x ~ gaussiana). Transforma linhas espectrais "perfeitas" em faixas
    estreitas: o piano congelado deixa de soar como órgão e ganha a "floração" de um ensemble."""
    c = 2 ** (cents / 1200.0) - 1.0
    p = mag.astype(np.float64) ** 2
    k = np.arange(len(p))
    half = np.maximum(1, np.round(k * c / 2)).astype(int)
    for _ in range(passes):
        cs = np.concatenate([[0.0], np.cumsum(p)])
        lo = np.clip(k - half, 0, len(p) - 1)
        hi = np.clip(k + half + 1, 1, len(p))
        p = (cs[hi] - cs[lo]) / (hi - lo)
    return np.sqrt(p)


def normalize_rms(x, target_db=TARGET_RMS_DB):
    rms = np.sqrt(np.mean(x ** 2))
    return x * (10 ** (target_db / 20) / rms)


def write_flac16(path, x):
    """16 bits com dither TPDF. Nível RMS de -20 dBFS deixa o ruído de quantização ~96 dB abaixo do pico."""
    peak = np.abs(x).max()
    if peak > 0.98:
        x = x * (0.98 / peak)
    d = (RNG.random(x.shape) - RNG.random(x.shape)) / 32768.0
    q = np.clip(np.round((x + d) * 32767.0), -32768, 32767).astype(np.int16)
    sf.write(path, q, SR, subtype="PCM_16", format="FLAC")


def report(name, x, f0, midi):
    mono = x.mean(axis=1)
    cents = 1200 * np.log2(f0 / midi_to_hz(round(hz_to_midi(f0))))
    print(f"  {name:38s} raiz {round(hz_to_midi(f0)):3d} ({cents:+5.1f} cents)  {len(x) / SR:5.2f} s  "
          f"pico {20 * np.log10(np.abs(x).max()):6.1f} dBFS")


# --------------------------------------------------------------------------- cordas

def prepare_strings(vsco, out):
    """Naipes do VSCO 2 CE por registro (sem sobreposição): violoncelo no grave,
    viola no médio, violino no agudo. Usa a dinâmica mais suave (v1), mais quente para pads."""
    sets = [
        ("Strings/Cello Section/susvib", r"susvib_([A-G]#?\d)_v1_1\.wav", 33, 54),
        ("Strings/Viola Section/susvib", r"ViolaEns_susvib_([A-G]#?\d)_v1_1\.wav", 55, 64),
        ("Strings/Violin Section/susVib", r"VlnEns_susVib_([A-G]#?\d)_v1\.wav", 65, 96),
    ]
    os.makedirs(out, exist_ok=True)
    lines = []
    for folder, pat, lo, hi in sets:
        for path in sorted(glob.glob(os.path.join(vsco, folder, "*.wav"))):
            m = re.search(pat, os.path.basename(path))
            if not m:
                continue
            x = load(path)
            mono = x.mean(axis=1)
            s, e = sustain_region(x)
            f0 = estimate_f0(mono[s:e], parse_note(m.group(1)))
            root = int(round(hz_to_midi(f0)))
            if root < lo or root > hi:
                continue
            seg = highpass(flatten_envelope(x[s:e], amount=0.6), max(30.0, 0.5 * f0))
            xf = int(min(1.0, 0.2 * (e - s) / SR) * SR)
            loop, rho = seamless_loop(seg, xf)
            loop = normalize_rms(loop)
            fn = f"str_{root:03d}.flac"
            write_flac16(os.path.join(out, fn), loop)
            report(os.path.basename(path), loop, f0, root)
            lines.append(f"{fn} {root} {f0:.4f}")
    with open(os.path.join(out, "manifest.txt"), "w") as f:
        f.write("# arquivo nota_midi_raiz frequencia_raiz_hz (VSCO 2 CE, CC0)\n" + "\n".join(lines) + "\n")


# --------------------------------------------------------------------------- piano congelado

def spectral_freeze(x, f0, loop_seconds=10.0, n=1 << 13, t_a=(0.35, 1.2), t_b=(1.2, 2.8)):
    """Piano "Cathedral": congela o espectro da nota do piano (sem o ataque do martelo) e o
    ressintetiza com fases aleatórias em quadros sobrepostos (a ideia do PaulStretch).
    O espectro alterna lentamente entre um retrato do início da nota (mais brilhante) e
    um da cauda (mais escuro) num ciclo periódico do tamanho do loop, então o loop evolui
    e mesmo assim fecha sem emenda."""
    hop = n // 4
    win = signal.windows.hann(n, sym=False)

    def avg_mag(t0, t1, ch):
        mags = []
        for i in range(int(t0 * SR), int(t1 * SR) - n, hop):
            mags.append(np.abs(np.fft.rfft(x[i:i + n, ch] * win)))
        return np.mean(mags, axis=0)

    out_len = int(loop_seconds * SR)
    xf = int(1.0 * SR)
    total = out_len + xf + n
    y = np.zeros((total, 2))
    wsum = np.zeros(total)
    for ch in range(2):
        ma, mb = smear_cents(avg_mag(*t_a, ch)), smear_cents(avg_mag(*t_b, ch))
        for k, pos in enumerate(range(0, total - n, hop)):
            ph = 0.5 - 0.5 * np.cos(2 * np.pi * (pos % out_len) / out_len)
            mag = (1 - ph) * ma + ph * mb
            phase = RNG.uniform(0, 2 * np.pi, len(mag))
            frame = np.fft.irfft(mag * np.exp(1j * phase), n) * win
            y[pos:pos + n, ch] += frame
            if ch == 0:
                wsum[pos:pos + n] += win ** 2
    y = y[n // 2: n // 2 + out_len + xf] / np.maximum(wsum[n // 2: n // 2 + out_len + xf], 1e-6)[:, None]
    # pequeno passa-baixa suave: retira o brilho metálico residual do martelo
    b, a = signal.butter(2, min(9000.0, f0 * 24) / (SR / 2))
    y = signal.filtfilt(b, a, y, axis=0)
    y = highpass(y, max(35.0, 0.7 * f0))
    loop, _ = seamless_loop(y, xf)
    return loop


def prepare_piano(source, path_root, out):
    os.makedirs(out, exist_ok=True)
    lines = []
    if source == "salamander":
        files = sorted(glob.glob(os.path.join(path_root, "Samples", "*v8.flac")))
        credit = "Salamander Grand Piano V3 por Alexander Holm (CC-BY 3.0)"
    else:
        files = sorted(glob.glob(os.path.join(path_root, "Chordophones/Zithers/Grand Piano, Steinway B/Sus", "*_vl3_rr1.wav")))
        credit = "VCSL, Grand Piano Steinway B (CC0)"
    for path in files:
        m = re.search(r"([A-G]#?\d)", os.path.basename(path).split("_Close_")[-1])
        expected = parse_note(m.group(1))
        if expected < 41 or expected > 90:   # faixa usada pelos voicings (com margem)
            continue
        x = load(path)
        f0 = estimate_f0(x.mean(axis=1)[int(0.3 * SR):int(2.0 * SR)], expected)
        root = int(round(hz_to_midi(f0)))
        loop = normalize_rms(spectral_freeze(x, f0))
        fn = f"pno_{root:03d}.flac"
        write_flac16(os.path.join(out, fn), loop)
        report(os.path.basename(path), loop, f0, root)
        lines.append(f"{fn} {root} {f0:.4f}")
    with open(os.path.join(out, "manifest.txt"), "w") as f:
        f.write(f"# arquivo nota_midi_raiz frequencia_raiz_hz ({credit}; processado pelo PAD Worship)\n" + "\n".join(lines) + "\n")


# --------------------------------------------------------------------------- órgão

def prepare_organ(vcsl, out):
    os.makedirs(out, exist_ok=True)
    lines = []
    folder = os.path.join(vcsl, "Aerophones/Edge-blown Aerophones/Pipe Organ/Quiet")
    for path in sorted(glob.glob(os.path.join(folder, "*.wav"))):
        m = re.search(r"Quiet_([A-G]#?\d)_", os.path.basename(path))
        expected = parse_note(m.group(1))
        x = load(path)
        s, e = sustain_region(x, attack_skip=0.4, release_margin=0.3)
        f0 = estimate_f0(x.mean(axis=1)[s:e], expected)
        root = int(round(hz_to_midi(f0)))
        if root < 41 or root > 90:
            continue
        seg = highpass(flatten_envelope(x[s:e], amount=0.8), max(30.0, 0.5 * f0))
        xf = int(min(0.8, 0.2 * (e - s) / SR) * SR)
        loop, _ = seamless_loop(seg, xf)
        loop = normalize_rms(loop)
        fn = f"org_{root:03d}.flac"
        write_flac16(os.path.join(out, fn), loop)
        report(os.path.basename(path), loop, f0, root)
        lines.append(f"{fn} {root} {f0:.4f}")
    with open(os.path.join(out, "manifest.txt"), "w") as f:
        f.write("# arquivo nota_midi_raiz frequencia_raiz_hz (VCSL Pipe Organ, manual quieto, CC0)\n" + "\n".join(lines) + "\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--vsco")
    ap.add_argument("--vcsl")
    ap.add_argument("--salamander")
    ap.add_argument("--piano", choices=["salamander", "vcsl"], default="salamander")
    ap.add_argument("--out", required=True)
    ap.add_argument("--only", nargs="*", default=["strings", "piano", "organ"])
    a = ap.parse_args()
    if "strings" in a.only:
        print("Cordas (VSCO 2 CE)")
        prepare_strings(a.vsco, os.path.join(a.out, "strings"))
    if "piano" in a.only:
        print(f"Piano congelado ({a.piano})")
        root = a.salamander if a.piano == "salamander" else a.vcsl
        prepare_piano(a.piano, root, os.path.join(a.out, "piano_cathedral"))
    if "organ" in a.only:
        print("Órgão suave (VCSL)")
        prepare_organ(a.vcsl, os.path.join(a.out, "organ"))


if __name__ == "__main__":
    sys.exit(main())
