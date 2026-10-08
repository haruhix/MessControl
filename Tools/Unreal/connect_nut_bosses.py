"""Persist the two authored boss rigs/clips in the existing nut event profile.

Run through the Unreal/MCP console after the boss Editor build and imports:
    py "E:/DEVGAME/MessControl/Tools/Unreal/connect_nut_bosses.py"
Saves DA_NutRain and, when loaded, its known artist reference in L_Mouth.
It does not replace imported art or retune encounter difficulty.
"""
import json
import math
from pathlib import Path

import unreal as u


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def measure_cast_offset(mesh, animation, height, release_fraction, model_yaw=-90):
    """Use the imported release pose, in the same space as the native model."""
    options = u.AnimPoseEvaluationOptions()
    options.set_editor_property("optional_skeletal_mesh", mesh)
    options.set_editor_property("extract_root_motion", False)
    pose = u.AnimPoseExtensions.get_anim_pose_at_time(
        animation, animation.get_editor_property("sequence_length") * release_fraction, options)
    hand = u.AnimPoseExtensions.get_bone_pose(pose, "hand_r", u.AnimPoseSpaces.WORLD)
    palm = u.MathLibrary.transform_location(hand, u.Vector(0, 8, 0))
    bounds = mesh.get_bounds()
    origin = bounds.get_editor_property("origin")
    extent = bounds.get_editor_property("box_extent")
    scale = height / (2 * extent.z)
    # Match FMCNutBossSettings::BodyRadiusForRole while keeping the model feet
    # at the floor. The server emitter is actor-local and never reads live bones.
    body_radius = max(60.0, min(260.0, height * .45))
    x, y = (palm.x - origin.x) * scale, (palm.y - origin.y) * scale
    yaw = math.radians(model_yaw)
    return u.Vector(x * math.cos(yaw) - y * math.sin(yaw),
                    x * math.sin(yaw) + y * math.cos(yaw),
                    (palm.z - origin.z) * scale + height * .5 - body_radius)


def main():
    levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
    require(not levels.is_in_play_in_editor(), "Stop PIE before connecting the authored bosses")
    dirty = list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    dirty += list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())
    require(not dirty, "Preserve unsaved packages before connecting bosses: " +
            ", ".join(package.get_name() for package in dirty))
    lib = u.EditorAssetLibrary
    profile = u.load_asset("/Game/Gameplay/CoreLoop/DA_NutRain")
    require(isinstance(profile, u.MCNutRainProfile), "The existing nut encounter profile is missing")
    settings = profile.get_editor_property("settings")
    boss = settings.get_editor_property("boss")
    bindings = {}
    bone_sets = []
    for role, folder, mesh_name, clips in (
            ("tank", "NutTank", "SK_NutTank",
             (("idle", "Idle"), ("walk", "Walk"), ("walk_left", "WalkLeft"),
              ("walk_right", "WalkRight"), ("melee", "Melee"),
              ("jump", "Jump"), ("transform", "Transform"))),
            ("mage", "NutWizard", "SK_NutWizard",
             (("idle", "Idle"), ("walk", "Walk"), ("cast", "Cast"),
              ("heavy_cast", "HeavyCast"), ("summon", "Summon"),
              ("rain", "Rain"), ("hit", "Hit"), ("death", "Death")))):
        base = "/Game/Gameplay/CoreLoop/" + folder
        mesh = u.load_asset(base + "/" + mesh_name)
        require(isinstance(mesh, u.SkeletalMesh), "Required boss rig is missing: " + base)
        skeleton = mesh.get_editor_property("skeleton")
        require(skeleton, "Boss rig has no skeleton: " + base)
        bone_sets.append({str(name) for name in skeleton.get_reference_pose().get_bone_names()})
        key = role + "_skeletal_mesh"
        boss.set_editor_property(key, mesh)
        bindings[key] = mesh.get_path_name()
        prefix = "A_NutTank_" if role == "tank" else "A_NutWizard_"
        for field, suffix in clips:
            clip = u.load_asset(base + "/Animations/" + prefix + suffix)
            require(isinstance(clip, u.AnimSequence) and
                    clip.get_editor_property("skeleton") == skeleton,
                    "Missing or incompatible boss clip: " + prefix + suffix)
            key = role + "_" + field + "_animation"
            boss.set_editor_property(key, clip)
            bindings[key] = clip.get_path_name()
    # Unreal sanitizes Blender's dotted limb names during FBX import. Persist
    # the actual shared names rather than leaving a source-rig naming override.
    for key, name in (("torso_bone", "root_x"), ("head_bone", "head_x"),
                      ("left_arm_bone", "arm_stretch_l"), ("right_arm_bone", "arm_stretch_r"),
                      ("left_forearm_bone", "forearm_stretch_l"), ("right_forearm_bone", "forearm_stretch_r"),
                      ("left_hand_bone", "hand_l"), ("right_hand_bone", "hand_r")):
        require(all(name in names for names in bone_sets), "Missing shared procedural boss bone: " + name)
        boss.set_editor_property(key, name)
    ball = u.load_asset("/Game/Gameplay/CoreLoop/NutTank/SM_NutTank_Ball")
    require(isinstance(ball, u.StaticMesh), "The tank's ball mesh is missing")
    boss.set_editor_property("tank_ball_mesh", ball)
    bindings["tank_ball_mesh"] = ball.get_path_name()
    boss.set_editor_property("tank_model_yaw", -90)
    boss.set_editor_property("mage_model_yaw", -90)
    mage = u.load_asset("/Game/Gameplay/CoreLoop/NutWizard/SK_NutWizard")
    cast = u.load_asset("/Game/Gameplay/CoreLoop/NutWizard/Animations/A_NutWizard_Cast")
    cast_offset = measure_cast_offset(mage, cast, boss.get_editor_property("mage_height"),
                                      boss.get_editor_property("mage_cast_release_fraction"),
                                      boss.get_editor_property("mage_model_yaw"))
    size_ratio = float(boss.get_editor_property("mage_height")) / 200.0
    require(40 * size_ratio < cast_offset.x < 220 * size_ratio
            and abs(cast_offset.y) < 120 * size_ratio and abs(cast_offset.z) < 140 * size_ratio,
            "Inspect the imported caster orientation before binding its palm: " + str(cast_offset))
    boss.set_editor_property("mage_cast_offset", cast_offset)
    settings.set_editor_property("boss", boss)
    profile.set_editor_property("settings", settings)
    require(lib.save_loaded_asset(profile, only_if_is_dirty=False), "Cannot save DA_NutRain")

    # Keep the artist's original display mesh available in the editor. Gameplay
    # spawns the actual enemy, so the display copy must not obstruct that fight.
    reference_path = "/Game/Maps/L_Mouth.L_Mouth:PersistentLevel.SkeletalMeshActor_11"
    reference = u.find_object(None, reference_path)
    reference_saved = False
    if isinstance(reference, u.SkeletalMeshActor):
        component = reference.get_editor_property("skeletal_mesh_component")
        source = component.get_editor_property("skeletal_mesh")
        if source and source.get_path_name() == "/Game/FromBlender8/SM_Nut_Tank.SM_Nut_Tank":
            reference.modify()
            component.modify()
            reference.set_actor_hidden_in_game(True)
            component.set_collision_enabled(u.CollisionEnabled.NO_COLLISION)
            require(levels.save_current_level(), "Cannot save the known artist reference in L_Mouth")
            reference_saved = True
    report = dict(saved=True, profile=profile.get_path_name(), bindings=bindings,
                  mage_cast_offset_cm=[cast_offset.x, cast_offset.y, cast_offset.z],
                  artist_reference_hidden_in_game=reference_saved,
                  gameplay_validation="Deferred; this script only authors package bindings")
    output = Path(u.Paths.project_saved_dir()) / "Checks/NutBossConnections.json"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2), encoding="utf8")
    u.log("MC_NUT_BOSS_CONNECT_PASS " + json.dumps(report))


if __name__ == "__main__":
    main()
