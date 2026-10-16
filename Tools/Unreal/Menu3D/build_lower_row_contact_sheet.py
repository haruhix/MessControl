"""Make an annotated QA sheet from actual Unreal opening screenshots only."""
import argparse
import json
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('capture', type=Path)
p.add_argument('output', type=Path)
p.add_argument('--start', type=float, default=42.0)
p.add_argument('--offsets', type=float, nargs=9, default=[-.15, .05, .15, .25, .4, .65, 1.0, 1.5, 2.3])
args = p.parse_args()
capture = json.loads((args.capture/'capture_receipt.json').read_text(encoding='utf8'))
route = json.loads((args.capture/'native_route_actual_states.json').read_text(encoding='utf8'))
offset = capture['started_monotonic']-route['started']
frames = [(float(f['time'])+offset, f['file']) for f in capture['frames']]
times = args.offsets
sheet = Image.new('RGB', (1920, 909), '#15171a')
draw = ImageDraw.Draw(sheet)
font = ImageFont.truetype('C:/Windows/Fonts/segoeui.ttf', 16)
receipt = []
for i, dt in enumerate(times):
    t, name = min(frames, key=lambda f: abs(f[0]-(args.start+dt)))
    image = Image.open(args.capture/name).convert('RGB')
    # Recorded floating PIE client is1280x720 within1286x760window.
    assert image.size == (1286, 760), image.size
    image = image.crop((3, 36, 1283, 756))
    # Focus on the lower viewport containing DAY03 and its adjacent row.
    image = image.crop((160, 250, 1120, 660)).resize((640, 273))
    x, y = i%3*640, i//3*303
    draw.text((x+8, y+5), f'{name} | route {t:.3f}s | delta {t-args.start:+.3f}s', font=font, fill='#faf3df')
    sheet.paste(image, (x, y+30))
    receipt.append({'file': name, 'route_time': t, 'action_delta': t-args.start})
args.output.parent.mkdir(parents=True, exist_ok=True)
sheet.save(args.output, quality=94)
args.output.with_suffix('.json').write_text(json.dumps({'source': str(args.capture.resolve()), 'actual_frames': receipt, 'crop': 'QA crop of lower viewport; no pose synthesis'}, indent=2), encoding='utf8')
print(args.output.resolve())
