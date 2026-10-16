"""Build a CPU-only QA timeline from recorded Unreal screenshots.

Usage: python build_transition_contact_sheet.py <capture_directory>
Reads capture_receipt.json and native_route_actual_states.json. Writes only
<capture_directory>/QA_transition_timeline.jpg; no UE/DCC/network access.
Day captions describe recorded native state, not OCR or assumed render state.
"""
from pathlib import Path
import argparse
import json
import math
from PIL import Image, ImageDraw, ImageFont


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture_directory', type=Path)
    parser.add_argument('--tile-width', type=int, default=480)
    parser.add_argument('--output-name', default='QA_transition_timeline.jpg')
    args = parser.parse_args()
    folder = args.capture_directory.resolve(strict=True)
    if not folder.is_dir():
        parser.error('capture_directory must be a directory')
    if not 320 <= args.tile_width <= 640:
        parser.error('tile width must be between320 and640')
    if Path(args.output_name).name != args.output_name or not args.output_name.startswith('QA_') or not args.output_name.lower().endswith('.jpg'):
        parser.error('output must be a plain QA_*.jpg filename in capture_directory')
    capture = json.loads((folder/'capture_receipt.json').read_text(encoding='utf8'))
    route = json.loads((folder/'native_route_actual_states.json').read_text(encoding='utf8'))
    offset = float(capture['started_monotonic'])-float(route['started'])
    frames = []
    for item in capture['frames']:
        path = (folder/item['file']).resolve()
        if path.parent != folder:
            raise ValueError('Screenshot path must stay directly in capture_directory')
        if path.is_file():
            frames.append({'file': item['file'], 'path': path, 'time': float(item['time'])+offset})
    frames.sort(key=lambda item: item['time'])
    samples = sorted(route.get('samples', []), key=lambda item: item['elapsed'])
    actions = sorted(route.get('actions', []), key=lambda item: item['elapsed'])
    if not frames or not samples:
        raise ValueError('Nonempty recorded frames and native state samples are required')
    warnings = []

    def state_at(time):
        # Never label a frame with a future native state sample.
        previous = [sample for sample in samples if float(sample['elapsed']) <= time+1e-6]
        return previous[-1] if previous else None

    def closest(time):
        return min(frames, key=lambda frame: abs(frame['time']-time))

    def transition(before, after):
        for action in actions:
            if action.get('action') != 'StepDay':
                continue
            t = float(action['elapsed'])
            old = state_at(t-1e-5)
            if old is None:
                continue
            selected = int(old.get('selected', 0))
            step = int(action.get('value') or 0)
            if selected == before and max(1, min(7, selected+step)) == after:
                return action
        return None

    tiles = []
    for before, after in ((3, 4), (7, 6)):
        action = transition(before, after)
        if action is None:
            warning = f'StepDay {before}->{after} not recorded'
            warnings.append(warning)
            tiles.extend([{'title': f'{before}->{after} | {phase}', 'missing': warning}
                          for phase in ('before', 'early', 'mid', 'settled')])
            continue
        t = float(action['elapsed'])
        following = [float(other['elapsed']) for other in actions if float(other['elapsed']) > t+1e-6]
        end = following[0] if following else frames[-1]['time']
        settled = min(t+1.5, end-.2, frames[-1]['time'])
        if settled < t+.8:
            warnings.append(f'{before}->{after}: less than.8s settling coverage before next action/end')
        requests = [('before', t-.15), ('early', t+.15), ('mid', t+.60), ('settled', max(t, settled))]
        for phase, requested in requests:
            if requested < frames[0]['time']-.05 or requested > frames[-1]['time']+.05 or (phase != 'before' and requested >= end):
                warning = f'{before}->{after} {phase}: capture/action interval does not cover requested time'
                warnings.append(warning)
                tiles.append({'title': f'{before}->{after} | {phase}', 'missing': warning})
                continue
            frame = closest(requested)
            tiles.append({'title': f'Step {before}->{after} | {phase}', 'frame': frame,
                          'action_time': t, 'requested_time': requested})

    for day in (1, 2, 3):
        candidates = []
        for sample in samples:
            if int(sample.get('page', -1)) != 1 or int(sample.get('active', 0)) != day or int(sample.get('selected', 0)) != day:
                continue
            openness = sample.get('openness', sample.get('opens'))
            if openness is not None and (len(openness) < day or float(openness[day-1]) < .98):
                continue
            if abs(float(sample.get('three_row_sum', 740.))-740.) > 1.:
                continue
            t = float(sample['elapsed'])
            prior = [float(action['elapsed']) for action in actions if float(action['elapsed']) <= t+1e-6]
            following = [float(action['elapsed']) for action in actions if float(action['elapsed']) > t+1e-6]
            if prior and t-prior[-1] < 1.0:
                continue
            if following and following[0]-t < .25:
                continue
            if frames[0]['time'] <= t <= frames[-1]['time']:
                candidates.append(sample)
        if candidates:
            chosen = candidates[-1]
            tiles.append({'title': f'DAY {day:02} | settled Open', 'frame': closest(float(chosen['elapsed']))})
        else:
            warning = f'DAY {day:02} stable Open not captured'
            warnings.append(warning)
            tiles.append({'title': f'DAY {day:02} | settled Open', 'missing': warning})

    width = args.tile_width
    image_height = round(width*9/16)
    caption_height = 55
    tile_height = image_height+caption_height
    columns, rows = 4, math.ceil((len(tiles)+1)/4)
    sheet = Image.new('RGB', (columns*width, rows*tile_height), (21, 21, 25))
    draw = ImageDraw.Draw(sheet)
    try:
        font = ImageFont.truetype('C:/Windows/Fonts/segoeui.ttf', max(13, round(width/28)))
    except OSError:
        font = ImageFont.load_default()
    for index, tile in enumerate(tiles):
        x, y = index%columns*width, index//columns*tile_height
        draw.text((x+8, y+4), tile['title'], font=font, fill=(242, 238, 232))
        frame = tile.get('frame')
        if frame is None:
            draw.text((x+12, y+80), tile['missing'], font=font, fill=(238, 161, 139))
            continue
        state = state_at(frame['time'])
        day = int(state.get('selected', 0)) if state else 0
        label = f"{frame['file']} | route {frame['time']:.3f}s | state D{day:02}"
        draw.text((x+8, y+27), label, font=font, fill=(192, 208, 217))
        with Image.open(frame['path']) as source:
            image = source.convert('RGB')
        image.thumbnail((width, image_height), Image.Resampling.LANCZOS)
        sheet.paste(image, (x+(width-image.width)//2, y+caption_height+(image_height-image.height)//2))
        if 'action_time' in tile:
            print(f"{tile['title']}: {frame['file']} route={frame['time']:.3f}s action={tile['action_time']:.3f}s state=D{day:02}")
        else:
            print(f"{tile['title']}: {frame['file']} route={frame['time']:.3f}s state=D{day:02}")
    legend_index = len(tiles)
    x, y = legend_index%columns*width+10, legend_index//columns*tile_height+9
    lines = ['Actual Unreal capture only', f'capture start - route start = {offset:+.6f}s',
             'Caption day = preceding native sample.', 'Render may lag native state.',
             'No unrecorded state is substituted.']
    lines.extend(warnings[:4])
    for index, line in enumerate(lines):
        draw.text((x, y+index*25), line, font=font, fill=(218, 207, 185))
    target = folder/args.output_name
    sheet.save(target, quality=94)
    print('QA_SHEET', target)
    for warning in warnings:
        print('COVERAGE_WARNING', warning)


if __name__ == '__main__':
    main()
