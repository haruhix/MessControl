"""Encode three real Unreal PNG captures at 1x, then replay the same frames at 0.5x.

Example: python Tools/BuildAnimationComparisonVideo.py --input-dir Artifacts/AnimationComparison_20261010
Input folders: Native, StylizedDynamics, KawaiiPhysics; consecutive numbered PNGs.
The supplied --fps is the original capture rate, not a motion-interpolation rate.
"""
import argparse
from datetime import datetime, timezone
from fractions import Fraction
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import subprocess


MODES = ("Native", "StylizedDynamics", "KawaiiPhysics")
FONT = "C:/Windows/Fonts/arial.ttf"


def png_dimensions(path):
    with path.open("rb") as source:
        header = source.read(24)
    if len(header) != 24 or header[:8] != b"\x89PNG\r\n\x1a\n" or header[12:16] != b"IHDR":
        raise ValueError("Invalid PNG header: " + str(path))
    return struct.unpack(">II", header[16:24])


def capture_info(root, mode, fps):
    folder = (root / mode).resolve(strict=True)
    if folder.parent != root:
        raise ValueError("Mode captures must be immediate children of --input-dir")
    frames = sorted(folder.glob("*.png"), key=lambda path: path.name)
    if len(frames) < 2:
        raise ValueError(mode + " needs at least two captured PNG frames")
    if any(re.fullmatch(r"\d{4,}\.png", path.name) is None for path in frames):
        raise ValueError(mode + " contains PNGs outside the numbered capture sequence")
    numbers = [int(path.stem) for path in frames]
    if numbers != list(range(numbers[0], numbers[0] + len(numbers))):
        raise ValueError(mode + " contains missing, duplicate, or unordered frames")
    padding = len(frames[0].stem)
    if any(path.name != f"{number:0{padding}d}.png" for path, number in zip(frames, numbers)):
        raise ValueError(mode + " must use one consistent numbered PNG pattern")
    dimensions = png_dimensions(frames[0])
    if any(png_dimensions(frame) != dimensions for frame in frames):
        raise ValueError(mode + " contains inconsistent frame dimensions")
    width, height = dimensions
    if width <= 0 or height <= 0 or width % 2 or height % 2:
        raise ValueError("Capture dimensions must be positive and even for H.264 yuv420p")
    return {"mode": mode, "folder": str(folder), "pattern": str(folder / f"%0{padding}d.png"),
        "first_frame": numbers[0], "last_frame": numbers[-1], "frame_count": len(frames),
        "width": width, "height": height, "original_input_rate_fps": fps,
        "original_input_duration_seconds": len(frames) / fps}


def font_filter_path(path):
    # One filter argument, passed directly to subprocess without a shell.
    return str(path).replace("\\", "/").replace(":", "\\:").replace("'", "\\'")


def text_filter(text, y, size=20):
    return (f"drawtext=fontfile='{font_filter_path(FONT)}':text='{text}':"
            f"fontcolor=white:fontsize={size}:x=(w-tw)/2:y={y}")


def phase_caption(label):
    return ("drawbox=x=0:y=ih-68:w=iw:h=68:color=black@0.75:t=fill," +
        text_filter("CURRENT GAMEPLAY SETTINGS - mild secondary motion", "h-59", 18) + "," +
        text_filter(label, "h-32", 22))


def encode(args):
    folder = args.input_dir.resolve(strict=True)
    if not folder.is_dir():
        raise ValueError("--input-dir must be a directory")
    if not math.isfinite(args.fps) or not 0 < args.fps <= 240:
        raise ValueError("--fps must be a finite original capture rate between 0 and 240")
    for tool in (args.ffmpeg, args.ffprobe, FONT):
        if not Path(tool).is_file():
            raise FileNotFoundError(tool)
    captures = [capture_info(folder, mode, args.fps) for mode in MODES]
    shared = {(row["width"], row["height"], row["frame_count"]) for row in captures}
    if len(shared) != 1:
        raise ValueError("All three modes must have identical dimensions and frame counts")
    width, height, count = next(iter(shared))
    capture_manifest = folder / "Capture.json"
    capture_metadata = None
    capture_cadence = {}
    if capture_manifest.is_file():
        capture_metadata = json.loads(capture_manifest.read_text(encoding="utf-8-sig"))
        if capture_metadata.get("status") != "complete" or capture_metadata.get("error"):
            raise ValueError("Capture.json must record a complete, successful capture")
        if abs(float(capture_metadata["source_fps"]) - args.fps) > .001:
            raise ValueError("--fps must match Capture.json source_fps, not game_fps")
        if "frames" in capture_metadata:
            for mode in MODES:
                rows = [row for row in capture_metadata["frames"] if row["mode"] == mode]
                if len(rows) != count:
                    raise ValueError(mode + " manifest count differs from the PNG sequence")
                times = [float(row["game_time"]) for row in rows]
                if not all(math.isfinite(value) for value in times) or any(b <= a for a, b in zip(times, times[1:])):
                    raise ValueError(mode + " capture timestamps must be finite and increasing")
                observed_fps = (len(times) - 1) / (times[-1] - times[0])
                if abs(observed_fps - args.fps) > .01:
                    raise ValueError(mode + " recorded cadence differs from the original --fps")
                capture_cadence[mode] = {"observed_input_fps": observed_fps,
                    "first_game_time": times[0], "last_game_time": times[-1]}
    normal_seconds = count / args.fps
    slow_seconds = 2 * normal_seconds
    total_seconds = normal_seconds + slow_seconds
    output_dir = args.output_dir.resolve() if args.output_dir else folder
    output_dir.mkdir(parents=True, exist_ok=True)
    video = output_dir / "AnimationComparison.mp4"
    poster = output_dir / "AnimationComparison_Poster.png"
    partial_video = output_dir / "AnimationComparison.partial.mp4"
    partial_poster = output_dir / "AnimationComparison_Poster.partial.png"

    command = [args.ffmpeg, "-y", "-hide_banner", "-loglevel", "warning"]
    filters = []
    for index, row in enumerate(captures):
        command.extend(["-framerate", str(args.fps), "-start_number", str(row["first_frame"]), "-i", row["pattern"]])
        filters.append(f"[{index}:v]settb=AVTB,setpts=PTS-STARTPTS,"
            "drawbox=x=0:y=0:w=iw:h=44:color=black@0.70:t=fill," +
            text_filter(row["mode"], "10", 24) + f"[panel{index}]")
    filters.extend([
        "[panel0][panel1][panel2]hstack=inputs=3:shortest=1,split=2[normal_source][slow_source]",
        "[normal_source]" + phase_caption("1x NORMAL PLAYBACK") + "[normal]",
        f"[slow_source]setpts=2*(PTS-STARTPTS),fps={args.fps},"
            f"tpad=stop_mode=clone:stop_duration=0.1,trim=duration={slow_seconds:.9f},setpts=PTS-STARTPTS," +
            phase_caption("0.5x SLOW REPLAY - same captured frames") + "[slow]",
        "[normal][slow]concat=n=2:v=1:a=0,format=yuv420p[out]",
    ])
    command.extend(["-filter_complex", ";".join(filters), "-map", "[out]", "-an",
        "-r", str(args.fps), "-fps_mode", "cfr", "-t", f"{total_seconds:.9f}",
        "-c:v", "libx264", "-preset", "fast", "-crf", "18", "-threads", "4",
        "-movflags", "+faststart", str(partial_video)])
    subprocess.run(command, check=True)
    result = subprocess.run([args.ffprobe, "-v", "error", "-count_frames", "-show_format",
        "-show_streams", "-of", "json", str(partial_video)], check=True, capture_output=True, text=True)
    media = json.loads(result.stdout)
    streams = [stream for stream in media["streams"] if stream.get("codec_type") == "video"]
    if len(streams) != 1:
        raise ValueError("Expected one encoded video stream")
    stream = streams[0]
    output_frames = int(stream["nb_read_frames"])
    if (stream["width"], stream["height"]) != (3 * width, height) or output_frames != 3 * count:
        raise ValueError("Encoded dimensions/frame count differ from the three-panel 1x + 0.5x timeline")
    output_fps = float(Fraction(stream["avg_frame_rate"]))
    if abs(output_fps - args.fps) > .001 or abs(float(media["format"]["duration"]) - total_seconds) > 1 / args.fps + .001:
        raise ValueError("Encoded playback rate or duration differs from the requested timeline")
    subprocess.run([args.ffmpeg, "-y", "-hide_banner", "-loglevel", "warning", "-ss",
        f"{normal_seconds / 2:.6f}", "-i", str(partial_video), "-frames:v", "1", "-update", "1",
        str(partial_poster)], check=True)
    if png_dimensions(partial_poster) != (3 * width, height):
        raise ValueError("Poster dimensions differ from the comparison video")
    partial_video.replace(video)
    partial_poster.replace(poster)
    with video.open("rb") as source:
        checksum = hashlib.file_digest(source, "sha256").hexdigest()
    report = {"encoded_utc": datetime.now(timezone.utc).isoformat(), "source": "Unreal SceneCapture2D PNG frames",
        "profile": "Current mild gameplay secondary-animation settings; base animation and physics retained",
        "captures": captures, "capture_rate_provenance": "Original capture rate supplied through --fps; capture scheduling is validated by the caller",
        "timeline": [{"start_seconds": 0., "duration_seconds": normal_seconds, "playback_speed": 1.},
                     {"start_seconds": normal_seconds, "duration_seconds": slow_seconds, "playback_speed": .5}],
        "interpolated_frames": False, "slow_replay": "Original frames repeated at half speed; no optical-flow or motion synthesis",
        "audio": "omitted", "output_width": 3 * width, "output_height": height,
        "output_fps": output_fps, "output_frames": output_frames, "output_duration_seconds": float(media["format"]["duration"]),
        "decoded_frame_count_check": "passed", "video_sha256": checksum, "video": str(video), "poster": str(poster)}
    if capture_metadata is not None:
        report["capture_manifest"] = str(capture_manifest)
        report["capture_metadata"] = capture_metadata
        report["recorded_capture_cadence"] = capture_cadence
        report["capture_rate_provenance"] = "Capture.json source_fps and per-mode game timestamps checked against --fps; game_fps is the distinct simulation rate"
    metadata = output_dir / "AnimationComparison.json"
    metadata.write_text(json.dumps(report, indent=2, ensure_ascii=False, allow_nan=False) + "\n", encoding="utf-8")
    print(json.dumps({"video": str(video), "poster": str(poster), "metadata": str(metadata),
        "seconds": report["output_duration_seconds"], "source_frames_per_mode": count}, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--fps", type=float, default=30., help="Original PNG capture rate; default 30 fps")
    parser.add_argument("--ffmpeg", default="C:/ffmpeg/ffmpeg.exe")
    parser.add_argument("--ffprobe", default="C:/ffmpeg/ffprobe.exe")
    encode(parser.parse_args())


if __name__ == "__main__":
    main()
