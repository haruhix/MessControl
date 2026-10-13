"""Encode the separate genuine entry/return windows of the owned Kenney PIE proof.

All written sequence frames are retained, including the longer recorded interval
across any missing screenshot requests. CFR output repeats captured images rather
than interpolating new imagery. The last image is held for the median requested
capture interval; this presentation hold is explicit and adds no source timestamp.
"""

import argparse
import json
import math
from pathlib import Path
import shutil
import statistics
import subprocess


def run(arguments, capture=False):
    return subprocess.run(arguments, check=True, capture_output=capture,
                          text=capture, encoding="utf-8" if capture else None)


def concat_file(path):
    return "file '" + path.as_posix().replace("'", "'\\''") + "'"


def encode_clip(name, rows, output, ffmpeg, ffprobe):
    if len(rows) < 2:
        raise ValueError(f"{name}: at least two requested samples are required")
    times = [float(row["time"]) for row in rows]
    if any(not math.isfinite(value) for value in times):
        raise ValueError(f"{name}: non-finite source timestamp")
    if any(b <= a for a, b in zip(times, times[1:])):
        raise ValueError(f"{name}: requested game timestamps must increase")

    present, missing = [], []
    for row in rows:
        source = Path(row["file"])
        if not source.is_absolute():
            raise ValueError(f"{name}: source paths must be absolute")
        if source.is_file():
            present.append((row, source.resolve()))
        else:
            missing.append(row)
    if len(present) < 2:
        raise ValueError(f"{name}: fewer than two written images")
    source_times = [float(row["time"]) for row, _ in present]
    end_hold = statistics.median(b-a for a, b in zip(times, times[1:]))
    durations = [b-a for a, b in zip(source_times, source_times[1:])] + [end_hold]
    target_duration = sum(durations)
    manifest = output / (name + ".ffconcat")
    lines = ["ffconcat version 1.0"]
    for (_, path), duration in zip(present, durations):
        lines.extend((concat_file(path), f"duration {duration:.9f}"))
    # The final duplicate allows the concat demuxer to honour its duration.
    lines.append(concat_file(present[-1][1]))
    manifest.write_text("\n".join(lines) + "\n", encoding="utf-8")

    video = output / (name + ".mp4")
    run([ffmpeg, "-y", "-hide_banner", "-loglevel", "warning",
         "-f", "concat", "-safe", "0", "-i", str(manifest),
         "-vf", "scale=1280:720:force_original_aspect_ratio=decrease,pad=1280:720:(ow-iw)/2:(oh-ih)/2,fps=30",
         "-c:v", "libx264", "-preset", "fast", "-crf", "20",
         "-pix_fmt", "yuv420p", "-threads", "4", "-an",
         "-t", f"{target_duration:.9f}",
         "-movflags", "+faststart", str(video)])
    run([ffmpeg, "-hide_banner", "-loglevel", "error", "-i", str(video),
         "-f", "null", "-"])
    media = json.loads(run([ffprobe, "-v", "error", "-show_streams",
                           "-show_format", "-of", "json", str(video)], True).stdout)
    stream = next(item for item in media["streams"] if item["codec_type"] == "video")
    if (stream["codec_name"], stream["width"], stream["height"]) != ("h264", 1280, 720):
        raise ValueError(f"{name}: unexpected output format")
    encoded_duration = float(media["format"]["duration"])
    if abs(encoded_duration-target_duration) > 1./30.+.001:
        raise ValueError(f"{name}: output duration differs by more than one output frame")
    posters = []
    for suffix, index in (("Start", 0), ("Midway", len(present)//2),
                          ("Complete", len(present)-1)):
        row, path = present[index]
        poster = output / (name + suffix + ".png")
        shutil.copyfile(path, poster)
        posters.append(dict(file=str(poster), source_file=str(path), game_seconds=row["time"]))
    return dict(video=str(video), concat_manifest=str(manifest),
                requested=len(rows), available=len(present), missing_requests=missing,
                source_start_game_seconds=source_times[0],
                source_end_game_seconds=source_times[-1],
                source_intervals_preserved_seconds=source_times[-1]-source_times[0],
                final_image_hold_seconds=end_hold,
                final_hold_policy="Median requested capture interval; a presentation hold, not a source timestamp.",
                target_presentation_duration_seconds=target_duration,
                duration_quantization_seconds=encoded_duration-target_duration,
                requested_missing_initial_seconds=source_times[0]-times[0],
                requested_missing_final_seconds=times[-1]-source_times[-1],
                encoded_duration_seconds=encoded_duration,
                encoded_frame_count=int(stream["nb_frames"]),
                codec=stream["codec_name"], width=stream["width"], height=stream["height"],
                full_decode="PASS", posters=posters,
                source_sequence=[dict(file=str(path), game_seconds=row["time"],
                                      held_duration_seconds=duration)
                                 for (row, path), duration in zip(present, durations)])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", type=Path,
                        default=Path("Saved/Checks/KenneySnowPowderSolo/Report.json"))
    parser.add_argument("--output", type=Path,
                        default=Path("Artifacts/KenneySnowPowder_20261013"))
    parser.add_argument("--ffmpeg", default="C:/ffmpeg/ffmpeg.exe")
    parser.add_argument("--ffprobe", default="C:/ffmpeg/ffprobe.exe")
    args = parser.parse_args()
    source = args.report.resolve(strict=True)
    proof = json.loads(source.read_text(encoding="utf-8-sig"))
    if proof.get("status") != "passed" or proof.get("passed") is not True:
        raise ValueError("Only a completed passed native PIE report may be encoded")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    observed_intervals = [float(b["time"])-float(a["time"])
        for rows in proof["clips"].values() for a, b in zip(rows, rows[1:])
        if float(b["time"]) > float(a["time"])]
    median_interval = statistics.median(observed_intervals)
    configured_hz = proof.get("capture_hz")
    if configured_hz is None and isinstance(proof.get("capture_plan"), dict):
        configured_hz = proof["capture_plan"].get("capture_hz")
    report = dict(source_report=str(source), native_report_passed=True,
                  timing="Separate real game-time windows; all written sequence images included. No interpolation or time scaling.",
                  timing_quantization="Encoded 30 fps presentation repeats captured images; source timing remains recorded in manifest. Output timestamps are quantized by image demuxing and video frame cadence.",
                  source_capture_hz=configured_hz if configured_hz is not None else round(1./median_interval, 6),
                  source_capture_cadence_evidence=dict(configured_hz=configured_hz,
                    median_requested_interval_game_seconds=median_interval,
                    observed_median_hz=1./median_interval,
                    basis="Report configuration when present; otherwise observed median within-window requested game-time cadence."),
                  windows={})
    for window, title in (("entry", "KenneySnowExpansion"),
                          ("return", "KenneySnowReturn")):
        report["windows"][window] = encode_clip(title, proof["clips"][window], output,
                                                args.ffmpeg, args.ffprobe)
    review_names = {"05_native_reverse_radial_front.png",
                    "06_reverse_complete_local_aura_and_slick_ice.png"}
    report["exact_review_stills"] = []
    for row in proof.get("screenshots", []):
        path = Path(row["file"])
        if path.name in review_names and path.is_file():
            target = output / path.name
            shutil.copyfile(path, target)
            report["exact_review_stills"].append(dict(file=str(target),
                source_file=str(path), game_seconds=row["time"]))
    report["decode_passed"] = True
    manifest = output / "VideoSourceReport.json"
    manifest.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    summary = dict(manifest=str(manifest), videos={window: {key: clip[key]
                   for key in ("video", "requested", "available", "encoded_duration_seconds",
                               "encoded_frame_count", "full_decode")}
                   for window, clip in report["windows"].items()})
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
