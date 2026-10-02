"""Pure Python contracts for saved Unreal food-collision authoring evidence."""
from __future__ import annotations

import hashlib
import json
import math
from pathlib import Path


def json_sha256(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True).encode("utf8")).hexdigest()


def file_sha256(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def package_file(content_dir, package):
    if not package.startswith("/Game/") or ".." in package.split("/"):
        raise ValueError("Not a project package: " + package)
    content = Path(content_dir).resolve()
    result = (content / (package[6:] + ".uasset")).resolve()
    if not result.is_relative_to(content):
        raise ValueError("Package escaped project Content: " + package)
    return result


def validate_plan(plan, rows, references, selected):
    """Reject incomplete, stale or partial-scope plans before any package writes."""
    if plan.get("complete") is not True:
        raise ValueError("Collision plan is incomplete; regenerate from a complete native audit")
    expected_scope = sorted(references)
    if plan.get("scope") != expected_scope or plan.get("scope_sha256") != json_sha256(expected_scope):
        raise ValueError("Collision plan scope differs from the current saved food menu")
    if plan.get("saved_table_rows_sha256") != json_sha256(rows):
        raise ValueError("Saved food menu rows/scales changed since planning; audit and regenerate")
    entries = plan.get("meshes", [])
    plans = {entry["path"].split(".")[0]: entry for entry in entries}
    if len(plans) != len(entries) or set(plans) != set(expected_scope):
        raise ValueError("Collision plan must contain exactly one entry for every current food mesh")
    for package in selected:
        entry = plans[package]
        if entry.get("table_uses") != references[package]:
            raise ValueError("Collision plan table uses differ for " + package)
        if not entry.get("source_render_sha256"):
            raise ValueError("Collision plan lacks a native render hash for " + package)
        if entry.get("method") not in ("source_convex", "hybrid_components", "native_vhacd"):
            raise ValueError("Unresolved collision authoring method for " + package)
        if entry.get("method") in ("source_convex", "hybrid_components"):
            if not entry.get("convex_elems"):
                raise ValueError("Collision plan has no source hulls for " + package)
            for hull in entry["convex_elems"]:
                validate_hull(hull)
    return plans


def validate_hull(hull):
    vertices = hull.get("vertex_data", [])
    indices = hull.get("index_data", [])
    if len(vertices) < 4 or any(len(point) != 3 or not all(math.isfinite(float(v)) for v in point) for point in vertices):
        raise ValueError("Invalid or lower-dimensional source convex vertices")
    if len(indices) % 3 or not indices or any(not isinstance(i, int) or not 0 <= i < len(vertices) for i in indices):
        raise ValueError("Invalid source convex triangle indices")
    transform = hull.get("transform", {})
    if transform and (transform.get("translation", [0, 0, 0]) != [0, 0, 0]
                      or transform.get("scale", [1, 1, 1]) != [1, 1, 1]
                      or transform.get("rotationQuat", [0, 0, 0, 1]) != [0, 0, 0, 1]):
        raise ValueError("Source import expects identity element transforms; bake other transforms first")


def validate_resume(previous, selected, scope_hash, table_hash, plan_hash, content_dir):
    """A saved path alone cannot prove that a package still contains authored hulls."""
    if previous.get("mode") != "build":
        raise ValueError("Resume input must be a native build report")
    for name, expected in (("scope_sha256", scope_hash), ("saved_table_rows_sha256", table_hash), ("source_plan_sha256", plan_hash)):
        if previous.get(name) != expected:
            raise ValueError("Resume evidence is stale or lacks " + name + "; run a fresh build")
    if previous.get("selected_scope") != selected:
        raise ValueError("Resume request differs from the original selected scope")
    completed = set(previous.get("saved_packages", []))
    records = {item["path"].split(".")[0]: item for item in previous.get("meshes", [])}
    if not completed <= set(selected):
        raise ValueError("Resume report contains packages outside this authoring request")
    for package in completed:
        record = records.get(package, {})
        expected = record.get("saved_asset_sha256")
        if record.get("error") or not expected:
            raise ValueError("Resume lacks successful saved-package evidence for " + package)
        if file_sha256(package_file(content_dir, package)) != expected:
            raise ValueError("Saved package changed after the resume checkpoint: " + package)
    return completed, [records[package] for package in sorted(completed)]


def raw_vertex_roundtrip(expected_hulls, actual_hulls):
    """Measure the reflected import/export round trip; this is not a Chaos cook audit."""
    if len(expected_hulls) != len(actual_hulls):
        raise ValueError("Native saved convex count differs from the source plan")
    maximum = 0.0
    worst = None
    counts = []
    for hull_index, (expected, actual) in enumerate(zip(expected_hulls, actual_hulls)):
        a, b = expected["vertex_data"], actual.get("vertex_data", [])
        if len(a) != len(b):
            raise ValueError("Native convex vertex count differs for hull " + str(hull_index))
        counts.append(len(a))
        for point_index, (left, right) in enumerate(zip(a, b)):
            error = math.sqrt(sum((float(x) - float(y)) ** 2 for x, y in zip(left, right)))
            if error > maximum:
                maximum = error
                worst = {"hull": hull_index, "vertex": point_index, "source": left, "native_export": right}
    return {"coordinate_space": "asset-local centimetres", "maximum_vertex_error_cm": maximum,
            "hull_count": len(counts), "total_vertices": sum(counts), "worst_vertex": worst,
            "evidence": "reflected FKConvexElem after mesh Build; actual Chaos pointer is not exposed to Python",
            "actual_native_cooked_geometry_verified": False}
