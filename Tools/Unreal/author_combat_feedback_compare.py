"""Author an isolated OLD/NEW combat-feedback map after native Live Coding.

Run in an idle Unreal Editor:
    py "E:/DEVGAME/MessControl/Tools/Unreal/author_combat_feedback_compare.py"

The World is authored while inactive, using the same explicit-destination
duplication workflow as create_vfx_lab.py. The currently open map, its selection,
and unrelated dirty packages are preserved. Only the comparison map is saved;
no new_level/load_level, save-all, source-asset edits, or L_Mouth writes occur.
"""
import json
import math

import unreal as u


MAP_PATH = "/Game/Tests/L_CombatImpactCompare"
MAP_NAME = "L_CombatImpactCompare"
TAG = "MCCombatFeedbackCompareAuthored"
OVERVIEW_TAG = "MCCombatFeedbackOverview"
TEMPLATE_PREFIX = "CombatImpactCompareTemplate_"
PLAYER_CLASS_PATH = "/Game/Blueprints/BP_PlayerCharacter.BP_PlayerCharacter_C"
VIEW_LOCATION = (1350.0, 0.0, 750.0)
VIEW_TARGET = (0.0, 0.0, 95.0)
LANES = (("OLD", True, 425.0), ("NEW", False, -425.0))


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def dirty_packages():
    packages = list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())
    packages += list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    return {package.get_name() for package in packages}


def main():
    editor = u.get_editor_subsystem(u.UnrealEditorSubsystem)
    actor_tools = u.get_editor_subsystem(u.EditorActorSubsystem)
    level_editor = u.get_editor_subsystem(u.LevelEditorSubsystem)
    lib = u.EditorAssetLibrary
    require(not level_editor.is_in_play_in_editor(), "Stop PIE before authoring the comparison map")

    current_world = editor.get_editor_world()
    require(current_world is not None, "No editor World is available")
    current_path = current_world.get_path_name()
    require(not current_path.startswith(MAP_PATH + "."),
            "Keep the comparison map inactive while authoring; open another map first")
    selected = list(actor_tools.get_selected_level_actors())
    dirty_before = dirty_packages()
    require(MAP_PATH not in dirty_before,
            "Comparison map has unsaved edits; preserve them before rerunning the author script")

    # Resolve every dependency before creating or changing the target World.
    # load_class uses the live UClass, including classes registered by Live Coding.
    def native_class(name):
        result = u.load_class(None, "/Script/MessControl." + name)
        require(result is not None, name + " is unavailable; finish native compilation first")
        return result

    floor_class = native_class("MCVFXLabFloor")
    station_class = native_class("MCCombatFeedbackStation")
    game_mode_class = native_class("MCCombatFeedbackGameMode")
    player_class = u.load_class(None, PLAYER_CLASS_PATH)
    require(player_class is not None, "Missing player Blueprint: " + PLAYER_CLASS_PATH)
    station_defaults = u.get_default_object(station_class)
    for property_name in ("bLegacyFeedback", "Floor", "SubjectClass", "bAutoRun",
                          "RepeatSeconds", "EnemyDamage", "PlayerDamage",
                          "SubjectOffset", "EnemyOffset"):
        station_defaults.get_editor_property(property_name)
    support_mesh = lib.load_asset("/Game/Tests/VFXLab/SM_LabSupport")
    floor_material = lib.load_asset("/Game/Tests/VFXLab/M_LabFloor")
    ambient_cube = lib.load_asset("/Engine/MapTemplates/Sky/DaylightAmbientCubemap")
    require(support_mesh is not None, "Missing saved VFX lab support mesh")
    require(support_mesh.get_editor_property("allow_cpu_access"),
            "Saved lab support must have Allow CPU Access enabled for the tongue surface")
    require(floor_material is not None and ambient_cube is not None,
            "Missing saved lab floor material or engine ambient cubemap")
    require(MAP_PATH not in dirty_packages(), "Preserve unsaved comparison map changes")

    world = lib.load_asset(MAP_PATH) if lib.does_asset_exist(MAP_PATH) else None
    if world is None:
        factory = u.WorldFactory()
        factory.set_editor_property("edit_after_new", False)
        world = u.AssetToolsHelpers.get_asset_tools().create_asset(
            MAP_NAME, "/Game/Tests", u.World, factory)
    require(world is not None and world != current_world, "Comparison World must remain inactive")
    require(world.get_path_name() == MAP_PATH + "." + MAP_NAME, "Unexpected target World")
    level = world.get_world_settings().get_outer()
    require(level.get_outer() == world, "Cannot resolve target persistent level")

    created = []
    rows = []
    try:
        # A rerun replaces only actors owned by this script. Hand-authored actors
        # in the saved comparison map are left in place.
        for existing in list(u.GameplayStatics.get_all_actors_of_class(world, u.Actor)):
            if (TAG in [str(tag) for tag in existing.tags]
                    or existing.get_name().startswith(TEMPLATE_PREFIX)):
                existing.destroy_actor()

        def actor(actor_class, name, location, rotation=None, scale=(1.0, 1.0, 1.0)):
            # Editor spawn helpers always target the open map. Duplication accepts
            # the inactive destination World and registers components there.
            template = u.new_object(actor_class, outer=level, name=TEMPLATE_PREFIX + name)
            result = actor_tools.duplicate_actor(template, world, u.Vector())
            template.destroy_actor()
            require(result is not None and result.get_world() == world, "Cannot author " + name)
            result.set_actor_label("CombatImpactCompare_" + name)
            result.set_folder_path("CombatImpactCompare/" + name.split("_")[0])
            result.tags = [u.Name(TAG)]
            result.set_actor_location(u.Vector(*location), False, False)
            result.set_actor_rotation(rotation if rotation is not None else u.Rotator(), False)
            result.set_actor_scale3d(u.Vector(*scale))
            created.append(result)
            return result

        def text(name, words, location, size=34.0):
            result = actor(u.TextRenderActor, name, location)
            component = result.text_render
            component.set_mobility(u.ComponentMobility.MOVABLE)
            component.set_text(words)
            component.set_world_size(size)
            component.set_horizontal_alignment(u.HorizTextAligment.EHTA_CENTER)
            component.set_vertical_alignment(u.VerticalTextAligment.EVRTA_TEXT_CENTER)
            component.set_text_render_color(u.Color(235, 242, 255, 255))
            component.set_collision_enabled(u.CollisionEnabled.NO_COLLISION)
            component.set_cast_shadow(False)
            return result

        for name, legacy, lane_y in LANES:
            floor = actor(floor_class, name + "_Support", (0.0, lane_y, -20.0),
                          scale=(9.0, 7.0, 0.4))
            floor.set_editor_property("SourceMesh", support_mesh)
            floor.set_editor_property("SurfaceMaterial", floor_material)
            floor.set_editor_property("bAutomaticYawns", False)
            floor.call_method("RebuildSurface")

            station = actor(station_class, name + "_Station", (0.0, lane_y, 0.0),
                            u.Rotator(pitch=0.0, yaw=90.0, roll=0.0))
            station.tags = [u.Name(TAG), u.Name("MCVFXLabStation"), u.Name("CombatFeedback" + name)]
            station.get_component_by_class(u.TextRenderComponent).set_editor_property(
                "relative_rotation", u.Rotator(pitch=0.0, yaw=-90.0, roll=0.0))
            for key, value in {
                "bLegacyFeedback": legacy,
                "Floor": floor,
                "SubjectClass": player_class,
                "bAutoRun": True,
                "RepeatSeconds": 4.0,
                "EnemyDamage": 25.0,
                "PlayerDamage": 15.0,
                "SubjectOffset": u.Vector(-95.0, 0.0, 0.0),
                "EnemyOffset": u.Vector(95.0, 0.0, 0.0),
            }.items():
                station.set_editor_property(key, value)
            require(station.get_editor_property("bLegacyFeedback") == legacy,
                    name + " feedback mode was not assigned")
            title = text(name + "_Title", name + (" / BEFORE" if legacy else " / AFTER"),
                         (-260.0, lane_y, 565.0), 48.0)
            title.tags = [u.Name(TAG), u.Name("CombatFeedback" + name + "Title")]
            subtitle = text(name + "_Subtitle", "F3: DAMAGE / SHIELD / REAL BOSS ATTACK",
                            (-260.0, lane_y, 475.0), 20.0)
            subtitle.tags = [u.Name(TAG), u.Name("CombatFeedback" + name + "Subtitle")]
            rows.append({"mode": name, "legacy": legacy, "center": [0.0, lane_y, 0.0],
                         "station": station.get_path_name(), "floor": floor.get_path_name()})

        pitch = math.degrees(math.atan2(VIEW_TARGET[2] - VIEW_LOCATION[2],
                                        VIEW_LOCATION[0] - VIEW_TARGET[0]))
        view_rotation = u.Rotator(pitch=pitch, yaw=180.0, roll=0.0)
        camera = actor(u.CameraActor, "OverviewCamera", VIEW_LOCATION, view_rotation)
        camera.tags = [u.Name(TAG), u.Name(OVERVIEW_TAG)]
        camera.camera_component.set_editor_property("projection_mode", u.CameraProjectionMode.ORTHOGRAPHIC)
        camera.camera_component.set_editor_property("ortho_width", 2000.0)
        camera.camera_component.set_editor_property("field_of_view", 70.0)
        camera.camera_component.set_editor_property("constrain_aspect_ratio", False)
        # The comparison controller selects this tagged view and returns the
        # spectator pawn when leaving either possessed test subject.
        actor(u.PlayerStart, "PlayerStart", VIEW_LOCATION, view_rotation)
        text("Instructions", "F3: DAMAGE / BOSS PANEL   1: OLD   2: NEW   0: OVERVIEW\nR: RESET   SPACE: BASIC ENEMY LOOP",
             (-370.0, 0.0, 670.0), 24.0)

        sun = actor(u.DirectionalLight, "Lighting_Key", (0.0, 0.0, 1700.0),
                    u.Rotator(pitch=-45.0, yaw=180.0, roll=0.0))
        sun.light_component.set_mobility(u.ComponentMobility.MOVABLE)
        sun.light_component.set_intensity(18.0)
        sun.light_component.set_light_color(u.LinearColor(0.98, 0.98, 1.0, 1.0))
        sky = actor(u.SkyLight, "Lighting_Ambient", (0.0, 0.0, 1200.0))
        sky.light_component.set_mobility(u.ComponentMobility.MOVABLE)
        sky.light_component.set_editor_property("source_type", u.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
        sky.light_component.set_editor_property("cubemap", ambient_cube)
        sky.light_component.set_editor_property("real_time_capture", False)
        sky.light_component.set_intensity(1.0)
        fill = actor(u.DirectionalLight, "Lighting_Fill", (0.0, 0.0, 1500.0),
                     u.Rotator(pitch=-35.0, yaw=0.0, roll=0.0))
        fill.light_component.set_mobility(u.ComponentMobility.MOVABLE)
        fill.light_component.set_intensity(8.0)
        fill.light_component.set_cast_shadows(False)
        actor(u.SkyAtmosphere, "Lighting_Sky", (0.0, 0.0, 0.0))

        post = actor(u.PostProcessVolume, "Lighting_Exposure", (0.0, 0.0, 0.0))
        settings = u.PostProcessSettings()
        for key, value in {
            "override_auto_exposure_min_brightness": True, "auto_exposure_min_brightness": 1.0,
            "override_auto_exposure_max_brightness": True, "auto_exposure_max_brightness": 1.0,
            "override_auto_exposure_bias": True, "auto_exposure_bias": 1.0,
            "override_bloom_intensity": True, "bloom_intensity": 0.1,
            "override_lens_flare_intensity": True, "lens_flare_intensity": 0.0,
            "override_vignette_intensity": True, "vignette_intensity": 0.0,
            "override_motion_blur_amount": True, "motion_blur_amount": 0.0,
        }.items():
            settings.set_editor_property(key, value)
        post.set_editor_property("settings", settings)
        post.set_editor_property("unbound", True)
        world.get_world_settings().set_editor_property("default_game_mode", game_mode_class)

        require(editor.get_editor_world() == current_world, "Active editor map changed during authoring")
        require(dirty_before.issubset(dirty_packages()), "An unrelated dirty package was unexpectedly saved")
        require(lib.save_loaded_asset(world, only_if_is_dirty=False), "Comparison map save failed")
        require(editor.get_editor_world().get_path_name() == current_path, "Active editor map changed after save")
        require(dirty_before.issubset(dirty_packages()), "Unrelated unsaved edits were not preserved")
    finally:
        actor_tools.set_selected_level_actors(selected)

    print("COMBAT_FEEDBACK_COMPARE " + json.dumps({
        "saved_map": world.get_path_name(),
        "current_map_unchanged": current_path,
        "unrelated_dirty_packages_preserved": sorted(dirty_before),
        "authored_actor_count": len(created),
        "lanes": rows,
        "overview_camera": camera.get_path_name(),
        "game_mode": game_mode_class.get_path_name(),
        "lane_spacing_cm": 850.0,
        "source_assets_saved": False,
        "main_arena_saved": False,
    }, ensure_ascii=False))


main()
