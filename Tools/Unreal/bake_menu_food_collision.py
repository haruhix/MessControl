"""Bake table-driven food collision before cooking, without saving source meshes.

Run with -ExecutePythonScript in the full unattended editor. Exit status is
reported in Saved/FoodCollisionBake/MenuCollision.json and checked by the caller.
"""
import argparse
import hashlib
import json
import time
import traceback
from pathlib import Path

import unreal as u


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def object_path(asset_path):
    # DataTable JSON exports hard UObject references as Class'/Package.Object'.
    if "'" in asset_path:
        parts = asset_path.split("'")
        if len(parts) != 3 or parts[2]:
            raise RuntimeError("Invalid exported asset reference: " + asset_path)
        return parts[1]
    return asset_path


def package_file(asset_path):
    asset_path = object_path(asset_path)
    package = asset_path.split(".")[0]
    if not package.startswith("/Game/") or ".." in package.split("/"):
        raise RuntimeError("Expected a project asset: " + asset_path)
    return Path(u.Paths.project_content_dir()) / (package[6:] + ".uasset")


def rows(table):
    return json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(table))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--menu", default="/Game/Data/DT_BreakfastMenu")
    parser.add_argument("--report", default="Saved/FoodCollisionBake/MenuCollision.json")
    parser.add_argument("--audit-only", action="store_true")
    parser.add_argument("--verify-idempotence", action="store_true")
    args = parser.parse_args()
    report_path = Path(args.report)
    if not report_path.is_absolute():
        report_path = Path(u.Paths.project_dir()) / report_path
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report = {"menu": args.menu, "complete": False, "errors": [], "meshes": []}
    started = time.perf_counter()
    try:
        table = u.load_asset(args.menu)
        if not table:
            raise RuntimeError("Food menu not found: " + args.menu)
        before = rows(table)
        sources = sorted({path for row in before for role in ("WholeMeshes", "FragmentMeshes")
                          for path in row.get(role, []) if path and path != "None"})
        hashes = {path: sha256(package_file(path)) for path in sources}
        if not args.audit_only and not u.MCFoodCollisionEditorLibrary.bake_menu_collision(table, True):
            raise RuntimeError("Food collision bake failed; see editor log")
        if not u.MCFoodCollisionEditorLibrary.is_menu_current(table):
            raise RuntimeError("Food collision profiles are missing or stale")
        after = rows(table)
        if args.audit_only and before != after:
            raise RuntimeError("Read-only audit changed the loaded menu")
        # CollisionData is derived. Every gameplay/appearance field must survive.
        gameplay = lambda data: [{key: value for key, value in row.items() if key != "CollisionData"}
                                 for row in data]
        if gameplay(before) != gameplay(after):
            raise RuntimeError("Baking changed gameplay menu fields")
        for row in after:
            for profile_path in row.get("CollisionData", []):
                profile_path = object_path(profile_path)
                if not package_file(profile_path).is_file():
                    raise RuntimeError("Collision profile has not been saved: " + profile_path)
                data = u.load_asset(profile_path)
                body = data.get_editor_property("body_setup")
                hulls = body.get_editor_property("agg_geom").get_editor_property("convex_elems")
                report["meshes"].append({
                    "row": row["Name"], "profile": profile_path,
                    "source": str(data.get_editor_property("source_mesh")),
                    "hulls": len(hulls), "hull_limit": data.get_editor_property("hull_limit"),
                    "vertex_limit": data.get_editor_property("hull_vertex_limit"),
                    "source_geometry_key": data.get_editor_property("source_geometry_key"),
                })
        report["source_packages_unchanged"] = all(sha256(package_file(path)) == digest
                                                   for path, digest in hashes.items())
        if not report["source_packages_unchanged"]:
            raise RuntimeError("A source visual mesh package changed during collision bake")
        if args.verify_idempotence and not args.audit_only:
            outputs = {table.get_path_name(), *(item["profile"] for item in report["meshes"])}
            saved = {path: sha256(package_file(path)) for path in outputs}
            if not u.MCFoodCollisionEditorLibrary.bake_menu_collision(table, True):
                raise RuntimeError("Second collision bake failed")
            report["second_bake_packages_unchanged"] = all(sha256(package_file(path)) == digest
                                                          for path, digest in saved.items())
            if not report["second_bake_packages_unchanged"]:
                raise RuntimeError("Unchanged menu was rewritten by the second bake")
        report["complete"] = True
        u.log("MC_FOOD_COLLISION_BAKE_PASS " + str(report_path))
    except Exception:
        report["errors"].append(traceback.format_exc())
        u.log_error(report["errors"][-1])
    finally:
        report["elapsed_seconds"] = time.perf_counter() - started
        report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
        u.SystemLibrary.quit_editor()


if __name__ == "__main__":
    main()
