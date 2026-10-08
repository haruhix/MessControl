"""Encode timestamped Unreal gameplay screenshots as an MP4 and a PNG cover."""

import argparse
import csv
import json
import math
import statistics
import subprocess
from pathlib import Path


def read_frames(directory: Path):
    timing = directory / "FrameTimes.csv"
    if not timing.is_file():
        raise RuntimeError(f"Capture timing is missing: {timing}")
    source = timing.read_text(encoding="utf-8-sig")
    delimiter = "\t" if "\t" in source.partition("\n")[0] else ","
    rows = list(csv.reader(source.splitlines(), delimiter=delimiter))
    if not rows:
        raise RuntimeError("Capture timing contains no rows")

    header = [field.strip().lower() for field in rows[0]]
    has_header = any(field in header for field in ("frame", "file", "filename"))
    frame_index = next((header.index(field) for field in ("frame", "file", "filename") if field in header), 0)
    time_index = next((header.index(field) for field in ("server_time", "time", "seconds") if field in header), 1)
    stage_index = header.index("stage") if "stage" in header else 2
    samples = []
    missing = []
    seen = set()
    for row in rows[1:] if has_header else rows:
        if not row or all(not field.strip() for field in row):
            continue
        frame = row[frame_index].strip()
        filename = f"Frame{int(frame):05d}.png" if frame.isdecimal() else Path(frame).name
        stamp = float(row[time_index])
        if not math.isfinite(stamp):
            raise RuntimeError(f"Invalid frame timestamp: {row}")
        path = directory / filename
        if not path.is_file():
            missing.append(filename)
            continue
        if filename in seen:
            continue
        seen.add(filename)
        stage = row[stage_index].strip() if len(row) > stage_index else ""
        samples.append((path, stamp, stage))

    if len(samples) < 2:
        raise RuntimeError(f"Only {len(samples)} rendered frames exist")
    if any(current[1] <= previous[1] for previous, current in zip(samples, samples[1:])):
        raise RuntimeError("Frame timestamps must increase; use captures from one game world")
    return samples, missing


def run_encoder(arguments):
    subprocess.run(arguments, check=True)


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--frames", type=Path, default=root / "Saved/IceEventFrames")
    parser.add_argument("--output", type=Path, default=root / "Artifacts/IceEvent_20261009")
    parser.add_argument("--name", default="IceEvent", help="Output filename stem")
    parser.add_argument("--ffmpeg", type=Path, default=Path("C:/ffmpeg/ffmpeg.exe"))
    parser.add_argument("--ffprobe", type=Path, default=Path("C:/ffmpeg/ffprobe.exe"))
    parser.add_argument("--fps", type=int, default=30)
    parser.add_argument("--thumbnail-time", type=float, default=4.0, help="Seconds from the start of the encoded clip")
    parser.add_argument("--exclude-stage", action="append", default=[], help="Omit this stage from the video; repeat to omit additional stages")
    parser.add_argument("--exclude-frame", action="append", default=[], help="Omit this frame filename from the video; repeat to omit additional frames")
    args = parser.parse_args()
    if not args.name or Path(args.name).name != args.name or any(char in args.name for char in '<>:"/\\|?*'):
        parser.error("--name must be a simple filename stem")
    if not 5 <= args.fps <= 60:
        parser.error("--fps must be between 5 and 60")
    if not math.isfinite(args.thumbnail_time) or args.thumbnail_time < 0:
        parser.error("--thumbnail-time must be finite and non-negative")
    if not args.ffmpeg.is_file() or not args.ffprobe.is_file():
        parser.error("ffmpeg and ffprobe executables are required")

    frames = args.frames.resolve()
    output = args.output.resolve()
    source_samples, missing = read_frames(frames)
    source_intervals = [current[1] - previous[1] for previous, current in zip(source_samples, source_samples[1:])]
    source_intervals.append(statistics.median(source_intervals))
    excluded = set(args.exclude_stage)
    excluded_files = {Path(filename).name for filename in args.exclude_frame}
    included = [(sample, interval) for sample, interval in zip(source_samples, source_intervals)
                if sample[2] not in excluded and sample[0].name not in excluded_files]
    if len(included) < 2:
        raise RuntimeError("Capture exclusions leave fewer than two rendered frames")
    samples = [sample for sample, _ in included]
    intervals = [interval for _, interval in included]
    # Preserve each kept frame's original duration without retaining omitted stages as pauses.
    duration = sum(intervals)
    output.mkdir(parents=True, exist_ok=True)
    concat = output / "Frames.ffconcat"

    def quote_path(path):
        return path.as_posix().replace("'", "'\\''")

    entries = ["ffconcat version 1.0"]
    for index, (path, _, _) in enumerate(samples):
        interval = intervals[index]
        entries.extend((f"file '{quote_path(path)}'", f"duration {interval:.9f}"))
    # The concat demuxer needs a final repeated file to retain the final duration.
    entries.append(f"file '{quote_path(samples[-1][0])}'")
    concat.write_text("\n".join(entries) + "\n", encoding="utf-8")

    video = output / f"{args.name}.mp4"
    cover = output / f"{args.name}.png"
    run_encoder([
        str(args.ffmpeg), "-hide_banner", "-loglevel", "warning", "-y",
        "-f", "concat", "-safe", "0", "-i", str(concat),
        "-vf", f"fps={args.fps},pad=ceil(iw/2)*2:ceil(ih/2)*2",
        "-t", f"{duration:.9f}", "-c:v", "libx264", "-preset", "medium",
        "-crf", "18", "-pix_fmt", "yuv420p", "-movflags", "+faststart", str(video),
    ])
    thumbnail_time = min(args.thumbnail_time, max(0.0, duration - 1.0 / args.fps))
    run_encoder([
        str(args.ffmpeg), "-hide_banner", "-loglevel", "warning", "-y",
        "-ss", f"{thumbnail_time:.9f}", "-i", str(video),
        "-frames:v", "1", "-update", "1", str(cover),
    ])
    result = subprocess.run([
        str(args.ffprobe), "-v", "error", "-select_streams", "v:0",
        "-show_entries", "stream=codec_name,width,height,avg_frame_rate,nb_frames:format=duration",
        "-of", "json", str(video),
    ], check=True, capture_output=True, text=True)
    probe = json.loads(result.stdout)
    metadata = {
        "source": "Unreal FScreenshotRequest rendered gameplay",
        "frames_directory": str(frames),
        "source_frames": len(source_samples),
        "rendered_frames": len(samples),
        "excluded_stages": list(dict.fromkeys(args.exclude_stage)),
        "excluded_frame_files": sorted(excluded_files),
        "excluded_frames": len(source_samples) - len(samples),
        "missing_frames": missing,
        "first_server_time": samples[0][1],
        "last_server_time": samples[-1][1],
        "gameplay_duration": duration,
        "stages": list(dict.fromkeys(sample[2] for sample in samples)),
        "video": str(video),
        "thumbnail": str(cover),
        "thumbnail_time": thumbnail_time,
        "media": probe,
    }
    (output / "Capture.json").write_text(json.dumps(metadata, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(metadata, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
