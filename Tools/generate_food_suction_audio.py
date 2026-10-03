"""Generate original room-wide filtered wind, without external audio samples."""
from pathlib import Path
import math
import random
import struct
import wave

RATE = 24000
DURATION = 2.4
DESTINATION = Path(__file__).resolve().parents[1] / "ArtSource" / "Audio" / "FoodSuctionWind.wav"


def smooth(value):
    value = max(0.0, min(1.0, value))
    return value * value * (3.0 - 2.0 * value)


def generate():
    noise = random.Random(735214)
    bands = [noise.uniform(-1.0, 1.0) for _ in range(8)]
    low = high_low = pressure = 0.0
    low_alpha = 1.0 - math.exp(-math.tau * 1800.0 / RATE)
    high_alpha = 1.0 - math.exp(-math.tau * 80.0 / RATE)
    pressure_alpha = 1.0 - math.exp(-math.tau * 120.0 / RATE)
    samples = []
    for index in range(round(RATE * DURATION)):
        time = index / RATE
        for band in range(len(bands)):
            if index % (1 << band) == 0:
                bands[band] = noise.uniform(-1.0, 1.0)
        pink = sum(bands) / len(bands)
        broad = 0.74 * pink + 0.26 * noise.uniform(-1.0, 1.0)
        low += low_alpha * (broad - low)
        high_low += high_alpha * (low - high_low)
        wind = low - high_low
        pressure += pressure_alpha * (pink - pressure)
        gust = 0.76 + 0.12 * math.sin(math.tau * 1.12 * time) + 0.06 * math.sin(math.tau * 2.7 * time + 0.8)
        resonance = 0.014 * math.sin(math.tau * 62.0 * time) + 0.009 * math.sin(math.tau * 93.0 * time + 0.4)
        envelope = smooth(time / 0.15) * smooth((DURATION - time) / 0.25)
        samples.append((wind * gust + pressure * 0.16 + resonance) * envelope)
    scale = 0.44 / max(abs(sample) for sample in samples)
    pcm = b"".join(struct.pack("<h", round(sample * scale * 32767)) for sample in samples)
    DESTINATION.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(DESTINATION), "wb") as output:
        output.setnchannels(1)
        output.setsampwidth(2)
        output.setframerate(RATE)
        output.writeframes(pcm)
    print(f"MC_FOOD_SUCTION_AUDIO_GENERATED {DESTINATION} duration={DURATION} rate={RATE}")


if __name__ == "__main__":
    generate()
