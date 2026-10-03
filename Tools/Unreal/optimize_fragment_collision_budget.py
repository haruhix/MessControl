"""Plan, then optionally author four bounded near-convex fragment colliders.

Default --mode plan runs outside Unreal and only writes Saved reports. It retains
every native LOD0 source position in one convex element, without vertex reduction.
--mode apply must run in a full Unreal editor via -ExecutePythonScript. It calls
the existing install_source_hulls, verifies saved mesh/render hashes and current
menu scales, backs up each original package, and saves only the four named meshes.
The geometric certificate concerns authored vertices; live Chaos cooking/contact
and before/after frame timings still require the project's runtime tests.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import shutil
import sys
import traceback

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
from food_collision_authoring_guard import (
    file_sha256, json_sha256, package_file, raw_vertex_roundtrip, validate_hull,
)

CANDIDATES = (
    "/Game/Art/Meshes/Breakfast/SM_Egg_03",
    "/Game/Stylized_Fruits/Meshes/SM_Grape_D",
    "/Game/Stylized_Vegetables/Meshes/SM_TomatoSliceC",
    "/Game/Stylized_Vegetables/Meshes/SM_TomatoSliceE",
)
DISTANCE_BUDGET_CM = 0.5
ADDED_VOLUME_BUDGET = 0.01


def write_report(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2), encoding="utf8")


def source_solid_volume(mesh, np):
    # The egg's existing collider already closes a single planar four-edge cut.
    # Close that same boundary for measuring volume, without changing render data.
    from food_collision_components import analyze
    from plan_food_collision import orient_closed_faces
    components = analyze(mesh)["components"]
    if len(components) != 1:
        raise ValueError("Budget candidates must have one connected source solid")
    component = components[0]
    points = np.asarray(component["vertex_data"], float)
    faces = np.asarray(component["index_data"], int).reshape(-1, 3)
    if not component["closed"]:
        if mesh["path"].split(".")[0] != CANDIDATES[0]:
            raise ValueError("Only the existing egg cut may have an open boundary")
        edges = {}
        for face in faces:
            for a, b in ((face[0], face[1]), (face[1], face[2]), (face[2], face[0])):
                key = tuple(sorted((int(a), int(b))))
                edges[key] = edges.get(key, 0) + 1
        boundary = [edge for edge, count in edges.items() if count == 1]
        neighbours = {}
        for a, b in boundary:
            neighbours.setdefault(a, []).append(b)
            neighbours.setdefault(b, []).append(a)
        if len(boundary) != 4 or len(neighbours) != 4 or any(len(v) != 2 for v in neighbours.values()):
            raise ValueError("Egg cut is no longer the known single quad")
        loop = [min(neighbours)]
        while len(loop) < 4:
            loop.append(min(v for v in neighbours[loop[-1]] if v not in loop))
        cut = points[loop]
        _, _, vh = np.linalg.svd(cut - cut.mean(axis=0))
        if abs((cut - cut.mean(axis=0)) @ vh[-1]).max() > 0.0002:
            raise ValueError("Egg cut is no longer planar")
        faces = np.concatenate((faces, [[loop[0], loop[1], loop[2]], [loop[0], loop[2], loop[3]]]))
    oriented = orient_closed_faces(points, faces)
    if oriented is None or oriented[1] <= 0:
        raise ValueError("Source faces do not bound a closed oriented solid")
    return float(oriented[1])


def certify_departure(triangles, parts, item_scale, np, max_depth=16):
    from food_collision_geometry import triangle_distances
    cache = {}
    pending = triangles
    certified_count = queried_count = 0
    maximum_upper = sampled_maximum = 0.0
    for depth in range(max_depth + 1):
        points, mapped = np.unique(pending.reshape(-1, 3), axis=0, return_inverse=True)
        missing = [tuple(point) for point in points if tuple(point) not in cache]
        if missing:
            query = np.asarray(missing)
            distances = np.zeros((len(query), len(parts)))
            for index, part in enumerate(parts):
                outside = (query @ part["normals"].T + part["offsets"]).max(axis=1) > 0.0
                if outside.any():
                    distances[outside, index] = triangle_distances(query[outside], part["triangles"])
            queried_count += len(query)
            sampled_maximum = max(sampled_maximum, float(distances.min(axis=1).max()) * item_scale)
            cache.update(zip(missing, distances))
        distances = np.asarray([cache[tuple(point)] for point in points])
        corners = distances[mapped.reshape(-1, 3)]
        # Distance to a convex solid is a convex function. For any single old
        # convex part, its largest corner distance bounds every point of this
        # triangle. The closest such part therefore bounds distance to the union.
        upper = corners.max(axis=1).min(axis=1) * item_scale + 1e-6
        accepted = upper <= DISTANCE_BUDGET_CM
        if accepted.any():
            maximum_upper = max(maximum_upper, float(upper[accepted].max()))
            certified_count += int(accepted.sum())
        remaining = pending[~accepted]
        if not len(remaining):
            return {
                "passed": True,
                "method": "adaptive triangle bound from convexity of distance-to-each-reference-convex-solid",
                "budget_item_cm": DISTANCE_BUDGET_CM,
                "maximum_certified_upper_bound_item_cm": maximum_upper,
                "maximum_queried_distance_item_cm": sampled_maximum,
                "floating_point_padding_item_cm": 1e-6,
                "certified_triangles": certified_count,
                "unique_queried_points": queried_count,
                "maximum_subdivision_depth": depth,
                "scope": "entire proposed convex boundary versus existing saved convex union; not live Chaos geometry",
            }
        if depth == max_depth or len(remaining) * 4 > 200000:
            raise ValueError("Could not certify every convex face within the conservative distance budget")
        a, b, c = remaining[:, 0], remaining[:, 1], remaining[:, 2]
        ab, bc, ca = (a + b) / 2, (b + c) / 2, (c + a) / 2
        pending = np.concatenate((np.stack((a, ab, ca), axis=1), np.stack((ab, b, bc), axis=1),
                                  np.stack((ca, bc, c), axis=1), np.stack((ab, bc, ca), axis=1)))
    raise AssertionError("Unreachable certificate state")


def plan(args):
    sys.path.insert(0, str(args.dependencies_root.resolve()))
    import numpy as np
    from scipy.spatial import ConvexHull
    from food_collision_geometry import transform_vertices
    source = json.loads(args.snapshot.read_text(encoding="utf8"))
    if source.get("complete") is not True or source.get("errors"):
        raise ValueError("Planning requires a complete successful native audit")
    by_package = {mesh["path"].split(".")[0]: mesh for mesh in source["meshes"]}
    result = {
        "mode": "plan", "complete": False, "saved_packages": [],
        "source_snapshot": str(args.snapshot.resolve()), "source_snapshot_sha256": file_sha256(args.snapshot),
        "selected_scope": list(CANDIDATES), "meshes": [],
        "distance_budget_item_cm": DISTANCE_BUDGET_CM, "added_volume_budget_fraction": ADDED_VOLUME_BUDGET,
        "requires_runtime_validation": ["actual cooked/live shapes", "drop and rest", "same 64-fragment pile before/after timings"],
    }
    for package in CANDIDATES:
        mesh = by_package[package]
        asset = package_file(ROOT / "Content", package)
        if file_sha256(asset) != mesh.get("saved_asset_sha256"):
            raise ValueError("Saved mesh changed since the native audit: " + package)
        uses = mesh["table_uses"]
        if not uses or any(not use.get("fragment") for use in uses):
            raise ValueError("Candidate must be used exclusively as a fragment: " + package)
        scales = [tuple(use["scale"][axis] for axis in ("X", "Y", "Z")) for use in uses]
        if any(min(scale) <= 0 or max(scale) != min(scale) for scale in scales):
            raise ValueError("Certificate requires positive uniform saved item scales")
        item_scale = max(scale[0] for scale in scales)
        points = np.unique(np.asarray([v for section in mesh["native_render_sections"] for v in section["vertices"]]), axis=0)
        convex = ConvexHull(points)
        faces = convex.simplices.copy()
        triangle = points[faces]
        reverse = np.einsum("ij,ij->i", np.cross(triangle[:, 1] - triangle[:, 0], triangle[:, 2] - triangle[:, 0]), convex.equations[:, :3]) < 0
        faces[reverse] = faces[reverse][:, [0, 2, 1]]
        parts = []
        for hull in mesh["native_collision"]["convex_elems"]:
            vertices = transform_vertices(hull["vertex_data"], hull.get("transform"))
            old = ConvexHull(vertices)
            parts.append({"normals": old.equations[:, :3], "offsets": old.equations[:, 3],
                          "triangles": vertices[old.simplices], "volume": float(old.volume)})
        source_volume = source_solid_volume(mesh, np)
        old_volume = sum(part["volume"] for part in parts)
        if abs(old_volume - source_volume) / source_volume > 0.00002:
            raise ValueError("Existing convex volume differs from the closed source solid: " + package)
        added_volume = max(0.0, float(convex.volume / source_volume - 1))
        if added_volume > ADDED_VOLUME_BUDGET:
            raise ValueError("Proposed convex fills too much source volume: " + package)
        certificate = certify_departure(points[faces], parts, item_scale, np)
        element = {"vertex_data": points.tolist(), "index_data": faces.reshape(-1).tolist(),
                   "transform": {"translation": [0, 0, 0], "rotationQuat": [0, 0, 0, 1], "scale": [1, 1, 1]}}
        validate_hull(element)
        entry = {"path": package, "table_uses": uses, "source_asset_sha256": mesh["saved_asset_sha256"],
                 "source_render_sha256": mesh["render_geometry_sha256"], "source_convex_sha256": json_sha256(mesh["native_collision"]["convex_elems"]),
                 "source_hulls": len(parts), "planned_hulls": 1, "retained_source_positions": len(points),
                 "planned_extreme_vertices": len(convex.vertices), "item_scale": item_scale,
                 "source_solid_volume_cm3": source_volume, "reference_convex_volume_sum_cm3": old_volume,
                 "planned_convex_volume_cm3": float(convex.volume), "added_volume_fraction": added_volume,
                 "distance_certificate": certificate, "convex_elems": [element]}
        result["meshes"].append(entry)
        print(json.dumps({"mesh": package, "old_hulls": len(parts), "planned_hulls": 1,
                          "added_volume_fraction": added_volume, "certificate": certificate}), flush=True)
    result["complete"] = True
    write_report(args.plan, result)
    print("PLAN_SAVED " + str(args.plan.resolve()), flush=True)


def apply(args):
    import unreal as u
    from create_food_collision import install_source_hulls, menu_meshes, mesh_report
    proposal = json.loads(args.plan.read_text(encoding="utf8"))
    if proposal.get("complete") is not True or proposal.get("selected_scope") != list(CANDIDATES):
        raise ValueError("Refusing incomplete or expanded collision budget plan")
    snapshot = Path(proposal["source_snapshot"])
    if file_sha256(snapshot) != proposal.get("source_snapshot_sha256"):
        raise ValueError("Native audit changed after planning")
    entries = {entry["path"]: entry for entry in proposal["meshes"]}
    if len(entries) != len(CANDIDATES) or set(entries) != set(CANDIDATES):
        raise ValueError("Budget plan must contain exactly the four approved fragment meshes")
    subsystem = u.get_editor_subsystem(u.StaticMeshEditorSubsystem)
    if subsystem is None:
        raise RuntimeError("Use a full Unreal editor, not a Python commandlet")
    rows, references = menu_meshes()
    table_hash = json_sha256(rows)
    checked = {}
    # Preflight every selected mesh before the first package is changed.
    for package in CANDIDATES:
        entry = entries[package]
        if references.get(package) != entry["table_uses"]:
            raise ValueError("Current saved menu mesh choices/scales changed: " + package)
        certificate = entry["distance_certificate"]
        if certificate.get("passed") is not True or certificate["maximum_certified_upper_bound_item_cm"] > DISTANCE_BUDGET_CM or entry["added_volume_fraction"] > ADDED_VOLUME_BUDGET:
            raise ValueError("Plan exceeds the approved geometry budgets")
        if len(entry["convex_elems"]) != 1:
            raise ValueError("Each selected fragment must become one source convex")
        validate_hull(entry["convex_elems"][0])
        asset = package_file(u.Paths.project_content_dir(), package)
        if file_sha256(asset) != entry["source_asset_sha256"]:
            raise ValueError("Saved mesh changed after planning: " + package)
        mesh = u.load_asset(package)
        if not isinstance(mesh, u.StaticMesh):
            raise ValueError("Not a saved static mesh: " + package)
        before = mesh_report(mesh, subsystem)
        if before["render_geometry_sha256"] != entry["source_render_sha256"] or json_sha256(before["native_collision"]["convex_elems"]) != entry["source_convex_sha256"]:
            raise ValueError("Loaded geometry differs from the saved native audit: " + package)
        native_positions = sorted({tuple(vertex) for section in before["native_render_sections"] for vertex in section["vertices"]})
        if entry["convex_elems"][0]["vertex_data"] != [list(point) for point in native_positions]:
            raise ValueError("Plan no longer retains every exact native source position")
        checked[package] = mesh, before, asset
    report = {"mode": "apply", "complete": False, "plan_sha256": file_sha256(args.plan),
              "menu_rows_sha256_before": table_hash, "selected_scope": list(CANDIDATES), "saved_packages": [], "meshes": [], "errors": []}
    try:
        for package in CANDIDATES:
            entry = entries[package]
            mesh, before, asset = checked[package]
            if file_sha256(asset) != entry["source_asset_sha256"]:
                raise ValueError("Package changed during preflight: " + package)
            backup = ROOT / "Saved/FoodCollisionProbe/BudgetBackup" / entry["source_asset_sha256"] / (package[6:] + ".uasset")
            backup.parent.mkdir(parents=True, exist_ok=True)
            if not backup.exists():
                shutil.copy2(asset, backup)
            if file_sha256(backup) != entry["source_asset_sha256"]:
                raise ValueError("Backup does not match the original saved package")
            install_source_hulls(mesh, entry["convex_elems"])
            after = mesh_report(mesh, subsystem)
            if after["render_geometry_sha256"] != before["render_geometry_sha256"] or after["materials"] != before["materials"]:
                raise ValueError("Render geometry/materials changed; package was not saved")
            bounds_error = max(abs(a - b) for field in ("bounds_min", "bounds_max") for a, b in zip(before[field], after[field]))
            if bounds_error > 0.00001:
                raise ValueError("Mesh bounds/pivot changed; package was not saved")
            roundtrip = raw_vertex_roundtrip(entry["convex_elems"], after["native_collision"]["convex_elems"])
            if roundtrip["maximum_vertex_error_cm"] > 0.00001 or after["native_convex_collision_count"] != 1:
                raise ValueError("Native convex import failed; package was not saved")
            if json_sha256(menu_meshes()[0]) != table_hash:
                raise ValueError("Saved menu rows/scales changed during authoring")
            if not u.EditorAssetLibrary.save_loaded_asset(mesh, only_if_is_dirty=False):
                raise RuntimeError("Saving authored fragment failed")
            after.update({"path": package, "backup_file": str(backup), "before_asset_sha256": entry["source_asset_sha256"],
                          "saved_asset_sha256": file_sha256(asset), "raw_vertex_roundtrip": roundtrip,
                          "bounds_roundtrip_maximum_error_cm": bounds_error, "distance_certificate": entry["distance_certificate"]})
            report["saved_packages"].append(package)
            report["meshes"].append(after)
            write_report(args.report, report)
            u.log("MC_FRAGMENT_COLLISION_BUDGET " + json.dumps({"mesh": package, "old_hulls": entry["source_hulls"], "new_hulls": 1}))
        report["menu_rows_sha256_after"] = json_sha256(menu_meshes()[0])
        if report["menu_rows_sha256_after"] != table_hash:
            raise ValueError("Saved menu changed during authoring")
        report["complete"] = True
    except Exception:
        report["errors"].append(traceback.format_exc())
        raise
    finally:
        write_report(args.report, report)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mode", choices=("plan", "apply"), default="plan")
    parser.add_argument("--snapshot", type=Path, default=ROOT / "Saved/FoodCollisionProbe/FoodCollision_20261003_Current.json")
    parser.add_argument("--plan", type=Path, default=ROOT / "Saved/FoodCollisionProbe/FragmentCollisionBudgetPlan.json")
    parser.add_argument("--report", type=Path, default=ROOT / "Saved/FoodCollisionProbe/FragmentCollisionBudgetApplied.json")
    parser.add_argument("--dependencies-root", type=Path, default=ROOT / "Saved/FoodCollisionProbe/QAPythonDeps")
    args = parser.parse_args()
    if args.mode == "plan":
        plan(args)
    else:
        apply(args)


if __name__ == "__main__":
    main()
