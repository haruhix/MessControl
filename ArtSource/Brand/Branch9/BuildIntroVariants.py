"""Render three distinct 3-second Branch9 intro studies from the approved V2 art.

Offline vector-layer compositing, explicitly not Unreal playback. Keeps all raw
frames, encoding/decode logs and validation on E:. Publishes only final MP4 and
contact sheets to the requested project artifact folder. No audio/network/DCC.
Usage: python BuildIntroVariants.py --variant A  (or B, C, all)
"""
from pathlib import Path
import argparse
import hashlib
import json
import math
import subprocess

import numpy as np
from PIL import Image, ImageChops, ImageDraw

ROOT = Path(__file__).resolve().parent
WORK = Path('E:/CodexWork/MessControlFrontEndBranch9_20261021/Art/Variants')
FINAL = Path('E:/DEVGAME/MessControl/Artifacts/Branch9Intro_20261021')
SIZE = (1920, 1080)
LOGO = (1440, 540)
FPS = 60
BACKGROUND = (6, 7, 8, 255)
FFMPEG = 'C:/ffmpeg/ffmpeg.exe'
FFPROBE = 'C:/ffmpeg/ffprobe.exe'
SPECS = {
    'A': dict(name='01_Branch9_WordThenBranch', hold=[.85, 2.61],
        description='Wordmark sweeps in, then the ascending branch and leaves settle. The approved native timing contract.',
        beats=[0, .15, .35, .6, 1.0, 2.2, 2.7, 3.0]),
    'B': dict(name='02_Branch9_BranchLeads', hold=[.98, 2.61],
        description='The branch is drawn alone first. The wordmark follows its path and settles vertically as one rigid composition.',
        beats=[0, .15, .32, .5, .8, 1.2, 2.2, 2.8]),
    'C': dict(name='03_Branch9_GraphicCut', hold=[.40, 2.68],
        description='One swift diagonal branch accent opens a graphic cut through the rigid letters. Fastest resolution, longest reading hold.',
        beats=[0, .12, .20, .28, .4, 1.2, 2.2, 2.9]),
}


def clamp(v):
    return max(0., min(1., v))


def cubic(v):
    return 1. - (1. - clamp(v)) ** 3


def smooth(v):
    v = clamp(v)
    return v * v * (3. - 2. * v)


def opacity(image, value):
    result = image.copy()
    result.putalpha(image.getchannel('A').point(lambda a: round(a * clamp(value))))
    return result


def layer(name):
    return Image.open(ROOT / f'T_Branch9_{name}.png').convert('RGBA').resize(
        LOGO, Image.Resampling.LANCZOS)


LAYERS = {name: layer(name) for name in ('Wordmark', 'Branch', 'Leaves', 'Logo')}
XX = np.arange(LOGO[0], dtype=np.float32)[None, :]
YY = np.arange(LOGO[1], dtype=np.float32)[:, None]
# Reference branch direction, expressed in the unchanged texture coordinate
# system. This is a reveal mask only: no vertex/letter deformation.
DIAGONAL_DISTANCE = np.abs(YY - (-.393 * XX + 600.5))


def mask_image(values):
    values = np.clip(values, 0, 1)
    values = values * values * (3 - 2 * values)
    return Image.fromarray((np.broadcast_to(values, (LOGO[1], LOGO[0])) * 255).astype('uint8'))


def with_mask(image, mask):
    result = image.copy()
    result.putalpha(ImageChops.multiply(result.getchannel('A'), mask))
    return result


def wipe(image, progress, soft=45.):
    edge = -soft + (LOGO[0] + 2 * soft) * progress
    return with_mask(image, mask_image((edge - XX) / soft))


def place(composition, scale=1., offset=(0, 0)):
    if abs(scale - 1.) > 1e-5:
        composition = composition.resize((round(LOGO[0] * scale),
            round(LOGO[1] * scale)), Image.Resampling.LANCZOS)
    output = Image.new('RGBA', SIZE, BACKGROUND)
    output.alpha_composite(composition, ((SIZE[0] - composition.width) // 2 + round(offset[0]),
        (SIZE[1] - composition.height) // 2 + round(offset[1])))
    return output.convert('RGB')


def render_a(t):
    result = Image.new('RGBA', LOGO)
    p = cubic((t - .10) / .42)
    result.alpha_composite(wipe(LAYERS['Wordmark'], p, 51.))
    branch = wipe(LAYERS['Branch'], cubic((t - .29) / .36), 33.)
    settle = 1. - cubic((t - .29) / .42)
    result.alpha_composite(branch, (round(-12 * settle), round(4.5 * settle)))
    leaves = opacity(LAYERS['Leaves'], smooth((t - .43) / .30))
    angle = 1.6 * (1. - cubic((t - .43) / .42))
    if angle:
        leaves = leaves.rotate(angle, Image.Resampling.BICUBIC, expand=False,
            center=(LOGO[0] * .66, LOGO[1] * .52))
    result.alpha_composite(leaves)
    exit_progress = smooth((t - 2.61) / .37)
    result = opacity(result, 1 - exit_progress)
    return place(result, 1 + .012 * (1 - cubic((t - .10) / .58)),
        (27 * exit_progress, 0))


def render_b(t):
    result = Image.new('RGBA', LOGO)
    # Lead with the organic mark. At .15s only the branch is visible; the
    # letter reveal starts after the ascending swash has established direction.
    result.alpha_composite(wipe(LAYERS['Branch'], cubic((t - .08) / .36), 25.))
    result.alpha_composite(opacity(LAYERS['Leaves'], smooth((t - .29) / .27)))
    word = wipe(LAYERS['Wordmark'], cubic((t - .34) / .52), 42.)
    # Lettering moves only as a rigid image: no resizing one axis, morphing or
    # independent per-letter bounce. Branch remains above the word layer.
    word_offset = round(13.5 * (1 - cubic((t - .34) / .54)))
    result.alpha_composite(word, (0, word_offset))
    result.alpha_composite(wipe(LAYERS['Branch'], cubic((t - .08) / .36), 25.))
    result.alpha_composite(opacity(LAYERS['Leaves'], smooth((t - .29) / .27)))
    exit_progress = smooth((t - 2.61) / .37)
    result = opacity(result, 1 - exit_progress)
    return place(result, 1., (-18 * exit_progress, 0))


def render_c(t):
    result = Image.new('RGBA', LOGO)
    # A single directional accent uses the actual branch silhouette. It opens
    # the letters outward from the branch diagonal, then the geometry rests.
    branch = wipe(LAYERS['Branch'], cubic((t - .07) / .20), 14.)
    result.alpha_composite(branch)
    band_width = 420. * cubic((t - .16) / .16)
    diagonal_mask = mask_image((band_width - DIAGONAL_DISTANCE) / 16.)
    word = with_mask(LAYERS['Wordmark'], diagonal_mask)
    word = opacity(word, smooth((t - .15) / .10))
    result.alpha_composite(word)
    result.alpha_composite(branch)
    result.alpha_composite(opacity(LAYERS['Leaves'], smooth((t - .23) / .12)))
    # The long still hold is followed by one clean, quick departure. No
    # repeated flashes, bloom pulse, frame shake or decorative lens flare.
    result = opacity(result, 1 - smooth((t - 2.84) / .13))
    return place(result)


RENDERERS = {'A': render_a, 'B': render_b, 'C': render_c}


def verify_source():
    manifest = json.loads((ROOT / 'AssetManifest.json').read_text())
    assert manifest['revision'] == 2, 'Use only the root-approved V2 identity'
    for item in manifest['layers']:
        assert hashlib.sha256((ROOT / item['file']).read_bytes()).hexdigest() == item['sha256']
    return manifest


def contact_sheet(key, path):
    spec = SPECS[key]
    sheet = Image.new('RGB', (1600, 2 * 252), (21, 22, 24))
    draw = ImageDraw.Draw(sheet)
    for index, time in enumerate(spec['beats']):
        image = RENDERERS[key](time).resize((400, 225), Image.Resampling.LANCZOS)
        x, y = index % 4 * 400, index // 4 * 252
        sheet.paste(image, (x, y))
        draw.text((x + 12, y + 231), f'{key}  {time:.2f} s', (224, 224, 224))
    sheet.save(path)


def validate_decoded_hold(key, path, directory):
    start, end = SPECS[key]['hold']
    # Skip the first .10s to stay clear of the encoder's reveal residual and
    # evaluate the full stationary reading interval, not legitimate animation.
    cmd = [FFMPEG, '-v', 'error', '-ss', f'{start + .10:.3f}', '-i', str(path),
        '-t', f'{end - start - .20:.3f}', '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-']
    decoded = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)
    (directory / 'HoldDecode.log').write_bytes(decoded.stderr)
    bytes_per_frame = SIZE[0] * SIZE[1] * 3
    assert len(decoded.stdout) % bytes_per_frame == 0
    n = len(decoded.stdout) // bytes_per_frame
    values = np.frombuffer(decoded.stdout, dtype=np.uint8).reshape(n, *SIZE[::-1], 3)
    first = values[0].astype(np.int16)
    # Encoding may change subpixel AA edges slightly. Broad flicker would
    # move the mean/99th percentile beyond these conservative pixel thresholds.
    worst_mean, worst_p99 = 0., 0.
    for value in values[1:]:
        delta = np.abs(value.astype(np.int16) - first)
        worst_mean = max(worst_mean, float(delta.mean()))
        worst_p99 = max(worst_p99, float(np.percentile(delta, 99)))
    assert worst_mean < .30 and worst_p99 <= 2., 'Unacceptable encoded hold flicker'
    return dict(decoded_hold_frames=n, maximum_mean_absolute_channel_delta=worst_mean,
        maximum_p99_absolute_channel_delta=worst_p99, gate='mean<0.30,p99<=2 of255')


def build(key):
    spec = SPECS[key]
    directory = WORK / key
    directory.mkdir(parents=True, exist_ok=True)
    output = FINAL / (spec['name'] + '.mp4')
    assert not output.exists(), 'Preserve prior review video: ' + str(output)
    # Pure compositing hold must be exactly stable before compression.
    hold_a, hold_b = spec['hold']
    assert hold_b - hold_a >= 1.5
    hold_first = RENDERERS[key](hold_a + .1).tobytes()
    for t in [hold_a + .2, (hold_a + hold_b) / 2, hold_b - .1]:
        assert RENDERERS[key](t).tobytes() == hold_first, 'Source hold is not stable'
    command = [FFMPEG, '-hide_banner', '-y', '-f', 'rawvideo', '-pixel_format', 'rgb24',
        '-video_size', f'{SIZE[0]}x{SIZE[1]}', '-framerate', str(FPS), '-i', '-', '-an',
        '-c:v', 'libx264', '-preset', 'medium', '-crf', '17', '-pix_fmt', 'yuv420p',
        '-movflags', '+faststart', str(output)]
    with (directory / 'Encode.log').open('wb') as log:
        process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=log, stderr=log)
        for index in range(FPS * 3):
            process.stdin.write(RENDERERS[key](index / FPS).tobytes())
        process.stdin.close()
        assert process.wait() == 0, 'ffmpeg encode failed'
    probe = subprocess.run([FFPROBE, '-v', 'error', '-count_frames', '-show_entries',
        'stream=codec_type,width,height,r_frame_rate,nb_read_frames,duration', '-of', 'json',
        str(output)], capture_output=True, text=True, check=True)
    data = json.loads(probe.stdout)
    stream = data['streams'][0]
    assert len(data['streams']) == 1 and stream['codec_type'] == 'video'
    assert (stream['width'], stream['height']) == SIZE
    assert stream['r_frame_rate'] == '60/1' and int(stream['nb_read_frames']) == 180
    assert abs(float(stream['duration']) - 3.) < .001
    decode = subprocess.run([FFMPEG, '-v', 'error', '-i', str(output), '-f', 'null', '-'],
        stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, check=True)
    (directory / 'FullDecode.log').write_bytes(decode.stderr)
    metrics = validate_decoded_hold(key, output, directory)
    sheet = FINAL / (spec['name'] + '_ContactSheet.png')
    contact_sheet(key, sheet)
    report = dict(variant=key, description=spec['description'], video=str(output),
        contact_sheet=str(sheet), sha256=hashlib.sha256(output.read_bytes()).hexdigest(),
        source='Approved V2 SVG-derived layer compositing; not Unreal runtime footage',
        width=SIZE[0], height=SIZE[1], fps=FPS, frames=180, duration=3., audio=False,
        full_read_hold_seconds=spec['hold'], source_hold_identical=True,
        rigid_lettering=True, source_identity_unchanged=True, decoded_hold=metrics,
        full_decode='passed', approval='awaiting root visual review', technical_passed=True)
    (directory / 'Validation.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--variant', choices=['A', 'B', 'C', 'all'], default='all')
    args = parser.parse_args()
    WORK.mkdir(parents=True, exist_ok=True)
    FINAL.mkdir(parents=True, exist_ok=True)
    verify_source()
    for key in ('A', 'B', 'C') if args.variant == 'all' else [args.variant]:
        build(key)
