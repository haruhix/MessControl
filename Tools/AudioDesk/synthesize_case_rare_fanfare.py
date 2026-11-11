"""Synthesize the original Case Rare arcade brass/chime one-shot at 48 kHz."""
import argparse
import json
import math
from pathlib import Path
import wave

import numpy as np


RATE = 48_000
DURATION = 1.32
TAU = 2.0 * math.pi


def hz(midi):
    return 440.0 * 2.0 ** ((midi - 69) / 12.0)


def times(seconds):
    return np.arange(round(RATE * seconds)) / RATE


def struck_envelope(t, attack, release, decay, sustain=0.52):
    rising = np.sin(np.pi * np.minimum(t / attack, 1.0) / 2.0) ** 2
    falling = np.sin(np.pi * np.minimum((t[-1] - t) / release, 1.0) / 2.0) ** 2
    return rising * falling * (sustain + (1.0 - sustain) * np.exp(-t / decay))


def brass(midi, seconds, dynamic=1.0):
    t = times(seconds)
    frequency = hz(midi)
    # Airy attack, tuned body, light late vibrato, and a brass-like harmonic taper.
    pitch_slide = 0.0035 * (1.0 - np.exp(-t / 0.010)) * frequency * 0.010
    vibrato = 0.035 * (1.0 - np.exp(-t / 0.065)) * np.sin(TAU * 6.2 * t)
    phase = TAU * (frequency * t - pitch_slide) + vibrato
    body = 0.78 * np.sin(phase) + 0.16 * np.sin(2.0 * phase)
    brightness = (0.55 + 0.45 * np.exp(-t / 0.070)) * dynamic
    brass_partials = 0.20 * np.sin(3.0 * phase) + 0.095 * np.sin(4.0 * phase)
    brass_partials += 0.064 * np.sin(5.0 * phase) + 0.026 * np.sin(7.0 * phase)
    return (body + brightness * brass_partials) * struck_envelope(t, 0.006, 0.035, 0.085)


def chime(midi, seconds):
    t = times(seconds)
    phase = TAU * hz(midi) * t
    body = 0.67 * np.sin(phase) * np.exp(-t / 0.24)
    overtone = 0.21 * np.sin(phase * 2.008) * np.exp(-t / 0.10)
    sparkle = 0.12 * np.sin(phase * 3.015) * np.exp(-t / 0.046)
    return (body + overtone + sparkle) * struck_envelope(t, 0.003, 0.080, 0.12, 1.0)


def add(dest, start, voice, gain):
    offset = round(RATE * start)
    end = min(len(dest), offset + len(voice))
    dest[offset:end] += voice[:end - offset] * gain


def render():
    rng = np.random.default_rng(12583)
    mono = np.zeros(round(DURATION * RATE))
    # The syncopated E–G–A–B pickup lands on a C-major brass chord.
    pickup = (
        (0.000, 76, 0.118, 0.52),
        (0.122, 79, 0.120, 0.57),
        (0.245, 81, 0.082, 0.61),
        (0.338, 83, 0.127, 0.66),
    )
    for start, midi, length, gain in pickup:
        add(mono, start, brass(midi, length), gain)
        add(mono, start + 0.004, chime(midi + 12, 0.19), gain * 0.13)

    resolve = 0.496
    for midi, delay, gain in ((60, 0.005, 0.27), (67, 0.003, 0.20), (76, 0.011, 0.20), (84, 0.000, 0.65)):
        add(mono, resolve + delay, brass(midi, 0.535, 0.86), gain)
    add(mono, resolve, chime(84, 0.67), 0.29)
    add(mono, resolve + 0.065, chime(91, 0.59), 0.095)

    # Short mechanical arcade punch at the resolution, followed by a tiny room return.
    click_t = times(0.13)
    click_noise = rng.normal(0.0, 1.0, len(click_t))
    click_phase = TAU * (69.0 * click_t + 3.6 * (1.0 - np.exp(-26.0 * click_t)))
    click = np.sin(click_phase) * np.exp(-30.0 * click_t)
    click += 0.12 * click_noise * np.exp(-90.0 * click_t)
    add(mono, resolve, click * struck_envelope(click_t, 0.004, 0.022, 0.045), 0.19)
    dry = mono.copy()
    for delay, gain in ((0.028, 0.070), (0.057, 0.044), (0.091, 0.024), (0.138, 0.011)):
        add(mono, delay, dry, gain)

    mono -= np.mean(mono)
    mono *= 10.0 ** (-3.0 / 20.0) / np.max(np.abs(mono))
    fade_samples = round(RATE * 0.055)
    mono[-fade_samples:] *= np.linspace(1.0, 0.0, fade_samples)
    pcm = np.rint(mono * 32767.0).astype("<i2")
    return mono, pcm


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path, help="E: output WAV path")
    args = parser.parse_args()
    destination = args.output.resolve()
    if destination.drive.upper() != "E:":
        parser.error("Store the render on E:")
    destination.parent.mkdir(parents=True, exist_ok=True)
    mono, pcm = render()
    with wave.open(str(destination), "wb") as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(RATE)
        out.writeframes(pcm.tobytes())
    stats = {
        "file": str(destination),
        "duration_seconds": len(pcm) / RATE,
        "sample_rate_hz": RATE,
        "channels": 1,
        "bit_depth": 16,
        "peak_dbfs": round(20.0 * math.log10(np.max(np.abs(mono))), 2),
        "rms_dbfs": round(20.0 * math.log10(np.sqrt(np.mean(mono ** 2))), 2),
        "clipped_samples": int(np.count_nonzero(np.abs(pcm.astype(np.int32)) == 32767)),
    }
    print(json.dumps(stats, indent=2))


if __name__ == "__main__":
    main()
