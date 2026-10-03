"""Encode two genuine Unreal clips from game-clock render-target frames."""
import csv
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
NAME = sys.argv[1]
if not re.fullmatch(r"[A-Za-z0-9_-]+", NAME):
    raise ValueError("Invalid recording name")
FOLDER = ROOT / "Saved" / "ApprovalFrames" / NAME
OUT = ROOT / "Artifacts" / NAME
OUT.mkdir(parents=True, exist_ok=True)
FFMPEG = "C:/ffmpeg/ffmpeg.exe"
FFPROBE = "C:/ffmpeg/ffprobe.exe"

rows = []
for filename, stamp in csv.reader((FOLDER / "times.csv").read_text(encoding="utf-8-sig").splitlines()):
    if (FOLDER / filename).is_file():
        rows.append((filename, float(stamp)))
record_log = FOLDER / "DashSuction1.log"
if not record_log.is_file():
    record_log.write_bytes((ROOT / "Saved" / "Logs" / "DashSuction1.log").read_bytes())
log = record_log.read_text(encoding="utf-8", errors="replace")
match = re.search(r"MC_DASH_VIDEO_START.*record_time=([0-9.]+)", log)
if not match:
    raise RuntimeError("The footage has no measured scenario start")
epoch = float(match[1])
if "MC_VALIDATION_PASS DASH_SUCTION" not in log:
    raise RuntimeError("The recorded scenario did not pass its gameplay and pose checks")

recordings = []
for stem, start, end, caption in [
    ("Dash_Shift", .35, 5.0, "Shift: короткое нажатие — рывок, удержание — бег"),
    ("Food_Suction", 7.5, 14.3, "Поглощение: общий поток, притяжение по расстоянию, реакция лица"),
]:
    selected = [(filename, stamp) for filename, stamp in rows if epoch + start <= stamp <= epoch + end]
    if len(selected) < 25:
        raise RuntimeError(f"Too few genuine rendered frames for {stem}: {len(selected)}")
    concat = []
    for index, (filename, stamp) in enumerate(selected):
        duration = selected[index + 1][1] - stamp if index + 1 < len(selected) else 1 / 30
        if not 0 < duration < .35:
            raise RuntimeError(f"Recording hitch of {duration:.3f}s in {stem}")
        concat.extend([f"file '{filename}'", f"duration {duration:.6f}"])
    concat.append(f"file '{selected[-1][0]}'")
    concat_path = FOLDER / (stem + ".ffconcat")
    concat_path.write_text("\n".join(concat), encoding="utf-8")
    (FOLDER / (stem + "_caption.txt")).write_text(caption, encoding="utf-8")
    vf = (
        "fps=30,drawbox=x=0:y=ih-54:w=iw:h=54:color=black@0.68:t=fill,"
        "drawtext=fontfile='C\\:/Windows/Fonts/arial.ttf':"
        f"textfile='{stem}_caption.txt':fontcolor=white:fontsize=23:x=20:y=h-38"
    )
    video = OUT / (stem + ".mp4")
    subprocess.run([
        FFMPEG, "-hide_banner", "-loglevel", "error", "-y", "-f", "concat", "-safe", "0",
        "-i", str(concat_path), "-vf", vf, "-c:v", "libx264", "-threads", "2", "-crf", "18",
        "-pix_fmt", "yuv420p", "-movflags", "+faststart", str(video),
    ], cwd=FOLDER, check=True)
    info = json.loads(subprocess.check_output([
        FFPROBE, "-v", "error", "-show_entries", "format=duration:stream=width,height,r_frame_rate",
        "-of", "json", str(video),
    ], text=True))
    duration = float(info["format"]["duration"])
    if duration < end - start - .25:
        raise RuntimeError(f"Truncated video: {video}")
    subprocess.run([
        FFMPEG, "-hide_banner", "-loglevel", "error", "-y", "-ss", "1.5", "-i", str(video),
        "-frames:v", "1", "-update", "1", str(OUT / (stem + "_VideoPreview.png")),
    ], check=True)
    recordings.append(dict(file=str(video), frames=len(selected), seconds=duration,
                           scenario_start=start, scenario_end=end,
                           source="Unreal game render target; game timestamps preserve normal gameplay speed",
                           capture_name=NAME, capture_time_scale=.25, players=4, audio=False, probe=info))
    print(video)
(OUT / "DashSuction_VideoRecording.json").write_text(json.dumps(recordings, ensure_ascii=False, indent=2), encoding="utf-8")
