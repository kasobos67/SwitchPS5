#!/usr/bin/env python3
# ProsperoEden - Turns raw generated sound effects into the launcher's cues.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
"""process-sfx.py <raw dir> <out dir>

Each raw file is named <cue>_NN.<ext> (any format ffmpeg reads). The output is a 48 kHz 16-bit
WAV per file: leading silence removed, cut to the cue's length, a clean fade to silence, and a
level by category (the tick of a moving highlight sits well below the welcome chime).
Needs ffmpeg and numpy.
"""

import subprocess
import sys
import wave
from pathlib import Path

import numpy as np

RATE = 48000

# cue -> (max seconds, loudness category, stereo). Categories are RMS targets in dBFS over the
# audible part; the peak never goes above -1 dBFS (-8 for the quiet ticks).
QUIET, UI, CHIME = -33.0, -27.0, -23.0
CUES = {
    "focus": (0.05, QUIET, False), "slider": (0.06, QUIET, False),
    "select": (0.20, UI, False), "back": (0.20, UI, False), "page": (0.22, UI, False),
    "toggle": (0.055, UI, False), "open": (0.45, UI, True), "modal_open": (0.35, UI, False),
    "modal_close": (0.35, UI, False), "error": (0.28, UI, False),
    "saved": (0.80, UI, True), "notify": (0.80, UI, True), "resume": (1.00, CHIME, True),
    "launch": (1.30, CHIME, True), "welcome": (2.20, CHIME, True),
}
# Ticks live in the mids and highs; a thump below 250 Hz is lost on a TV.
TICKS = {"focus", "slider", "toggle"}


def decode(path, channels, highpass):
    # The high-pass removes DC drift and sub-bass rumble that generated clips often start with;
    # it would otherwise fool the trim and the level.
    raw = subprocess.run(["ffmpeg", "-v", "error", "-i", str(path), "-af",
                          f"highpass=f={highpass},highpass=f={highpass}", "-ac", str(channels),
                          "-ar", str(RATE), "-f", "f32le", "-"],
                         capture_output=True, check=True).stdout
    return np.frombuffer(raw, dtype=np.float32).reshape(-1, channels).copy()


def db(x):
    return 20 * np.log10(max(x, 1e-9))


def process(path, out_dir):
    stem = path.stem
    cue = stem.rsplit("_", 1)[0]
    if cue not in CUES:
        print(f"skip {path.name}: unknown cue")
        return
    max_s, rms_db, stereo = CUES[cue]
    highpass = 45 if rms_db == CHIME else 250 if cue in TICKS else 90
    audio = decode(path, 2 if stereo else 1, highpass)
    level = np.abs(audio).max(axis=1)
    peak = level.max()
    if peak <= 0:
        print(f"skip {path.name}: silent")
        return
    # Start at the attack: the first 1 ms window whose energy reaches a quarter (-12 dB) of the
    # loudest window, so noise ahead of the hit is dropped. Chimes and whooshes swell, so they
    # start where the sound first rises out of silence instead.
    hop = RATE // 1000
    frames = len(level) // hop
    rms = np.sqrt((level[:frames * hop].reshape(frames, hop) ** 2).mean(axis=1))
    threshold = 10 ** ((-12 if max_s <= 0.3 else -34) / 20)
    onset = int(np.nonzero(rms >= rms.max() * threshold)[0][0]) * hop
    start = max(0, onset - int(0.003 * RATE))
    # End after the last audible sound, capped at the cue's length.
    audible = np.nonzero(level > peak * 10 ** (-50 / 20))[0]
    end = min(audible[-1] + int(0.02 * RATE), start + int(max_s * RATE), len(audio))
    audio = audio[start:end]
    n = len(audio)
    # A short fade in (no click) and a fade out that reaches digital silence.
    fade_in = min(int(0.002 * RATE), n // 4)
    fade_out = min(n // 2 if n < 0.3 * RATE else int(0.25 * n), int(0.4 * RATE))
    env = np.ones(n, dtype=np.float32)
    if fade_in > 0:
        env[:fade_in] = np.linspace(0, 1, fade_in, dtype=np.float32)
    if fade_out > 0:
        env[n - fade_out:] = np.linspace(1, 0, fade_out, dtype=np.float32) ** 2
    audio *= env[:, None]
    # Loudness by category, measured over the audible part so a long fade does not count as
    # quiet; the peak cap wins when the two disagree.
    mono = np.abs(audio).max(axis=1)
    active = audio[mono > mono.max() * 10 ** (-30 / 20)]
    rms = float(np.sqrt((active ** 2).mean()))
    peak_cap = 10 ** ((-8.0 if rms_db == QUIET else -1.0) / 20)
    gain = min(10 ** (rms_db / 20) / max(rms, 1e-9), peak_cap / max(float(np.abs(audio).max()), 1e-9))
    audio *= gain
    pcm = np.clip(np.round(audio * 32767), -32768, 32767).astype("<i2")
    out = out_dir / f"{stem}.wav"
    with wave.open(str(out), "wb") as w:
        w.setnchannels(pcm.shape[1])
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(pcm.tobytes())
    # The spectral centre tells a tick from a thud at a glance.
    spectrum = np.abs(np.fft.rfft(audio.mean(axis=1)))
    centre = float((spectrum * np.fft.rfftfreq(n, 1 / RATE)).sum() / max(spectrum.sum(), 1e-9))
    print(f"{stem}: {n / RATE * 1000:.0f} ms, peak {db(np.abs(audio).max()):.1f} dBFS, "
          f"centre {centre:.0f} Hz, {'stereo' if stereo else 'mono'}")


def main():
    raw_dir, out_dir = Path(sys.argv[1]), Path(sys.argv[2])
    out_dir.mkdir(parents=True, exist_ok=True)
    for path in sorted(raw_dir.iterdir()):
        if path.is_file():
            process(path, out_dir)


if __name__ == "__main__":
    main()
