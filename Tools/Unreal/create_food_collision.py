"""Author and audit saved compound-convex collision for the current food menu.

Run in a full, unattended editor (not a Python commandlet):
UnrealEditor.exe MessControl.uproject -NullRHI -unattended -nosplash -nop4
  -ExecutePythonScript=".../create_food_collision.py --mode audit --report FoodCollisionBefore.json"
Build imports audit-derived source convex hulls, or explicitly uses native Auto Convex.
Only the explicitly enumerated food mesh packages are saved; render geometry,
material assignments, pivots, menu data, and item scales are preserved.
"""
import argparse
import hashlib
import json
import re
import shutil
import sys
import time
import traceback
from pathlib import Path

import unreal as u

sys.path.insert(0, str(Path(__file__).resolve().parent))
from food_collision_authoring_guard import (
    file_sha256, json_sha256, package_file, raw_vertex_roundtrip, validate_plan, validate_resume,
)


def vector(value):
    return [float(value.x), float(value.y), float(value.z)]


def property_value(value, name, default=None):
    try:
        return value.get_editor_property(name)
    except Exception:
        return default


def serialize(value):
    if isinstance(value, (bool, int, float, str)) or value is None:
        return value
    if isinstance(value, u.Vector):
        return vector(value)
    if isinstance(value, u.Rotator):
        return [float(value.pitch), float(value.yaw), float(value.roll)]
    if isinstance(value, u.Transform):
        rotation = value.rotation
        return {
            "translation": vector(value.translation),
            "rotationQuat": [rotation.x, rotation.y, rotation.z, rotation.w],
            "scale": vector(value.scale3d),
        }
    if isinstance(value, u.Object):
        return value.get_path_name()
    try:
        return [serialize(item) for item in value]
    except TypeError:
        return str(value)


def text_group(text, name):
    """Read an exact reflected struct field, including non-editor-visible data."""
    match = re.search(r"(?:\(|,)" + re.escape(name) + r"=\(", text)
    if not match:
        return None
    start = match.end() - 1
    depth = 0
    for index in range(start, len(text)):
        depth += (text[index] == "(") - (text[index] == ")")
        if depth == 0:
            return text[start:index + 1]
    raise RuntimeError("Unbalanced Unreal struct export: " + name)


def text_vector(text, dimensions=3):
    axes = ("X", "Y", "Z", "W")[:dimensions]
    return [float(re.search(r"(?:\(|,)" + axis + r"=([^,)]+)", text).group(1)) for axis in axes]


def convex_payload(element):
    # FKConvexElem.VertexData/IndexData are native UPROPERTY without editor visibility.
    # UScriptStruct::ExportText exposes their real loaded values, unlike get_editor_property.
    text = element.export_text()
    vertices = text_group(text, "VertexData")
    indices = text_group(text, "IndexData")
    transform = text_group(text, "Transform")
    payload = {
        "vertex_data": [text_vector(item) for item in re.findall(r"\(X=[^()]+\)", vertices or "")],
        "index_data": [int(value) for value in re.findall(r"-?\d+", indices or "")],
        "transform": {"translation": [0.0, 0.0, 0.0], "rotationQuat": [0.0, 0.0, 0.0, 1.0], "scale": [1.0, 1.0, 1.0]},
        "native_export_text": text,
    }
    if transform:
        for field, key, dimensions in (("Translation", "translation", 3), ("Rotation", "rotationQuat", 4), ("Scale3D", "scale", 3)):
            value = text_group(transform, field)
            if value:
                payload["transform"][key] = text_vector(value, dimensions)
    return payload


def convex_text(element):
    points = element["vertex_data"]
    if len(points) < 4:
        raise RuntimeError("Refusing a lower-dimensional collision hull")
    def point(value):
        return "(X={:.17g},Y={:.17g},Z={:.17g})".format(*value)
    minimum = [min(vertex[axis] for vertex in points) for axis in range(3)]
    maximum = [max(vertex[axis] for vertex in points) for axis in range(3)]
    return "(VertexData=({}),IndexData=({}),ElemBox=(Min={},Max={},IsValid=True))".format(
        ",".join(point(vertex) for vertex in points),
        ",".join(str(index) for index in element.get("index_data", [])), point(minimum), point(maximum))


def install_source_hulls(mesh, elements):
    body = mesh.get_editor_property("body_setup")
    aggregate = type(body.get_editor_property("agg_geom"))()
    if not aggregate.import_text("(ConvexElems=({}))".format(",".join(convex_text(element) for element in elements))):
        raise RuntimeError("Unreal rejected reflected source convex geometry")
    body.set_editor_property("agg_geom", aggregate)
    # Set the convex cook policy before the native mesh rebuild. ComplexAsSimple
    # would otherwise skip cooking the simple convex data that was just imported.
    body.set_editor_property("collision_trace_flag", u.CollisionTraceFlag.CTF_USE_SIMPLE_AND_COMPLEX)
    # This reflected editable flag avoids an unnecessary V-HACD initialization.
    mesh.set_editor_property("customized_collision", True, notify_mode=u.PropertyAccessChangeNotifyMode.NEVER)
    # A mesh rebuild invalidates BodySetup's physics GUID and cooks the new hulls.
    # UStaticMesh::PostEditChangeProperty -> Build -> InvalidatePhysicsData/CreatePhysicsMeshes.
    # The value is unchanged; ALWAYS requests this native notification explicitly.
    mesh.set_editor_property("body_setup", body, notify_mode=u.PropertyAccessChangeNotifyMode.ALWAYS)


def decompose_component(component, subsystem, args, name):
    """Temporary native StaticMesh authoring; no intermediate package is saved."""
    points = [tuple(point) for point in component["vertex_data"]]
    canonical_points = sorted(set(points))
    point_ids = {point: index for index, point in enumerate(canonical_points)}
    indices = component["index_data"]
    faces = []
    for index in range(0, len(indices), 3):
        face = tuple(point_ids[points[indices[index + corner]]] for corner in range(3))
        faces.append(min(face, face[1:] + face[:1], face[2:] + face[:2]))
    signature = {"vertices": canonical_points, "triangles": sorted(faces), "hulls": args.hulls,
                 "max_hull_vertices": args.vertices, "voxel_precision": args.precision,
                 "engine": u.SystemLibrary.get_engine_version()}
    cache_key = hashlib.sha256(json.dumps(signature, sort_keys=True).encode("utf8")).hexdigest()
    cache_dir = Path(u.Paths.project_saved_dir()) / "FoodCollisionProbe" / "NativeComponentCache"
    cache_dir.mkdir(parents=True, exist_ok=True)
    cache_file = cache_dir / (cache_key + ".json")
    if cache_file.exists():
        cache = json.loads(cache_file.read_text(encoding="utf8"))
        if cache["signature"] != json.loads(json.dumps(signature)):
            raise RuntimeError("Collision cache signature mismatch")
        u.log("MC_FOOD_COLLISION_COMPONENT_CACHE " + cache_key)
        return cache["convex_elems"]
    temporary = u.new_object(u.StaticMesh, name=name)
    descriptor = u.StaticMesh.create_static_mesh_description(temporary)
    group = descriptor.create_polygon_group()
    descriptor.set_polygon_group_material_slot_name(group, "Collision")
    vertices = []
    for point in component["vertex_data"]:
        vertex = descriptor.create_vertex()
        descriptor.set_vertex_position(vertex, u.Vector(*point))
        vertices.append(vertex)
    for index in range(0, len(indices), 3):
        instances = [descriptor.create_vertex_instance(vertices[indices[index + corner]]) for corner in range(3)]
        descriptor.create_triangle(group, instances)
    temporary.build_from_static_mesh_descriptions([descriptor], build_simple_collision=False, fast_build=False)
    if not subsystem.set_convex_decomposition_collisions(temporary, args.hulls, args.vertices, args.precision):
        raise RuntimeError("Native decomposition of isolated source component failed")
    aggregate = temporary.get_editor_property("body_setup").get_editor_property("agg_geom")
    elements = [convex_payload(element) for element in aggregate.get_editor_property("convex_elems")]
    if not elements:
        raise RuntimeError("Isolated source component produced no native convex hulls")
    cache_file.write_text(json.dumps({"signature": signature, "convex_elems": elements}), encoding="utf8")
    return elements


def menu_meshes():
    table = u.load_asset("/Game/Data/DT_BreakfastMenu")
    if not table:
        raise RuntimeError("Saved DT_BreakfastMenu is missing")
    rows = json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(table))
    references = {}
    for row in rows:
        for key in ("WholeMeshes", "FragmentMeshes"):
            for reference in row.get(key, []):
                if reference and reference != "None":
                    package = reference.split(".")[0]
                    references.setdefault(package, []).append({
                        "row": row["Name"],
                        "fragment": key == "FragmentMeshes",
                        "scale": row["FragmentScale" if key == "FragmentMeshes" else "Scale"],
                    })
    # The native food actor still uses these saved meshes as its constructor/fallback.
    for package in ("/Game/Art/Meshes/SM_Food", "/Game/Stylized_Vegetables/Meshes/SM_Broccoli"):
        if u.EditorAssetLibrary.does_asset_exist(package):
            references.setdefault(package, []).append({"native_fallback": True})
    return rows, references


def mesh_report(mesh, subsystem):
    box = mesh.get_bounding_box()
    data = {
        "path": mesh.get_path_name(),
        "bounds_min": vector(box.min),
        "bounds_max": vector(box.max),
        "lod0_triangles": mesh.get_num_triangles(0),
        "native_simple_collision_count": subsystem.get_simple_collision_count(mesh),
        "native_convex_collision_count": subsystem.get_convex_collision_count(mesh),
        "materials": [str(slot.material_interface.get_path_name()) if slot.material_interface else None
                      for slot in mesh.get_editor_property("static_materials")],
        "customized_collision": bool(property_value(mesh, "customized_collision", False)),
    }
    sections = []
    for section in range(mesh.get_num_sections(0)):
        vertices, indices, normals, uvs, tangents = u.ProceduralMeshLibrary.get_section_from_static_mesh(mesh, 0, section)
        sections.append({
            "section": section,
            "vertices": [vector(vertex) for vertex in vertices],
            "triangles": list(indices),
            "collision_enabled": subsystem.is_section_collision_enabled(mesh, 0, section),
        })
    data["native_render_sections"] = sections
    data["render_geometry_sha256"] = hashlib.sha256(json.dumps(sections, sort_keys=True).encode("utf8")).hexdigest()
    body = mesh.get_editor_property("body_setup")
    if not body:
        data["native_collision"] = {}
        return data
    data["body_setup_path"] = body.get_path_name()
    data["collision_trace_flag"] = str(body.get_editor_property("collision_trace_flag"))
    aggregate = body.get_editor_property("agg_geom")
    data["native_collision"] = {}
    for name in ("sphere_elems", "box_elems", "sphyl_elems", "convex_elems", "tapered_capsule_elems", "level_set_elems"):
        items = []
        for element in property_value(aggregate, name, []):
            item = {}
            for field in ("name", "center", "rotation", "x", "y", "z", "radius", "length", "radius0", "radius1",
                          "transform", "vertex_data", "index_data", "elem_box", "collision_enabled"):
                value = property_value(element, field)
                if value is not None:
                    item[field] = serialize(value)
            if name == "convex_elems":
                item.update(convex_payload(element))
            items.append(item)
        data["native_collision"][name] = items
    return data


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--mode", choices=("audit", "build"), default="audit")
    parser.add_argument("--report", default="FoodCollisionAfter.json")
    parser.add_argument("--hulls", type=int, default=64)
    parser.add_argument("--vertices", type=int, default=64)
    parser.add_argument("--precision", type=int, default=1000000)
    parser.add_argument("--mesh", action="append", default=[])
    parser.add_argument("--plan", help="Native audit-derived hybrid plan JSON; never changes render geometry")
    parser.add_argument("--resume-from", help="Resume a saved native report after interrupted expensive authoring")
    parser.add_argument("--require-source-exact", action="store_true", help="Refuse V-HACD and hybrid fallback authoring")
    parser.add_argument("--raw-vertex-tolerance", type=float, default=0.00001, help="Maximum reflected import/export error in asset-local cm")
    parser.add_argument("--bounds-tolerance", type=float, default=0.00001, help="Allow only floating-point bounds recomputation error in asset-local cm")
    args = parser.parse_args()
    output = Path(u.Paths.project_saved_dir()) / "FoodCollisionProbe"
    output.mkdir(parents=True, exist_ok=True)
    report = {"mode": args.mode, "engine": u.SystemLibrary.get_engine_version(), "source_assets_saved": False,
              "settings": {"hulls": args.hulls, "max_hull_vertices": args.vertices, "voxel_precision": args.precision},
              "meshes": [], "errors": [], "saved_packages": []}
    try:
        subsystem = u.get_editor_subsystem(u.StaticMeshEditorSubsystem)
        if subsystem is None:
            raise RuntimeError("StaticMeshEditorSubsystem is unavailable. Use a full editor with -ExecutePythonScript, not -run=pythonscript.")
        rows, references = menu_meshes()
        report["saved_table_rows"] = rows
        report["scope"] = sorted(references)
        report["scope_sha256"] = json_sha256(report["scope"])
        report["saved_table_rows_sha256"] = json_sha256(rows)
        plans = {}
        plan = None
        plan_hash = None
        if args.plan:
            plan = json.loads(Path(args.plan).read_text(encoding="utf8"))
            report["source_plan"] = str(Path(args.plan).resolve())
            plan_hash = file_sha256(args.plan)
            report["source_plan_sha256"] = plan_hash
        selected = sorted(references)
        if args.mesh:
            invalid = set(args.mesh) - set(references)
            if invalid:
                raise RuntimeError("Refusing package outside food scope: " + repr(sorted(invalid)))
            selected = sorted(set(args.mesh))
        report["selected_scope"] = selected
        if plan is not None:
            plans = validate_plan(plan, rows, references, selected)
            source_report = Path(plan["source_report"])
            if not source_report.is_absolute():
                source_report = Path(u.Paths.project_dir()) / source_report
            if not plan.get("source_report_sha256") or file_sha256(source_report) != plan["source_report_sha256"]:
                raise RuntimeError("Native audit file changed after collision planning")
        if args.require_source_exact and (plan is None or any(plans[package]["method"] != "source_convex" for package in selected)):
            raise RuntimeError("Exact source authoring requires a complete source_convex plan for every selected mesh")
        if not args.raw_vertex_tolerance > 0:
            raise RuntimeError("Raw vertex tolerance must be positive")
        if args.resume_from:
            previous = json.loads(Path(args.resume_from).read_text(encoding="utf8"))
            completed, report["meshes"] = validate_resume(previous, selected, report["scope_sha256"],
                report["saved_table_rows_sha256"], plan_hash, u.Paths.project_content_dir())
            report["saved_packages"] = sorted(completed)
            report["source_assets_saved"] = bool(completed)
            report["resumed_from"] = str(Path(args.resume_from).resolve())
            selected = [package for package in selected if package not in completed]
        for package in selected:
            started = time.monotonic()
            data = {"path": package, "table_uses": references[package]}
            try:
                mesh = u.load_asset(package)
                if not isinstance(mesh, u.StaticMesh):
                    raise RuntimeError("Not a saved StaticMesh: " + package)
                before = mesh_report(mesh, subsystem)
                data["before_bounds_min"] = before["bounds_min"]
                data["before_bounds_max"] = before["bounds_max"]
                if args.mode == "build":
                    asset_file = package_file(u.Paths.project_content_dir(), package)
                    before_asset_sha = file_sha256(asset_file)
                    # Each disk state gets its own backup; incoming Git properties
                    # must not be replaced by a pre-merge snapshot during recovery.
                    backup = output / "Backup" / before_asset_sha / package.removeprefix("/Game/")
                    backup = backup.with_suffix(".uasset")
                    backup.parent.mkdir(parents=True, exist_ok=True)
                    if not backup.exists():
                        shutil.copy2(asset_file, backup)
                    data["before_asset_sha256"] = before_asset_sha
                    data["backup_file"] = str(backup)
                    selected_plan = plans.get(package)
                    method = selected_plan.get("method", "native_vhacd") if selected_plan else "native_vhacd"
                    if selected_plan and selected_plan.get("source_render_sha256") != before["render_geometry_sha256"]:
                        raise RuntimeError("Source render geometry changed since hybrid planning; audit and regenerate the plan")
                    if method in ("source_convex", "hybrid_components"):
                        elements = list(selected_plan["convex_elems"])
                        for component_index, component in enumerate(selected_plan.get("voxel_components", [])):
                            elements.extend(decompose_component(component, subsystem, args, "FoodCollisionTransient_{}_{}".format(len(report["meshes"]), component_index)))
                        install_source_hulls(mesh, elements)
                    elif method == "native_vhacd":
                        if not subsystem.set_convex_decomposition_collisions(mesh, args.hulls, args.vertices, args.precision):
                            raise RuntimeError("Native Auto Convex authoring failed")
                    else:
                        raise RuntimeError("Unknown collision authoring method: " + method)
                    body = mesh.get_editor_property("body_setup")
                    body.set_editor_property("collision_trace_flag", u.CollisionTraceFlag.CTF_USE_SIMPLE_AND_COMPLEX)
                    after = mesh_report(mesh, subsystem)
                    if not after["native_convex_collision_count"]:
                        raise RuntimeError("Auto Convex produced zero hulls; package was not saved")
                    if after["render_geometry_sha256"] != before["render_geometry_sha256"] or after["materials"] != before["materials"]:
                        raise RuntimeError("Render geometry/materials unexpectedly changed; package was not saved")
                    bounds_error = max(abs(left-right) for field in ("bounds_min", "bounds_max")
                                       for left, right in zip(before[field], after[field]))
                    data["bounds_roundtrip"] = dict(before_min=before["bounds_min"], before_max=before["bounds_max"],
                        after_min=after["bounds_min"], after_max=after["bounds_max"], maximum_error_cm=bounds_error,
                        tolerance_cm=args.bounds_tolerance)
                    if bounds_error > args.bounds_tolerance:
                        raise RuntimeError("Mesh bounds/pivot unexpectedly changed; package was not saved")
                    if method in ("source_convex", "hybrid_components"):
                        after["raw_vertex_roundtrip"] = raw_vertex_roundtrip(elements, after["native_collision"]["convex_elems"])
                        if after["raw_vertex_roundtrip"]["maximum_vertex_error_cm"] > args.raw_vertex_tolerance:
                            raise RuntimeError("Reflected convex import lost vertex precision; package was not saved")
                    if not u.EditorAssetLibrary.save_loaded_asset(mesh, only_if_is_dirty=False):
                        raise RuntimeError("Saving authored food mesh failed")
                    report["saved_packages"].append(package)
                    report["source_assets_saved"] = True
                    after["saved_asset_sha256"] = file_sha256(asset_file)
                    after["before_collision_count"] = before["native_simple_collision_count"]
                    after["authoring_method"] = method
                    after["authoring_settings"] = dict(report["settings"])
                    data.update(after)
                else:
                    data.update(before)
                    data["saved_asset_sha256"] = file_sha256(package_file(u.Paths.project_content_dir(), package))
                hulls = data.get("native_collision", {}).get("convex_elems", [])
                data["convex_hull_vertex_counts"] = [len(hull.get("vertex_data", [])) for hull in hulls]
                data["convex_total_vertices"] = sum(data["convex_hull_vertex_counts"])
                u.log("MC_FOOD_COLLISION_MESH " + json.dumps({"path": package, "hulls": len(hulls), "vertices": data["convex_total_vertices"]}))
            except Exception:
                data["error"] = traceback.format_exc()
                report["errors"].append({"mesh": package, "error": data["error"]})
                u.log_error(data["error"])
            data["seconds"] = round(time.monotonic() - started, 3)
            report["meshes"].append(data)
            (output / args.report).write_text(json.dumps(report, indent=2), encoding="utf8")
        report["complete"] = not report["errors"] and len(report["meshes"]) == len(report["selected_scope"])
    except Exception:
        report["errors"].append({"script": traceback.format_exc()})
        report["complete"] = False
    finally:
        (output / args.report).write_text(json.dumps(report, indent=2), encoding="utf8")
        u.log("MC_FOOD_COLLISION_RESULT " + json.dumps({"complete": report.get("complete"), "errors": len(report["errors"]),
                                                       "meshes": len(report["meshes"]), "saved_packages": report["saved_packages"]}))
        u.SystemLibrary.quit_editor()


if __name__ == "__main__":
    main()
