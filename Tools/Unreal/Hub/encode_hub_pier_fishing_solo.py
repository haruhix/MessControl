"""Encode the five actual timestamped windows of a completed Hub pier solo report.

Uses the existing shared native-capture encoder. Frames are repeated for 30 fps
presentation; no interpolated poses, time scaling or invented gameplay frames.
Only E: source reports, input frames, temporary paths and outputs are accepted.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from encode_kenney_snow_powder_capture import encode_clip


CLIPS = ('SpawnReady', 'PierAutoEquip', 'PierKeyToggle', 'PierExitAndGate', 'PierReentry')


def on_e(value):
    path = Path(value).resolve()
    if path.drive.upper() != 'E:':
        raise ValueError('Task inputs and outputs must remain on E: '+str(path))
    return path


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--ffmpeg', default='C:/ffmpeg/ffmpeg.exe')
    parser.add_argument('--ffprobe', default='C:/ffmpeg/ffprobe.exe')
    args = parser.parse_args()
    source = on_e(args.report)
    proof = json.loads(source.read_text(encoding='utf-8-sig'))
    if proof.get('passed') is not True or proof.get('status') != 'passed':
        raise ValueError('Only a completed passed native solo report may be encoded')
    if proof.get('restored_settings') is not True or proof.get('owned_pie_stopped') is not True:
        raise ValueError('The solo report must confirm owned PIE stop and setting restoration')
    output = on_e(args.output)
    output.mkdir(parents=True, exist_ok=True)
    temporary = output/'Temp'; temporary.mkdir(exist_ok=True)
    os.environ['TEMP'] = os.environ['TMP'] = str(temporary)
    source_sha = digest(source)
    report = dict(source_report=str(source), source_report_sha256=source_sha,
        native_report_passed=True, checks=proof.get('checks'), limitations=proof.get('limitations'),
        capture_hz=proof.get('capture_hz'),
        timing='Separate genuine game-time windows; native frames repeated for 30 fps, no temporal interpolation or time scaling.',
        visual_review='Pending root review; successful encoding and gameplay assertions do not certify visual quality.', windows={})
    for name in CLIPS:
        rows = proof.get('clips',{}).get(name,[])
        if len(rows)<2:
            raise ValueError('Required clip has fewer than two captured frames: '+name)
        hashes = {}
        for row in rows:
            frame = on_e(row['file'])
            if not frame.is_file():
                raise ValueError('Source frame is missing: '+str(frame))
            hashes[str(frame)] = digest(frame)
            if row.get('sha256') and hashes[str(frame)] != row['sha256']:
                raise ValueError('Source capture changed after the native report: '+str(frame))
        clip = encode_clip(name,rows,output,args.ffmpeg,args.ffprobe)
        if any(digest(Path(path))!=sha for path,sha in hashes.items()):
            raise ValueError('Native source frames changed during encoding: '+name)
        clip['video_sha256'] = digest(Path(clip['video']))
        clip['source_frames_unchanged'] = True
        report['windows'][name] = clip
    if digest(source) != source_sha:
        raise ValueError('Native source report changed during encoding')
    report['decode_passed'] = True
    target = output/'VideoSourceReport.json'
    target.write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(dict(manifest=str(target),videos={name:clip['video']
        for name,clip in report['windows'].items()}),ensure_ascii=False,indent=2))


if __name__=='__main__':
    main()
