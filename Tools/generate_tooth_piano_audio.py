"""Original piano octave samples: struck strings and decaying partials."""
from pathlib import Path
import math
import random
import struct
import wave

RATE = 44100
DESTINATION = Path(__file__).resolve().parents[1] / "ArtSource" / "Audio"


def generate(octave):
    duration = 3.6 * .77 ** (octave - 4)
    fundamental = 440 * 2 ** ((12 * (octave + 1) - 69) / 12)
    amplitudes = (1, .72, .46, .26, .20, .14, .10, .075, .048, .032, .021, .014)
    noise = random.Random(601041)
    partials = []
    for harmonic, amplitude in enumerate(amplitudes, 1):
        frequency = fundamental * harmonic * math.sqrt((1 + .00012 * harmonic ** 2) / 1.00012)
        if frequency < RATE * .46:
            partials.append((math.tau * frequency, amplitude, harmonic))
    samples = []
    for index in range(round(RATE * duration)):
        time = index / RATE
        attack = 1 - math.exp(-time / .0025)
        release = min(1, (duration - time) / .12)
        value = 0
        for angular, amplitude, harmonic in partials:
            phase = .16 * harmonic
            strings = .58 * math.sin(angular * time + phase) + .42 * math.sin(angular * 1.0008 * time + phase)
            decay_rate = 1.3 ** (octave - 4)
            decay = .7 * math.exp(-(1.2 + .24 * harmonic) * time * decay_rate) + .3 * math.exp(-(.55 + .15 * harmonic) * time * decay_rate)
            value += amplitude * strings * decay
        value += .1 * noise.uniform(-1, 1) * math.exp(-time / .008)
        samples.append(value * attack * release)
    scale = .78 / max(abs(sample) for sample in samples)
    pcm = b"".join(struct.pack("<h", round(sample * scale * 32767)) for sample in samples)
    DESTINATION.mkdir(parents=True, exist_ok=True)
    source = DESTINATION / f"ToothPianoC{octave}.wav"
    with wave.open(str(source), "wb") as output:
        output.setnchannels(1)
        output.setsampwidth(2)
        output.setframerate(RATE)
        output.writeframes(pcm)
    print(f"MC_TOOTH_PIANO_AUDIO_GENERATED {source} duration={duration:.3f} rate={RATE}")


if __name__ == "__main__":
    for octave in range(4, 8):
        generate(octave)
