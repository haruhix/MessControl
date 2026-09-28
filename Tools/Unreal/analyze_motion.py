"""Summarize mc.Anim.Record CSV files without requiring Unreal or extra packages."""
import argparse
import csv
import json
import math
import statistics
from collections import defaultdict
from pathlib import Path


def vector(row, prefix=""):
    return tuple(float(row[prefix + axis]) for axis in "xyz")


def summarize(path, idle_only=False):
    groups = defaultdict(list)
    with path.open(encoding="utf-8-sig", newline="") as stream:
        for row in csv.DictReader(stream):
            if idle_only and (float(row["speed"]) > 1 or int(row["state"]) != 0):
                continue
            groups[row["bone"]].append(row)
    results = {}
    for bone, rows in groups.items():
        steps, spins, errors, hits = [], [], [], []
        last = None
        for row in rows:
            p = vector(row)
            local = tuple(a - b for a, b in zip(p, vector(row, "actor_")))
            q = tuple(float(row["q" + axis]) for axis in "xyzw")
            length = math.sqrt(sum(v * v for v in q))
            q = tuple(v / length for v in q) if length else (0, 0, 0, 1)
            errors.append(math.dist(p, vector(row, "target_")))
            hits.append(row["foot_hit"])
            if last:
                dt = float(row["time"]) - float(last[0]["time"])
                # Do not bridge gaps introduced by an idle-only selection.
                if 0 < dt < max(.15, float(row["dt"]) * 1.5):
                    steps.append(math.dist(local, last[1]) / dt)
                    cosine = min(1, abs(sum(a * b for a, b in zip(q, last[2]))))
                    spins.append(math.degrees(2 * math.acos(cosine)) / dt)
            last = row, local, q
        results[bone] = dict(
            frames=len(rows),
            peak_relative_speed_cm_s=round(max(steps, default=0), 3),
            peak_angular_speed_deg_s=round(max(spins, default=0), 3),
            median_animation_error_cm=round(statistics.median(errors), 3),
            peak_animation_error_cm=round(max(errors), 3),
            ground_hit_switches=sum(a != b for a, b in zip(hits, hits[1:])),
        )
    return {"file": str(path), "idle_only": idle_only, "bones": results}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("files", nargs="+", type=Path)
    parser.add_argument("--idle-only", action="store_true")
    args = parser.parse_args()
    print(json.dumps([summarize(p, args.idle_only) for p in args.files], indent=2))
