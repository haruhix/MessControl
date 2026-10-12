"""Encode genuine PIE screenshot sequences using their recorded game timestamps."""
import argparse
import json
import subprocess
import struct
from pathlib import Path


def encode(name, frames, output, ffmpeg):
    output.mkdir(parents=True, exist_ok=True)
    normalized = output/'_frames'/name
    valid = []
    for frame in frames:
        path = Path(frame['file']).resolve()
        if not path.is_file():
            suffixed = sorted(path.parent.glob(path.stem+'[0-9][0-9][0-9][0-9][0-9].png'))
            if len(suffixed) == 1:
                path = suffixed[0]
        if path.is_file():
            data = path.read_bytes()
            if data.startswith(b'\x89PNG\r\n\x1a\n'):
                offset = 8
                while offset+12 <= len(data):
                    length = int.from_bytes(data[offset:offset+4], 'big')
                    end = offset+12+length
                    if end > len(data):
                        raise RuntimeError('Incomplete PNG chunk: '+str(path))
                    if data[offset+4:offset+8] == b'IEND':
                        if end < len(data):
                            # UE's render-target exporter can leave bytes after
                            # IEND. Strip only that tail; retain original pixels.
                            normalized.mkdir(parents=True, exist_ok=True)
                            clean = normalized/path.name
                            clean.write_bytes(data[:end])
                            path = clean
                        break
                    offset = end
                else:
                    raise RuntimeError('PNG has no IEND: '+str(path))
            valid.append((path, float(frame.get('time', frame.get('game_time', 0.)))))
    if len(valid) < 3:
        raise RuntimeError(f'{name}: fewer than three genuine frames available')
    manifest = output/(name+'.ffconcat')
    rows = ['ffconcat version 1.0']
    for index, (path, stamp) in enumerate(valid):
        duration = max(.001, valid[index+1][1]-stamp) if index+1 < len(valid) else 1./12.
        escaped = path.as_posix().replace("'", "'\\''")
        rows.extend(["file '"+escaped+"'", f'duration {duration:.6f}'])
    escaped = valid[-1][0].as_posix().replace("'", "'\\''")
    rows.append("file '"+escaped+"'")
    manifest.write_text('\n'.join(rows)+'\n', encoding='utf8')
    video = output/(name+'.mp4')
    width, height = struct.unpack('>II', valid[0][0].read_bytes()[16:24])
    # SHOWUI includes the floating Unreal window's frame. Keep only its actual
    # 1280x720 game client; scene-capture clips already contain only the game.
    filters = ('crop=1280:720:3:35,' if (width, height) == (1286, 760) else '')+'scale=1280:-2:flags=lanczos'
    subprocess.run([str(ffmpeg), '-hide_banner', '-loglevel', 'error', '-y',
                    '-safe', '0', '-f', 'concat', '-i', str(manifest),
                    '-vf', filters, '-r', '30',
                    '-c:v', 'libx264', '-preset', 'medium', '-crf', '19',
                    '-pix_fmt', 'yuv420p', '-movflags', '+faststart', str(video)], check=True)
    duration = valid[-1][1]-valid[0][1]
    poster = output/(name+'.png')
    subprocess.run([str(ffmpeg), '-hide_banner', '-loglevel', 'error', '-y',
                    '-ss', str(min(1.2, duration*.25)), '-i', str(video),
                    '-frames:v', '1', str(poster)], check=True)
    ffprobe = ffmpeg.with_name('ffprobe.exe')
    result = subprocess.run([str(ffprobe), '-v', 'error', '-show_entries',
                             'format=duration:stream=codec_name,width,height,nb_frames',
                             '-of', 'json', str(video)], capture_output=True, text=True, check=True)
    return dict(video=video.as_posix(), poster=poster.as_posix(),
                genuine_source_frames=len(valid), game_duration=duration,
                encoded=json.loads(result.stdout), timestamps=manifest.as_posix())


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('report', type=Path)
    parser.add_argument('--name', required=True)
    parser.add_argument('--clip')
    parser.add_argument('--output', type=Path, default=Path('Artifacts/ApprovedMechanics_20261012'))
    parser.add_argument('--ffmpeg', type=Path, default=Path('C:/ffmpeg/ffmpeg.exe'))
    args = parser.parse_args()
    report = json.loads(args.report.read_text(encoding='utf8'))
    frames = report['clips'][args.clip] if args.clip else report['frames']
    if isinstance(frames, dict):
        frames = frames['frames']
    result = encode(args.name, frames, args.output.resolve(), args.ffmpeg)
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output/(args.name+'.json')).write_text(json.dumps(result, indent=2), encoding='utf8')
    print(json.dumps(result, indent=2))
