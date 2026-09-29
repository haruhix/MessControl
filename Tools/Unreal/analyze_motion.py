"""Compare rendered bones with animation and Chaos in the rotating actor's space.

Steps are normalized to 1/60 second. Jump, ragdoll and swallow need separate
criteria; these measurements are not universal pass limits.
"""
import argparse
import csv
import json
import math
import statistics
from collections import defaultdict
from pathlib import Path


def vector(row, prefix=""):
    return tuple(float(row[prefix + axis]) for axis in "xyz")


def quat(row, prefix=""):
    q = tuple(float(row.get(prefix + "q" + axis, axis == "w")) for axis in "xyzw")
    n = math.sqrt(sum(x*x for x in q))
    return tuple(x/n for x in q) if n else (0, 0, 0, 1)


def multiply(a, b):
    x, y, z, w = a
    X, Y, Z, W = b
    return (w*X+x*W+y*Z-z*Y, w*Y-x*Z+y*W+z*X,
            w*Z+x*Y-y*X+z*W, w*W-x*X-y*Y-z*Z)


def conjugate(q):
    return (-q[0], -q[1], -q[2], q[3])


def local_point(row, prefix=""):
    p = tuple(a-b for a, b in zip(vector(row, prefix), vector(row, "actor_")))
    q = quat(row, "actor_")
    return multiply(multiply(conjugate(q), (*p, 0)), q)[:3]


def stats(values):
    values = sorted(values)
    if not values:
        return {"max": 0, "p95": 0, "p99": 0, "median": 0}
    return {"max": round(values[-1], 4),
            "p95": round(values[min(len(values)-1, int(len(values)*.95))], 4),
            "p99": round(values[min(len(values)-1, int(len(values)*.99))], 4),
            "median": round(statistics.median(values), 4)}


def summarize(path, idle_only=False, start=0, end=math.inf):
    groups = defaultdict(list)
    with path.open(encoding="utf-8-sig", newline="") as stream:
        for row in csv.DictReader(stream):
            if not start <= float(row["time"]) <= end:
                continue
            if idle_only and (float(row["speed"]) > 1 or int(row["state"]) != 0):
                continue
            groups[(row.get("stage", "gameplay"), row["bone"])].append(row)
    results = defaultdict(dict)
    for (stage, bone), rows in groups.items():
        steps, spins, errors, target_steps, residual_steps = [], [], [], [], []
        invalid, discontinuities, switches = 0, 0, 0
        last = None
        for row in rows:
            p, target = local_point(row), local_point(row, "target_")
            q = multiply(conjugate(quat(row, "actor_")), quat(row))
            residual = tuple(a-b for a, b in zip(p, target))
            if not all(math.isfinite(v) for v in (*p, *target, *q)):
                invalid += 1
                last = None
                continue
            errors.append(math.dist(p, target))
            if last:
                old, lp, lt, lq, lr = last
                dt = float(row["time"]) - float(old["time"])
                teleport = math.dist(vector(row, "actor_"), vector(old, "actor_")) > 200
                consecutive = 0 < dt < max(.15, float(row["dt"])*1.5)
                if teleport or not consecutive:
                    discontinuities += 1
                else:
                    steps.append(math.dist(p, lp)/dt/60)
                    target_steps.append(math.dist(target, lt)/dt/60)
                    residual_steps.append(math.dist(residual, lr)/dt/60)
                    cosine = min(1, abs(sum(a*b for a, b in zip(q, lq))))
                    spins.append(math.degrees(2*math.acos(cosine))/dt/60)
                    switches += bone.startswith("foot_") and row["foot_hit"] != old["foot_hit"]
            last = row, p, target, q, residual
        results[stage][bone] = dict(frames=len(rows), invalid=invalid,
            discontinuities=discontinuities, step_cm_per_60hz=stats(steps),
            target_step_cm_per_60hz=stats(target_steps),
            physics_residual_step_cm_per_60hz=stats(residual_steps),
            rotation_deg_per_60hz=stats(spins), animation_error_cm=stats(errors),
            ground_hit_switches=switches)
    return {"file": str(path), "idle_only": idle_only, "stages": dict(results)}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("files", nargs="+", type=Path)
    parser.add_argument("--idle-only", action="store_true")
    parser.add_argument("--start", type=float, default=0)
    parser.add_argument("--end", type=float, default=math.inf)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    report = json.dumps([summarize(p, args.idle_only, args.start, args.end) for p in args.files], indent=2)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(report, encoding="utf-8")
        print(args.output)
    else:
        print(report)
