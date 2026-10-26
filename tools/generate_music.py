"""Compose Midnight Circuit's original, loopable synthwave track.

Run with Python 3 and NumPy. Writes 16-bit mono PCM for libnds and a WAV preview.
No third-party samples or game audio are used.
"""
from __future__ import annotations

from pathlib import Path
import wave

import numpy as np


ROOT = Path(__file__).resolve().parents[1]
RATE = 16000
BPM = 116
BEAT = 60.0 / BPM
BARS = 16
SAMPLES = round(BARS * 4 * BEAT * RATE)
MIX = np.zeros(SAMPLES, dtype=np.float64)
RNG = np.random.default_rng(1985)


def hz(midi: float) -> float:
    return 440.0 * 2.0 ** ((midi - 69) / 12.0)


def add(start: float, signal: np.ndarray, level: float = 1.0) -> None:
    offset = round(start * RATE)
    if offset >= SAMPLES:
        return
    count = min(len(signal), SAMPLES - offset)
    MIX[offset : offset + count] += signal[:count] * level


def envelope(seconds: float, attack: float, release: float) -> np.ndarray:
    count = round(seconds * RATE)
    t = np.arange(count) / RATE
    return np.minimum(1.0, t / max(attack, 1 / RATE)) * np.minimum(
        1.0, (seconds - t) / max(release, 1 / RATE)
    )


def pad(midi: int, duration: float) -> np.ndarray:
    t = np.arange(round(duration * RATE)) / RATE
    f = hz(midi)
    wave_a = np.sin(2 * np.pi * f * t)
    wave_b = np.sin(2 * np.pi * f * 1.004 * t + 0.7)
    body = wave_a + 0.62 * wave_b + 0.18 * np.sin(2 * np.pi * f * 2 * t)
    tremolo = 0.86 + 0.14 * np.sin(2 * np.pi * 0.42 * t)
    return body * tremolo * envelope(duration, 0.24, 0.25)


def bass(midi: int, duration: float) -> np.ndarray:
    t = np.arange(round(duration * RATE)) / RATE
    f = hz(midi)
    body = np.sin(2 * np.pi * f * t) + 0.34 * np.sin(2 * np.pi * f * 2 * t)
    return np.tanh(body * 1.35) * envelope(duration, 0.008, 0.11)


def lead(midi: int, duration: float) -> np.ndarray:
    t = np.arange(round(duration * RATE)) / RATE
    f = hz(midi)
    phase = 2 * np.pi * f * t + 0.10 * np.sin(2 * np.pi * 5.2 * t)
    body = np.sin(phase) + 0.31 * np.sin(2 * phase) + 0.11 * np.sin(3 * phase)
    return body * envelope(duration, 0.016, 0.10)


def pluck(midi: int, duration: float) -> np.ndarray:
    t = np.arange(round(duration * RATE)) / RATE
    f = hz(midi)
    body = np.sin(2 * np.pi * f * t) + 0.29 * np.sin(2 * np.pi * f * 2 * t)
    return body * np.exp(-9 * t) * envelope(duration, 0.004, 0.03)


def kick() -> np.ndarray:
    t = np.arange(round(0.23 * RATE)) / RATE
    phase = 2 * np.pi * (52 * t + 65 * (1 - np.exp(-23 * t)) / 23)
    return np.sin(phase) * np.exp(-18 * t)


def snare() -> np.ndarray:
    t = np.arange(round(0.17 * RATE)) / RATE
    noise = RNG.standard_normal(len(t))
    crisp = noise - np.convolve(noise, np.ones(13) / 13, mode="same")
    return (0.7 * crisp + 0.25 * np.sin(2 * np.pi * 182 * t)) * np.exp(-25 * t)


def hat() -> np.ndarray:
    t = np.arange(round(0.075 * RATE)) / RATE
    noise = RNG.standard_normal(len(t))
    crisp = noise - np.convolve(noise, np.ones(5) / 5, mode="same")
    return crisp * np.exp(-64 * t)


# A minor, F major, C major, G major; a Dm/E turnaround in the middle.
chords = [
    (45, 48, 52), (41, 45, 48), (48, 52, 55), (43, 47, 50),
    (45, 48, 52), (41, 45, 48), (48, 52, 55), (43, 47, 50),
    (50, 53, 57), (45, 48, 52), (41, 45, 48), (40, 44, 47),
    (45, 48, 52), (41, 45, 48), (48, 52, 55), (43, 47, 50),
]
hooks = [
    [69, 72, 76, 72, 69, 67, 69, 72],
    [69, 72, 77, 72, 69, 65, 69, 72],
    [67, 72, 76, 79, 76, 72, 67, 72],
    [67, 71, 74, 79, 74, 71, 67, 64],
    [72, 76, 81, 76, 72, 69, 72, 76],
    [72, 77, 81, 77, 72, 69, 72, 77],
    [72, 76, 79, 76, 72, 67, 72, 76],
    [71, 74, 79, 74, 71, 67, 71, 74],
    [74, 77, 81, 77, 74, 69, 74, 77],
    [72, 76, 81, 76, 72, 69, 72, 76],
    [69, 72, 77, 72, 69, 65, 69, 72],
    [68, 71, 76, 71, 68, 64, 68, 71],
    [69, 72, 76, 81, 76, 72, 69, 72],
    [69, 72, 77, 81, 77, 72, 69, 72],
    [67, 72, 76, 79, 76, 72, 67, 72],
    [67, 71, 74, 79, 74, 71, 67, 64],
]

drum_kick, drum_snare, drum_hat = kick(), snare(), hat()
for bar in range(BARS):
    t0 = bar * 4 * BEAT
    chord = chords[bar]
    # Pads hold the harmony, with a slightly wider top voice.
    for voice, note in enumerate(chord):
        add(t0, pad(note + 12, 4 * BEAT), 0.047 if voice < 2 else 0.038)
    for beat in range(4):
        t = t0 + beat * BEAT
        add(t, drum_kick, 0.36)
        if beat in (1, 3):
            add(t, drum_snare, 0.075)
        add(t + 0.5 * BEAT, drum_hat, 0.035)
        add(t, bass(chord[0] - 12 if beat != 2 else chord[2] - 12, 0.38 * BEAT), 0.24)
        add(t + 0.5 * BEAT, bass(chord[0] - 12, 0.35 * BEAT), 0.14)
    for eighth, note in enumerate(hooks[bar]):
        if bar < 4 and eighth % 2:
            continue  # first phrase leaves room for the city ambience
        add(t0 + eighth * 0.5 * BEAT, lead(note, 0.46 * BEAT), 0.105)
    for sixteenth in range(16):
        note = chord[(sixteenth + bar) % 3] + (24 if sixteenth % 4 else 12)
        add(t0 + sixteenth * 0.25 * BEAT, pluck(note, 0.20 * BEAT), 0.043)

# Subtle mono delay gives the lead depth without obscuring the drums.
delay = round(0.30 * RATE)
MIX[delay:] += MIX[:-delay] * 0.065
MIX = np.tanh(MIX * 1.45)
MIX *= 0.73 / max(1.0, np.max(np.abs(MIX)))
fade = round(0.01 * RATE)
MIX[:fade] *= np.linspace(0, 1, fade)
MIX[-fade:] *= np.linspace(1, 0, fade)
pcm = np.round(MIX * 32767).astype("<i2")
if len(pcm) % 2:
    pcm = np.pad(pcm, (0, 1))

data = ROOT / "data"
data.mkdir(exist_ok=True)
(data / "song.bin").write_bytes(pcm.tobytes())
with wave.open(str(ROOT / "soundtrack-preview.wav"), "wb") as wav:
    wav.setnchannels(1)
    wav.setsampwidth(2)
    wav.setframerate(RATE)
    wav.writeframes(pcm.tobytes())
print(f"Wrote {len(pcm) / RATE:.2f}s of original synthwave music")
