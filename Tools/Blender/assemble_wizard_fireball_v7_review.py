"""Assemble completed native-timed V7 renders; does not alter source art."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import subprocess

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "Artifacts/WizardPerformanceV7"
VIDEO = OUT / "Wizard_Fireball_V7.mp4"
POSTER = OUT / "Wizard_Fireball_V7_Poster.png"
FFMPEG = Path("C:/ffmpeg/ffmpeg.exe")
FFPROBE = Path("C:/ffmpeg/ffprobe.exe")
VIEWS = ("Front", "ThreeQuarter", "Side")
FPS = 30
SIZE = (1440, 720)
TITLE_FONT = ImageFont.truetype("C:/Windows/Fonts/seguisb.ttf", 29)
TEXT_FONT = ImageFont.truetype("C:/Windows/Fonts/segoeui.ttf", 20)
LABEL_FONT = ImageFont.truetype("C:/Windows/Fonts/seguisb.ttf", 20)
SMALL_FONT = ImageFont.truetype("C:/Windows/Fonts/segoeui.ttf", 16)


def validate_renders():
    manifest = json.loads((OUT / "RenderManifest.json").read_text(encoding="utf-8"))
    assert manifest["fps"] == FPS
    assert len(manifest["frames"]) == 162
    assert hashlib.sha256(Path(manifest["source"]).read_bytes()).hexdigest() == manifest["sha256"]
    for view in VIEWS:
        entries = [item for item in manifest["frames"] if item["view"] == view]
        assert [item["index"] for item in entries] == list(range(54))
        assert abs(entries[27]["native_seconds"] - .9) < 1e-8
        assert abs(entries[27]["source_frame"] - 56) < 1e-8
        for item in entries:
            path = OUT / view / f"frame_{item['index']:04d}.png"
            with Image.open(path) as im:
                assert im.size == (480, 540), (path, im.size)
                im.verify()
    return manifest


def centered(draw, value, x, y, font, fill):
    bounds = draw.textbbox((0, 0), value, font=font)
    draw.text((x - (bounds[2]-bounds[0])/2, y), value, font=font, fill=fill)


def composite(index, caption):
    canvas = Image.new("RGB", SIZE, "#111c28")
    draw = ImageDraw.Draw(canvas)
    draw.text((26, 13), "FIREBALL CAST  /  V7", font=TITLE_FONT, fill="#f0f4f8")
    draw.text((26, 52), "Native timing: 1.8 s per cast  |  Release: 0.9 s", font=TEXT_FONT, fill="#a8bacd")
    for column, view in enumerate(VIEWS):
        centered(draw, {"ThreeQuarter": "Three-quarter"}.get(view, view), column*480+240, 80, LABEL_FONT, "#e1eaf3")
        with Image.open(OUT / view / f"frame_{index:04d}.png") as im:
            canvas.paste(im.convert("RGB"), (column*480, 104))
    draw = ImageDraw.Draw(canvas)
    draw.line((480, 104, 480, 644), fill="#24384b", width=2)
    draw.line((960, 104, 960, 644), fill="#24384b", width=2)
    draw.text((26, 657), caption, font=LABEL_FONT, fill="#f0f4f8")
    native_seconds = index/FPS
    phase = "TELL / CHARGE" if native_seconds < .9 else "RELEASE" if index == 27 else "RECOVERY"
    draw.text((26, 686), f"{native_seconds:.2f} s native  ·  {phase}", font=SMALL_FONT, fill="#a8bacd")
    x0, x1, y = 680, 1375, 678
    middle = (x0+x1)/2
    draw.line((x0, y, middle, y), fill="#3c81aa", width=6)
    draw.line((middle, y, x1, y), fill="#b77a37", width=6)
    progress = x0+(x1-x0)*native_seconds/1.8
    draw.ellipse((progress-6, y-6, progress+6, y+6), fill="#f0f4f8")
    draw.line((middle, y-11, middle, y+11), fill="#f0f4f8", width=2)
    centered(draw, "0", x0, 691, SMALL_FONT, "#a8bacd")
    centered(draw, "release 0.9", middle, 691, SMALL_FONT, "#cbd7e3")
    centered(draw, "1.8 s", x1, 691, SMALL_FONT, "#a8bacd")
    return canvas


def main():
    manifest = validate_renders()
    sequence = [(0, "READY", 9)]
    for cycle in (1, 2):
        sequence += [(index, f"1× speed  ·  Cast {cycle} / 2", 1) for index in range(54)]
    sequence += [(index, "0.5× speed  ·  Slow review", 2) for index in range(54)]
    frames = sum(repeats for _, _, repeats in sequence)
    cmd = [str(FFMPEG), "-hide_banner", "-loglevel", "error", "-y",
           "-f", "rawvideo", "-pix_fmt", "rgb24", "-s:v", "1440x720", "-r", str(FPS), "-i", "pipe:0",
           "-an", "-c:v", "libx264", "-preset", "medium", "-crf", "18", "-pix_fmt", "yuv420p",
           "-movflags", "+faststart", str(VIDEO)]
    process = subprocess.Popen(cmd, stdin=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        for index, caption, repeats in sequence:
            pixels = composite(index, caption).tobytes()
            for _ in range(repeats):
                process.stdin.write(pixels)
        process.stdin.close()
        error = process.stderr.read().decode("utf-8", errors="replace")
        assert process.wait() == 0, error
    except Exception:
        process.kill()
        raise
    composite(21, "Loaded pose  ·  Native cast review").save(POSTER)
    probe = json.loads(subprocess.check_output([str(FFPROBE), "-v", "error", "-count_frames",
        "-select_streams", "v:0", "-show_entries", "stream=width,height,r_frame_rate,nb_read_frames,duration",
        "-show_entries", "format=duration", "-of", "json", str(VIDEO)], text=True))
    stream = probe["streams"][0]
    assert stream["width"] == SIZE[0] and stream["height"] == SIZE[1]
    assert stream["r_frame_rate"] == "30/1"
    assert int(stream["nb_read_frames"]) == frames
    assert abs(float(probe["format"]["duration"]) - frames/FPS) < .04
    report = dict(video=str(VIDEO), poster=str(POSTER), rendered_frames=162, output_frames=frames,
                  duration_seconds=frames/FPS, fps=FPS, size=list(SIZE), source_sha256=manifest["sha256"],
                  sequence="0.3-second Ready intro, two native 1.8-second casts at 1×, one cast at 0.5×",
                  release_native_seconds=.9, release_source_frame=56, probe=probe)
    (OUT / "VideoReviewManifest.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
