"""Render the original two-contact slot hologram reset one-shot at 48 kHz."""

import argparse
import hashlib
import json
import math
from pathlib import Path
import wave

import numpy as np


RATE = 48_000
DURATION = 0.480
OFF = 0.158
ON = 0.327
TAU = math.tau


def times(seconds):
    return np.arange(round(RATE * seconds), dtype=np.float64) / RATE


def lowpass(signal, cutoff):
    weight = -math.expm1(-TAU * cutoff / RATE)
    filtered = np.empty_like(signal)
    state = 0.0
    for index, sample in enumerate(signal):
        state += weight * (sample - state)
        filtered[index] = state
    return filtered


def envelope(t, attack, decay, tail):
    onset = np.sin(np.pi / 2 * np.minimum(t / attack, 1.0)) ** 2
    release = np.sin(np.pi / 2 * np.minimum((t[-1] - t) / tail, 1.0)) ** 2
    return onset * np.exp(-t / decay) * release


def falling_phase(t, bottom, top, decay):
    return TAU * (bottom * t + (top - bottom) * decay * -np.expm1(-t / decay))


def relay(rng, bright=False):
    t = times(0.047)
    white = rng.standard_normal(len(t))
    contact = lowpass(white, 3950 if bright else 2700) - lowpass(white, 520)
    snap = envelope(t, 0.0007, 0.0021 if bright else 0.0037, 0.009)
    ring_freq = 1630 if bright else 1180
    ring = np.sin(TAU * ring_freq * t + 0.45) + 0.38 * np.sin(TAU * ring_freq * 1.72 * t)
    ring *= envelope(t, 0.00045, 0.0048 if bright else 0.0068, 0.009)
    return (0.67 if bright else 0.84) * contact * snap + 0.45 * ring


def add(destination, onset, voice, gain=1.0):
    start = round(RATE * onset)
    end = min(start + len(voice), len(destination))
    destination[start:end] += voice[: end - start] * gain


def render():
    rng = np.random.default_rng(84377)
    mono = np.zeros(round(DURATION * RATE))
    # Mechanical contact, a quieter relay rebound, then a brief cabinet thump.
    add(mono, OFF, relay(rng))
    add(mono, OFF + 0.012, relay(rng), 0.26)
    t = times(0.112)
    cabinet = np.sin(falling_phase(t, 59, 129, 0.022))
    cabinet += 0.29 * np.sin(falling_phase(t, 294, 492, 0.009) + 0.18)
    add(mono, OFF + 0.001, cabinet * envelope(t, 0.0025, 0.022, 0.021), 0.57)

    # The smaller boot click pulls in a rising sync tone and a muted supply hum.
    add(mono, ON, relay(rng, bright=True), 0.74)
    t = times(0.136)
    sync_phase = falling_phase(t, 708, 305, 0.025)
    sync = np.sin(sync_phase) + 0.23 * np.sin(sync_phase * 2.07 + 0.13)
    hiss = lowpass(rng.standard_normal(len(t)), 3100) - lowpass(rng.standard_normal(len(t)), 1150)
    add(mono, ON + 0.005, (sync * 0.38 + 0.13 * hiss) * envelope(t, 0.009, 0.028, 0.030))
    hum = np.sin(TAU * 119 * t) + 0.36 * np.sin(TAU * 239 * t + 0.57)
    add(mono, ON + 0.004, hum * envelope(t, 0.0045, 0.036, 0.024), 0.22)
    mono -= lowpass(mono, 32)
    mono[-96:] *= np.linspace(1.0, 0.0, 96)
    mono *= 10 ** (-11.5 / 20) / np.max(np.abs(mono))
    return mono


def decibels(amplitude):
    return round(20.0 * math.log10(amplitude), 2)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path, help="Destination mono PCM WAV on E:")
    args = parser.parse_args()
    path = args.output.resolve()
    if path.drive.upper() != "E:":
        parser.error("Store the render on E:")
    path.parent.mkdir(parents=True, exist_ok=True)
    mono = render()
    pcm = np.rint(mono * 32767.0).astype("<i2")
    with wave.open(str(path), "wb") as file:
        file.setparams((1, 2, RATE, len(pcm), "NONE", "not compressed"))
        file.writeframes(pcm.tobytes())
    with wave.open(str(path), "rb") as file:
        assert (file.getnchannels(), file.getsampwidth(), file.getframerate(), file.getnframes()) == (1, 2, RATE, len(pcm))
    assert pcm[0] == pcm[-1] == 0
    assert not np.any(pcm[:round(RATE * OFF)])
    assert not np.any(np.abs(pcm.astype(np.int32)) >= 32767)
    beats = {}
    for name, cue in (("collapse", OFF), ("boot", ON)):
        start = round(RATE * cue)
        region = np.abs(mono[start: start + round(RATE * 0.060)])
        offset = int(np.argmax(region))
        beats[name] = {"start_s": cue, "peak_time_s": (start + offset) / RATE, "peak_dbfs": decibels(region[offset])}
    stats = {
        "file": str(path),
        "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
        "channels": 1,
        "sample_rate_hz": RATE,
        "bit_depth": 16,
        "duration_s": len(pcm) / RATE,
        "peak_dbfs": decibels(np.max(np.abs(mono))),
        "rms_dbfs": decibels(np.sqrt(np.mean(mono ** 2))),
        "mean_dbfs": decibels(abs(np.mean(mono))),
        "clipped_samples": 0,
        "beats": beats,
    }
    path.with_suffix(".json").write_text(json.dumps(stats, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(stats, indent=2))


if __name__ == "__main__":
    main()
