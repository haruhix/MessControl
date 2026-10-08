"""Derive individual walnut meshes from the imported Fab showcase collection.

Run through remote_python.py in an idle Unreal Editor. Geometry Script keeps the
source UVs, normals and material while spatially separating its disconnected
objects. Imported packages are hash checked and never saved. Existing generated
meshes are reused, preserving subsequent designer edits.
"""
import hashlib
import json
from pathlib import Path

import unreal as u

SOURCE_PATH = "/Game/Fab/Stylized_Walnut_-_Game_Ready/stylized_walnut_game_ready/StaticMeshes/stylized_walnut_game_ready"
FOLDER = "/Game/Gameplay/CoreLoop"
TAG = "MCWalnutDerivation"
PARTS = (
    ("SM_Walnut_Whole", 0, 474, (-5.0, 20.0, 3.8839)),
    ("SM_Walnut_HalfShell", 3, 336, (-6.5757, 0.0050, 3.8832)),
    ("SM_Walnut_Kernel", 7, 682, (5.0, 10.0, 3.3273)),
)


def max_half_extent(mesh):
    box = mesh.get_bounding_box()
    extent = (box.max - box.min) * .5
    return max(extent.x, extent.y, extent.z)


def source_package_hashes():
    content = Path(u.Paths.project_content_dir())
    imported = content / "Fab/Stylized_Walnut_-_Game_Ready"
    return {str(p.relative_to(content)).replace("\\", "/"): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(imported.rglob("*.uasset"))}


def ensure_walnut_assets():
    if u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor():
        raise RuntimeError("Stop the owned PIE session before authoring walnut assets")
    dirty = list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    dirty += list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())
    if dirty:
        raise RuntimeError("Preserve unsaved packages before walnut derivation: " + ", ".join(p.get_name() for p in dirty))
    source = u.load_asset(SOURCE_PATH)
    if not isinstance(source, u.StaticMesh):
        raise RuntimeError("Required Fab walnut mesh is unavailable")
    before = source_package_hashes()
    source_file = Path(u.Paths.project_content_dir()) / (SOURCE_PATH[6:] + ".uasset")
    source_hash = hashlib.sha256(source_file.read_bytes()).hexdigest()
    dynamic = u.DynamicMesh()
    _, outcome = u.GeometryScript_AssetUtils.copy_mesh_from_static_mesh_v2(
        source, dynamic, u.GeometryScriptCopyMeshFromAssetOptions(), u.GeometryScriptMeshReadLOD(), False)
    if outcome != u.GeometryScriptOutcomePins.SUCCESS:
        raise RuntimeError("Geometry Script could not read the imported walnut")
    _, components = u.GeometryScript_MeshDecomposition.split_mesh_by_vertex_overlap(dynamic, None, .001)
    if len(components) != 13:
        raise RuntimeError("The imported walnut collection changed; inspect its groups before deriving meshes")
    assets = {}
    details = []
    lib = u.EditorAssetLibrary
    for name, index, triangles, expected_center in PARTS:
        part = components[index]
        box = u.GeometryScript_MeshQueries.get_mesh_bounding_box(part)
        center = (box.min + box.max) * .5
        if part.get_triangle_count() != triangles or (center-u.Vector(*expected_center)).length() > .02:
            raise RuntimeError("Unexpected spatial part for " + name + "; source needs inspection")
        path = FOLDER + "/" + name
        mesh = u.load_asset(path)
        created = mesh is None
        if mesh:
            marker = lib.get_metadata_tag(mesh, TAG)
            if not isinstance(mesh, u.StaticMesh) or not marker:
                raise RuntimeError("Derived walnut path is occupied by an unrelated asset: " + path)
            prior = json.loads(marker)
            if prior.get("source_sha256") != source_hash or prior.get("component") != index:
                raise RuntimeError("Existing walnut derivative has a different source; preserve it and inspect before replacing")
        else:
            u.GeometryScript_MeshTransforms.translate_mesh(part, -center)
            options = u.GeometryScriptCreateNewStaticMeshAssetOptions()
            options.set_editor_property("enable_collision", False)
            mesh, outcome = u.GeometryScript_NewAssetUtils.create_new_static_mesh_asset_from_mesh(part, path, options)
            if not mesh or outcome != u.GeometryScriptOutcomePins.SUCCESS:
                raise RuntimeError("Could not create centered walnut mesh: " + path)
            copy = u.GeometryScriptCopyMeshToAssetOptions()
            copy.set_editor_property("replace_materials", True)
            copy.set_editor_property("new_materials", [source.get_material(0)])
            copy.set_editor_property("new_material_slot_names", ["Walnut"])
            copy.set_editor_property("use_build_scale", False)
            copy.set_editor_property("generate_lightmap_u_vs", u.GeometryScriptGenerateLightmapUVOptions.DO_NOT_GENERATE_LIGHTMAP_U_VS)
            _, outcome = u.GeometryScript_AssetUtils.copy_mesh_to_static_mesh(part, mesh, copy, u.GeometryScriptMeshWriteLOD(), False)
            if outcome != u.GeometryScriptOutcomePins.SUCCESS:
                raise RuntimeError("Could not preserve walnut UV/material data: " + path)
            lib.set_metadata_tag(mesh, TAG, json.dumps(dict(tool="isolate_walnut.py", version=1, source=SOURCE_PATH,
                source_sha256=source_hash, component=index, triangles=triangles, original_center=[center.x,center.y,center.z])))
            if not lib.save_loaded_asset(mesh, only_if_is_dirty=False):
                raise RuntimeError("Could not save derived walnut mesh: " + path)
        assets[name] = mesh
        b = mesh.get_bounding_box()
        midpoint = (b.min+b.max)*.5
        if midpoint.length() > .02:
            raise RuntimeError("Derived walnut pivot is no longer centered: " + path)
        details.append(dict(asset=mesh.get_path_name(), component=index, triangles=triangles, created=created,
            bounds_min=[b.min.x,b.min.y,b.min.z], bounds_max=[b.max.x,b.max.y,b.max.z]))
    after = source_package_hashes()
    if before != after:
        raise RuntimeError("Walnut derivation changed imported Fab packages")
    report = dict(complete=True, imported_packages_unchanged=True, source_hashes=before, spatial_groups=13, meshes=details)
    report_path = Path(u.Paths.project_saved_dir()) / "NutRain/IsolatedMeshes.json"
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    u.log("MC_WALNUT_ISOLATION_PASS " + json.dumps(report))
    return assets


if __name__ == "__main__":
    ensure_walnut_assets()
