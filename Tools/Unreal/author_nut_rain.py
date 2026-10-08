"""Author the nut event's own profile/menu and a readable hostile eye material.

Run in Unreal after compiling MCNutRainProfile. The derived whole walnut and
single shell/kernel fragments replace only this tool's old collection references.
Imported Fab packages and the breakfast menu remain unchanged. Re-running preserves
designer tuning; old collection scales are converted to the same maximum size.
"""
import hashlib
import importlib
import json
import math
import sys
from pathlib import Path

import unreal as u


def migrate_boss_health(boss):
    """Replace only known generated health values; preserve designer tuning."""
    migrated = {}
    for key, old, new in (("tank_health", 2000.0, 1400.0),
                          ("mage_health", 1600.0, 1100.0),
                          ("extra_player_health", .8, .65)):
        current = float(boss.get_editor_property(key))
        # Unreal's float32 representation of .8 differs slightly from Python's float.
        if math.isclose(current, old, rel_tol=0.0, abs_tol=1e-7):
            boss.set_editor_property(key, new)
            migrated[key] = dict(previous=current, current=new)
    return migrated


def main():
    if u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor():
        raise RuntimeError("Stop the owned PIE session before authoring nut assets")
    dirty = list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    dirty += list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())
    if dirty:
        raise RuntimeError("Preserve unsaved packages before nut authoring: " + ", ".join(p.get_name() for p in dirty))
    folder = "/Game/Gameplay/CoreLoop"
    lib = u.EditorAssetLibrary
    tools = u.AssetToolsHelpers.get_asset_tools()
    mesh_path = "/Game/Fab/Stylized_Walnut_-_Game_Ready/stylized_walnut_game_ready/StaticMeshes/stylized_walnut_game_ready"
    mesh = u.load_asset(mesh_path)
    if not isinstance(mesh, u.StaticMesh):
        raise RuntimeError("Required imported walnut is missing: " + mesh_path)
    source_file = Path(u.Paths.project_content_dir()) / (mesh_path[6:] + ".uasset")
    source_hash = hashlib.sha256(source_file.read_bytes()).hexdigest()
    sys.path.insert(0, str(Path(u.Paths.project_dir()) / "Tools/Unreal"))
    import isolate_walnut
    importlib.reload(isolate_walnut)
    from isolate_walnut import ensure_walnut_assets, max_half_extent
    isolated = ensure_walnut_assets()
    whole = isolated["SM_Walnut_Whole"]
    fragments = [isolated["SM_Walnut_HalfShell"], isolated["SM_Walnut_Kernel"]]

    profile_path = folder + "/DA_NutRain"
    profile = u.load_asset(profile_path)
    if not profile:
        factory = u.DataAssetFactory()
        factory.set_editor_property("data_asset_class", u.MCNutRainProfile)
        profile = tools.create_asset("DA_NutRain", folder, u.MCNutRainProfile, factory)
    if not isinstance(profile, u.MCNutRainProfile):
        raise RuntimeError("Nut profile path has an unexpected asset type")
    # Migrate this authored first encounter from the old dense 12-second storm.
    settings = profile.get_editor_property("settings")
    for key, value in dict(boss_encounter=True, minimum_series=4, maximum_series=8,
                           minimum_nuts_per_series=2, maximum_nuts_per_series=4,
                           minimum_drop_seconds=1.5, maximum_drop_seconds=2.5,
                           series_rest_seconds=4.0, rain_seconds=40.0,
                           large_nut_height=180.0, impact_damage_limit=18.0, enemy_count=4, enemies_per_extra_player=1).items():
        settings.set_editor_property(key, value)
    enemy = settings.get_editor_property("enemy")
    for key, value in dict(max_health=40.0, move_speed=135.0, attack_damage=8.0,
                           windup_seconds=.75, attack_cooldown=2.3, body_radius=35.0).items():
        enemy.set_editor_property(key, value)
    settings.set_editor_property("enemy", enemy)
    boss = settings.get_editor_property("boss")
    health_migrations = migrate_boss_health(boss)
    boss.set_editor_property("creep_health", 40.0)
    boss.set_editor_property("whole_mesh", whole)
    boss.set_editor_property("shell_mesh", fragments[0])
    boss.set_editor_property("kernel_mesh", fragments[1])
    settings.set_editor_property("boss", boss)
    profile.set_editor_property("settings", settings)

    menu_path = folder + "/DT_NutRainMenu"
    table = u.load_asset(menu_path)
    if not table:
        factory = u.DataTableFactory()
        factory.set_editor_property("struct", u.load_object(None, "/Script/MessControl.MCFoodRow"))
        table = tools.create_asset("DT_NutRainMenu", folder, u.DataTable, factory)
    if not isinstance(table, u.DataTable):
        raise RuntimeError("Nut menu path has an unexpected asset type")
    rows = json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(table))
    migrated = False
    walnut = next((row for row in rows if row["Name"] == "Walnut"), None)
    if walnut is None:
        settings = profile.get_editor_property("settings")
        radius = settings.get_editor_property("enemy").get_editor_property("body_radius")
        scale = radius / max(.001, max_half_extent(whole))
        rows.append({
            "Name": "Walnut", "Label": "WALNUT",
            "WholeMeshes": [whole.get_path_name()], "FragmentMeshes": [m.get_path_name() for m in fragments],
            "Scale": dict(X=scale, Y=scale, Z=scale),
            "FragmentScale": dict(X=scale*.55, Y=scale*.55, Z=scale*.55),
            "Resistance": "Hard", "Kind": "Food", "Health": 90,
            "Mass": 7, "SelectionWeight": 1, "Fragments": 3, "SpoilSeconds": 600,
            "AbsorbSeconds": 10, "HalfExtent": dict(X=42, Y=42, Z=42),
            "Collision": dict(bAutoOptimize=True, WholeHullLimit=1, FragmentHullLimit=2,
                              HullVertexLimit=256, VoxelResolution=100000),
        })
        if not u.DataTableFunctionLibrary.fill_data_table_from_json_string(table, json.dumps(rows)):
            raise RuntimeError("Could not author the walnut food row")
    else:
        # Convert only references this tool authored previously; custom mesh choices and all task tuning survive.
        normalize = lambda path: path.split(".")[0]
        source_reference = normalize(mesh.get_path_name())
        if [normalize(p) for p in walnut["WholeMeshes"]] == [source_reference]:
            ratio = max_half_extent(mesh) / max(.001, max_half_extent(whole))
            walnut["WholeMeshes"] = [whole.get_path_name()]
            walnut["Scale"] = {axis: value*ratio for axis,value in walnut["Scale"].items()}
            migrated = True
        if [normalize(p) for p in walnut["FragmentMeshes"]] == [source_reference]:
            ratio = max_half_extent(mesh) / max(.001, max(max_half_extent(m) for m in fragments))
            walnut["FragmentMeshes"] = [m.get_path_name() for m in fragments]
            walnut["FragmentScale"] = {axis: value*ratio for axis,value in walnut["FragmentScale"].items()}
            migrated = True
        if migrated:
            # One convex whole hull uses 98 support vertices at the intended 42 cm radius.
            # Change only the earlier generated collection policy; preserve designer policies.
            if walnut["Collision"] == dict(bAutoOptimize=True, WholeHullLimit=2, FragmentHullLimit=2,
                                           HullVertexLimit=32, VoxelResolution=100000):
                walnut["Collision"]["WholeHullLimit"] = 1
                walnut["Collision"]["HullVertexLimit"] = 256
            walnut["CollisionData"] = []
            if not u.DataTableFunctionLibrary.fill_data_table_from_json_string(table, json.dumps(rows)):
                raise RuntimeError("Could not replace the collection with individual walnut meshes")
    walnut = next(row for row in rows if row["Name"] == "Walnut")
    bounds = whole.get_bounding_box()
    height = bounds.max.z - bounds.min.z
    scale = settings.get_editor_property("large_nut_height") / max(.001, height)
    fragment_scale = boss.get_editor_property("creep_height") / max(.001, max(m.get_bounding_box().max.z-m.get_bounding_box().min.z for m in fragments))
    walnut["Scale"] = dict(X=scale, Y=scale, Z=scale)
    walnut["FragmentScale"] = dict(X=fragment_scale, Y=fragment_scale, Z=fragment_scale)
    walnut["WholeMeshes"] = [whole.get_path_name()]
    walnut["FragmentMeshes"] = [m.get_path_name() for m in fragments]
    walnut["SpoilSeconds"] = 600
    if not u.DataTableFunctionLibrary.fill_data_table_from_json_string(table, json.dumps(rows)):
        raise RuntimeError("Could not resize event walnuts to player height")
    if not u.MCFoodCollisionEditorLibrary.bake_menu_collision(table, True):
        raise RuntimeError("Walnut food collision bake failed")
    if not u.MCFoodCollisionEditorLibrary.is_menu_current(table):
        raise RuntimeError("Walnut menu collision is not current")
    if not lib.save_loaded_asset(table, only_if_is_dirty=False):
        raise RuntimeError("Could not save DT_NutRainMenu")
    profile.set_editor_property("menu", table)
    profile.set_editor_property("nut_row", "Walnut")
    if not lib.save_loaded_asset(profile, only_if_is_dirty=False):
        raise RuntimeError("Could not save DA_NutRain")

    eyes_path = folder + "/M_NutEnemyEyes"
    eyes = u.load_asset(eyes_path)
    if not eyes:
        eyes = tools.create_asset("M_NutEnemyEyes", folder, u.Material, u.MaterialFactoryNew())
        eyes.set_editor_property("shading_model", u.MaterialShadingModel.MSM_UNLIT)
        color = u.MaterialEditingLibrary.create_material_expression(eyes, u.MaterialExpressionConstant3Vector)
        color.set_editor_property("constant", u.LinearColor(4.0, .07, .015, 1))
        if not u.MaterialEditingLibrary.connect_material_property(color, "", u.MaterialProperty.MP_EMISSIVE_COLOR):
            raise RuntimeError("Could not connect hostile walnut eye material")
        u.MaterialEditingLibrary.recompile_material(eyes)
        if not lib.save_loaded_asset(eyes, only_if_is_dirty=False):
            raise RuntimeError("Could not save hostile walnut eye material")
    if hashlib.sha256(source_file.read_bytes()).hexdigest() != source_hash:
        raise RuntimeError("Nut authoring changed the imported walnut package")
    report = dict(complete=True, profile=profile.get_path_name(), menu=table.get_path_name(),
                  source_unchanged=True, migrated_collection_row=migrated,
                  migrated_boss_health=health_migrations,
                  meshes={name:asset.get_path_name() for name,asset in isolated.items()},
                  settings=str(profile.get_editor_property("settings")))
    report_path = Path(u.Paths.project_saved_dir()) / "NutRain" / "Authoring.json"
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    u.log("MC_NUT_RAIN_AUTHOR_PASS " + json.dumps(report))


if __name__ == "__main__":
    main()
