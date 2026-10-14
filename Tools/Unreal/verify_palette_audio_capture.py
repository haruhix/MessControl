"""Check the fresh WAV exported by smoke_metasound_gameplay.py.

Run after its solo PIE session ends: python Tools/Unreal/verify_palette_audio_capture.py
An optional first argument selects a different GameplaySmoke.json report.
The source/volume/pitch check is separate from this complete non-silent PCM check.
"""
import array
import json
import math
import struct
import sys
import wave
from pathlib import Path

report_path = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[2] / 'Saved' / 'AudioDARouting' / 'GameplaySmoke.json'
report = json.loads(report_path.read_text(encoding='utf-8'))
assert report['status'] == 'passed', report
capture = Path(report['audio_capture_requested'])
raw = capture.read_bytes()
assert raw[:4] == b'RIFF' and raw[8:12] == b'WAVE', 'Not a RIFF/WAVE file'
assert len(raw) == struct.unpack_from('<I', raw, 4)[0] + 8, 'WAV export is incomplete'
with wave.open(str(capture), 'rb') as sound:
    channels, width, rate, frames = sound.getnchannels(), sound.getsampwidth(), sound.getframerate(), sound.getnframes()
    pcm = sound.readframes(frames)
assert width == 2 and len(pcm) == channels * width * frames, 'Expected complete 16-bit PCM'
samples = array.array('h', pcm)
assert samples and any(samples), 'Audio mixer output is silent'
peak = max(abs(value) for value in samples) / 32768.0
rms = math.sqrt(sum(value * value for value in samples) / len(samples)) / 32768.0
report['audio_output_recorded'] = True
report['audio_capture'] = dict(path=str(capture), duration_seconds=frames / rate,
                               channels=channels, sample_rate=rate, sample_width=width,
                               peak=peak, rms=rms, complete_riff=True,
                               note='Master mixer recording during solo gameplay; route paths separately verified in selected_sources')
report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps(dict(status=report['status'], missing=report['missing_gameplay_events'],
                     routing_errors=report['routing_errors'], counts=report['gameplay_event_counts'],
                     checks=report['checks'], capture=report['audio_capture']), ensure_ascii=False))
