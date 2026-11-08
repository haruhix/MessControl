"""Read-only, loaded-editor audit; never change or save menu/mesh assets."""
import datetime
import hashlib
import json
import math
import re
import traceback
from pathlib import Path

import unreal as u


TABLE = "/Game/Data/DT_BreakfastMenu"
OUTPUT = Path("E:/CodexWork/MessControl/Validation/FoodPortions_20261107/CpuMeshCoverage.json")
LIMIT_CM = 110.0


def object_path(value):
    text = str(value or "").strip()
    if text in ("", "None", "null"):
        return None
    if "'" in text:
        fields = text.split("'")
        if len(fields) != 3 or fields[2]:
            raise ValueError("Unexpected asset reference: " + text)
        text = fields[1]
    if not text.startswith("/") or ".." in text.split("/"):
        raise ValueError("Unexpected asset path: " + text)
    return text if "." in text else text + "." + text.rsplit("/", 1)[1]


def vector(value):
    if isinstance(value, dict):
        result = [float(value[axis]) for axis in ("X", "Y", "Z")]
    elif isinstance(value, (list, tuple)) and len(value) == 3:
        result = [float(component) for component in value]
    else:
        # FJsonObjectConverter normally exports FVector as an object.
        result = [float(component) for component in re.findall(r"[XYZ]\s*=\s*([-+0-9.eE]+)", str(value))]
    if len(result) != 3 or not all(math.isfinite(component) for component in result):
        raise ValueError("Invalid row scale: " + str(value))
    return result


def dirty_packages():
    return sorted(package.get_path_name() for package in u.EditorLoadingAndSavingUtils.get_dirty_content_packages())


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True).encode("utf-8")).hexdigest()


def main():
    report = {
        "table": TABLE, "read_only": True, "complete": False, "errors": [],
        "utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "limit_cm": LIMIT_CM, "actor_scale_assumption": [1.0, 1.0, 1.0],
        "scope": "Current ordinary Food menu rows; excludes Spicy/ForeignObject. Boss encounter scale/ownership overrides are outside this table audit.",
        "dimension_note": "Authored mesh AABB times absolute row scale. The 110cm isotropic limit is invariant under the native flat-pose axis permutations; impact squash is ignored.",
        "rows": [], "excluded_rows": [], "meshes": [],
        "unsupported_oversized_fragment_uses": [], "unsupported_oversized_whole_uses": [],
    }
    try:
        before_dirty = dirty_packages()
        report["dirty_packages_before"] = before_dirty
        table = u.load_asset(TABLE)
        if not table:
            raise RuntimeError("Food menu missing: " + TABLE)
        rows = json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(table))
        report["table_rows_sha256_before"] = digest(rows)
        report["table_row_count"] = len(rows)
        mesh_uses = {}
        for row in rows:
            kind = row.get("Kind", "Food")
            if kind not in ("Food", "EMCFoodKind::Food", 0):
                report["excluded_rows"].append({"row": row["Name"], "kind": kind})
                continue
            report["rows"].append({"row": row["Name"], "kind": kind,
                                   "scale": row.get("Scale"), "fragment_scale": row.get("FragmentScale")})
            for role, scale_key in (("WholeMeshes", "Scale"), ("FragmentMeshes", "FragmentScale")):
                scale = vector(row[scale_key])
                paths = {path for path in (object_path(value) for value in row.get(role, [])) if path}
                # ConfigureItem falls back to whole mesh variants when no fragment is authored.
                fallback = role == "FragmentMeshes" and not paths
                if fallback:
                    paths = {path for path in (object_path(value) for value in row.get("WholeMeshes", [])) if path}
                for path in sorted(paths):
                    mesh_uses.setdefault(path, []).append({"row": row["Name"], "role": role,
                                                          "scale": scale, "whole_mesh_fallback": fallback})
        for path, uses in sorted(mesh_uses.items()):
            entry = {"path": path, "allow_cpu_access": None, "uses": uses}
            report["meshes"].append(entry)
            try:
                mesh = u.load_asset(path)
                if not isinstance(mesh, u.StaticMesh):
                    raise RuntimeError("Missing/non-StaticMesh asset")
                entry["allow_cpu_access"] = bool(mesh.get_editor_property("allow_cpu_access"))
                bounds = mesh.get_bounding_box()
                lo, hi = bounds.min, bounds.max
                size = [float(hi.x-lo.x), float(hi.y-lo.y), float(hi.z-lo.z)]
                if not all(math.isfinite(component) and component > 0 for component in size):
                    raise RuntimeError("Invalid mesh bounds: " + str(size))
                entry["mesh_size_cm"] = size
                for use in uses:
                    use["world_size_cm"] = [size[axis] * abs(use["scale"][axis]) for axis in range(3)]
                    use["exceeds_110_cm"] = max(use["world_size_cm"]) > LIMIT_CM + 0.0001
                    use["requires_cut_for_hand"] = use["exceeds_110_cm"]
                    use["can_use_native_planar_cut"] = entry["allow_cpu_access"]
                    if use["exceeds_110_cm"] and not entry["allow_cpu_access"]:
                        key = "unsupported_oversized_fragment_uses" if use["role"] == "FragmentMeshes" else "unsupported_oversized_whole_uses"
                        report[key].append(dict(path=path, **use))
            except Exception as error:
                entry["error"] = str(error)
                report["errors"].append({"path": path, "error": str(error)})
        after = json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(table))
        report["table_rows_sha256_after"] = digest(after)
        report["table_payload_unchanged"] = rows == after
        report["dirty_packages_after"] = dirty_packages()
        report["dirty_packages_unchanged"] = before_dirty == report["dirty_packages_after"]
        report["unique_mesh_count"] = len(report["meshes"])
        report["ordinary_row_count"] = len(report["rows"])
        report["unsupported_fragment_use_count"] = len(report["unsupported_oversized_fragment_uses"])
        report["complete"] = not report["errors"] and report["table_payload_unchanged"] and report["dirty_packages_unchanged"]
    except Exception:
        report["errors"].append({"fatal": traceback.format_exc()})
    finally:
        OUTPUT.parent.mkdir(parents=True, exist_ok=True)
        OUTPUT.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        print("MC_FOOD_CPU_AUDIT", json.dumps({"complete": report["complete"],
              "unsupported_fragment_uses": len(report["unsupported_oversized_fragment_uses"]),
              "errors": len(report["errors"]), "report": str(OUTPUT)}))


main()
