"""Encode real UE screenshot samples using their recorded game timestamps."""

import argparse
import csv
import json
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("frames", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--ffmpeg", default="C:/ffmpeg/ffmpeg.exe")
    parser.add_argument("--ffprobe", default="C:/ffmpeg/ffprobe.exe")
    parser.add_argument("--gap", type=float, default=1.5,
                        help="Cut uncaptured intervals longer than this many game seconds.")
    args = parser.parse_args()
    folder = args.frames.resolve(strict=True)
    with (folder / "FrameTimes.csv").open(encoding="utf-8-sig", newline="") as source:
        rows = list(csv.DictReader(source))
    if len(rows) < 2 or args.gap <= 0:
        raise ValueError("Expected at least two samples and a positive cut threshold")
    times = [float(row["server_time"]) for row in rows]
    if any(b <= a for a, b in zip(times, times[1:])):
        raise ValueError("Capture timestamps must increase")
    images = [(folder / row["frame"]).resolve(strict=True) for row in rows]
    if any(image.parent != folder for image in images):
        raise ValueError("Captured frames must belong to the supplied folder")
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    manifest = folder / "Frames.ffconcat"
    cuts = []
    duration = 0.0
    entries = ["ffconcat version 1.0"]
    for index, image in enumerate(images):
        interval = times[index + 1] - times[index] if index + 1 < len(times) else 0.12
        if interval > args.gap:
            cuts.append({"after": rows[index], "before": rows[index + 1],
                         "removedSeconds": interval - 0.12})
            interval = 0.12
        escaped = image.as_posix().replace("'", "'\\''")
        entries.extend([f"file '{escaped}'", f"duration {interval:.6f}"])
        duration += interval
    escaped = images[-1].as_posix().replace("'", "'\\''")
    entries.append(f"file '{escaped}'")
    manifest.write_text("\n".join(entries) + "\n", encoding="utf-8")
    subprocess.run([args.ffmpeg, "-y", "-hide_banner", "-loglevel", "warning",
                    "-f", "concat", "-safe", "0", "-i", str(manifest),
                    "-vf", "fps=30", "-c:v", "libx264", "-preset", "fast",
                    "-crf", "21", "-pix_fmt", "yuv420p", "-threads", "8",
                    "-movflags", "+faststart", "-an", str(output)], check=True)
    subprocess.run([args.ffmpeg, "-hide_banner", "-loglevel", "error",
                    "-i", str(output), "-f", "null", "-"], check=True)
    probe = subprocess.run([args.ffprobe, "-v", "error", "-show_format",
                            "-show_streams", "-of", "json", str(output)],
                           check=True, capture_output=True, text=True)
    media = json.loads(probe.stdout)
    cover_index = next((i for i, row in enumerate(rows)
                        if row["stage"] == "COLD_BLOCKED_ZONE_AXE"), len(rows) // 2)
    shutil.copyfile(images[cover_index], output.with_name("Cover.png"))
    report = {"sourceTimeline": str(folder / "FrameTimes.csv"),
              "samples": len(rows), "gameStart": times[0], "gameEnd": times[-1],
              "sampledGameSeconds": duration,
              "mediaSeconds": float(media["format"]["duration"]), "cuts": cuts,
              "streams": media["streams"],
              "stages": list(dict.fromkeys(row["stage"] for row in rows)),
              "timing": "Game timestamp intervals preserved; uncaptured idle gaps cut. No time rescaling; audio omitted.",
              "fullDecode": "PASS", "video": str(output)}
    output.with_suffix(".json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
