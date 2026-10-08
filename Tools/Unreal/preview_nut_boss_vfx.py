"""Place one transient, cosmetic boss/VFX gallery in the existing L_Mouth Editor.

Run through MCP console: py "E:/DEVGAME/MessControl/Tools/Unreal/preview_nut_boss_vfx.py"
Then capture the viewport. Cleanup without saving:
    import sys; sys.path.insert(0, 'E:/DEVGAME/MessControl/Tools/Unreal')
    import preview_nut_boss_vfx; preview_nut_boss_vfx.cleanup()
No PIE, gameplay damage, camera changes or package/map saves are performed.
"""
import json
from pathlib import Path

import unreal as u


TAG = "MCNutBossVFXPreview"
REPORT = Path(u.Paths.project_saved_dir()) / "NutCombat/EditorPreview.json"
actors = u.get_editor_subsystem(u.EditorActorSubsystem)


def vector(value):
    return [float(value.x), float(value.y), float(value.z)]


def owned():
    world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
    # EditorActorSubsystem deliberately excludes RF_Transient actors.
    return [actor for actor in u.GameplayStatics.get_all_actors_of_class(world, u.Actor)
            if TAG in [str(tag) for tag in actor.get_editor_property("tags")]]


def cleanup():
    if u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor():
        raise RuntimeError("Stop PIE before removing the editor-only preview")
    removed = []
    for actor in reversed(owned()):
        if isinstance(actor, u.MCNutCombatEffect):
            actor.get_editor_property("sustained_particles").deactivate()
        path = actor.get_path_name()
        if not actors.destroy_actor(actor):
            raise RuntimeError("Cannot remove owned preview actor: " + path)
        removed.append(path)
    result = dict(removed=removed, remaining=[actor.get_path_name() for actor in owned()],
                  dirty_maps=[package.get_name() for package in u.EditorLoadingAndSavingUtils.get_dirty_map_packages()],
                  dirty_content=[package.get_name() for package in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()])
    u.log("MC_NUT_BOSS_VFX_PREVIEW_CLEANUP " + json.dumps(result))
    return result


def main():
    if u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor():
        raise RuntimeError("Preview requires an idle Editor, without PIE")
    if owned():
        raise RuntimeError("Remove the previous owned preview with cleanup() first")
    dirty = list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    dirty += list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())
    if dirty:
        raise RuntimeError("Preserve unsaved user edits before previewing")
    level_actors = actors.get_all_level_actors()
    tongue = next((actor for actor in level_actors if isinstance(actor, u.MCTongue)), None)
    if not tongue or "/Game/Maps/L_Mouth" not in tongue.get_path_name():
        raise RuntimeError("The current saved L_Mouth must contain a valid tongue")
    profile = u.load_asset("/Game/Gameplay/CoreLoop/DA_NutRain")
    boss_settings = profile.get_editor_property("settings").get_editor_property("boss")
    center, extent, _ = u.SystemLibrary.get_component_bounds(tongue.get_editor_property("surface"))
    anchor = center + u.Vector(-extent.x * .12, 0, 0)
    ignored = [actor for actor in level_actors if actor != tongue]
    records = []

    def floor(offset):
        point = anchor + offset
        hit = u.SystemLibrary.line_trace_single(tongue, point + u.Vector(0, 0, 2000),
                    point - u.Vector(0, 0, 2000), u.TraceTypeQuery.TRACE_TYPE_QUERY1,
                    True, ignored + owned(), u.DrawDebugTrace.NONE, False)
        # UE5.8 Python PackReturnValues removes the native success bool:
        # LineTraceSingle returns HitResult on success, None on failure.
        # HitResult HasNativeBreak is GameplayStatics.BreakHitResult, whose
        # output order starts BlockingHit and puts ImpactPoint at index 5.
        fields = hit.to_tuple() if hit is not None else None
        if fields is None or not fields[0]:
            raise RuntimeError("No tongue support at preview point: " + str(vector(point)))
        return fields[5]

    def spawn(kind, name, point):
        actor = actors.spawn_actor_from_class(kind, point, u.Rotator(), transient=True)
        if not actor:
            raise RuntimeError("Cannot create transient preview: " + name)
        actor.set_editor_property("tags", [TAG])
        actor.set_actor_label("PREVIEW | Nut VFX | " + name)
        actor.set_actor_tick_enabled(False)
        actor.set_actor_enable_collision(False)
        records.append(dict(name=name, actor=actor.get_path_name(), position=vector(point)))
        return actor

    def boss(name, role, point, mesh_path, clip_path, phase):
        height = float(boss_settings.get_editor_property("mage_height" if role == u.MCNutBossRole.MAGE else "tank_height"))
        body_radius = max(60.0, min(260.0, height * (.45 if role == u.MCNutBossRole.MAGE else 100.0 / 220.0)))
        actor = spawn(u.MCNutBoss, name, point + u.Vector(0, 0, body_radius))
        actor.set_editor_property("tongue", tongue)
        actor.set_editor_property("boss_role", role)
        actor.set_editor_property("boss_settings", boss_settings)
        actor.set_editor_property("locked_target", point + u.Vector(800, 0, body_radius))
        actor.set_editor_property("attack_forward", u.Vector(1, 0, 0))
        for component in actor.get_components_by_class(u.PrimitiveComponent):
            component.set_visibility(False)
            component.set_collision_enabled(u.CollisionEnabled.NO_COLLISION)
        model = actor.get_editor_property("mage_model" if role == u.MCNutBossRole.MAGE else "tank_model")
        mesh, clip = u.load_asset(mesh_path), u.load_asset(clip_path)
        if not isinstance(mesh, u.SkeletalMesh) or not isinstance(clip, u.AnimSequence):
            raise RuntimeError("Required generated rig/clip has not been imported: " + mesh_path)
        if mesh.get_editor_property("skeleton") != clip.get_editor_property("skeleton"):
            raise RuntimeError("Preview clip uses a different skeleton")
        model.set_skeletal_mesh_asset(mesh)
        # Both ARP source rigs face +Y; native boss presentation also applies
        # this model-space correction while gameplay/cast forward stays +X.
        model_rotation = u.Rotator(pitch=0, yaw=-90, roll=0)
        model.set_relative_rotation(model_rotation, False, True)
        bounds = mesh.get_bounds()
        model_extent = bounds.get_editor_property("box_extent")
        height = 200 if role == u.MCNutBossRole.MAGE else 220
        scale = height / max(1., model_extent.z * 2)
        model.set_relative_scale3d(u.Vector(scale, scale, scale))
        model_center = u.MathLibrary.rotate_angle_axis(bounds.get_editor_property("origin") * scale,
                                                       -90, u.Vector(0, 0, 1))
        model.set_world_location(point + u.Vector(0, 0, height * .5) - model_center, False, True)
        model.set_visibility(True)
        model.set_animation_mode(u.AnimationMode.ANIMATION_SINGLE_NODE, True)
        model.set_update_animation_in_editor(True)
        model.play_animation(clip, False)
        model.set_play_rate(0)
        model.set_position(clip.get_play_length() * phase, False)
        return actor

    def cue(name, source, kind, origin, target, radius, windup, active, age):
        actor = spawn(u.MCNutCombatEffect, name, target)
        actor.set_editor_property("tongue", tongue)
        actor.set_editor_property("source_actor", source)
        state = u.MCNutCombatCueState()
        for key, value in dict(role=source.get_editor_property("boss_role"), type=kind,
                origin=origin, target=target, radius=radius, windup_seconds=windup,
                active_seconds=active, started_at=0., seed=41, detail_radius=90.,
                drop_count=8, drop_cadence=4. / 7.).items():
            state.set_editor_property(key, value)
        actor.set_editor_property("cue", state)
        actor.preview_at_age(age)
        # Burst components finish normally and return to their native pool.
        # Only the public, actor-owned sustained component is held for capture.
        components = [actor.get_editor_property("sustained_particles")]
        for component in components:
            if component.get_asset():
                component.advance_simulation_by_time(min(age, .25), 1. / 30.)
                component.set_paused(True)
        records[-1].update(cue=str(kind), age=age)
        return actor

    try:
        tank_floor, mage_floor = floor(u.Vector(-110, -260, 0)), floor(u.Vector(-70, 260, 0))
        tank = boss("Tank warrior", u.MCNutBossRole.TANK, tank_floor,
                    "/Game/Gameplay/CoreLoop/NutTank/SK_NutTank",
                    "/Game/Gameplay/CoreLoop/NutTank/Animations/A_NutTank_Melee", .48)
        mage = boss("Wizard casting", u.MCNutBossRole.MAGE, mage_floor,
                    "/Game/Gameplay/CoreLoop/NutWizard/SK_NutWizard",
                    "/Game/Gameplay/CoreLoop/NutWizard/Animations/A_NutWizard_Cast", .55)
        mage_origin = mage.get_actor_location() + boss_settings.get_editor_property("mage_cast_offset")
        aim = floor(u.Vector(460, 240, 0))
        cue("Palm charge", mage, u.MCNutCombatCue.CAST_CHARGE, mage_origin, aim, 55, .9, .25, .65)
        cue("Directional release", mage, u.MCNutCombatCue.CAST_RELEASE, mage_origin, aim, 55, 0, .3, .08)
        cue("Nut rain", mage, u.MCNutCombatCue.NUT_RAIN, mage_origin, aim,
            float(boss_settings.get_editor_property("rain_radius")), 1.2, 4, 2.35)
        portal = floor(u.Vector(170, 20, 0))
        cue("Summon portal", mage, u.MCNutCombatCue.SUMMON_TELL, portal, portal, 65, 1.1, .3, .8)
        slam = floor(u.Vector(260, -280, 0))
        cue("Tank slam", tank, u.MCNutCombatCue.SLAM_IMPACT, slam, slam, 200, 0, .6, .16)

        ball_floor = floor(u.Vector(-350, -220, 0))
        ball_height = float(boss_settings.get_editor_property("tank_ball_height"))
        ball = spawn(u.MCNutBoss, "Rolling ball", ball_floor + u.Vector(0, 0, ball_height * .5))
        ball.set_editor_property("attack_forward", u.Vector(1, 0, 0))
        for component in ball.get_components_by_class(u.PrimitiveComponent):
            component.set_visibility(False)
            component.set_collision_enabled(u.CollisionEnabled.NO_COLLISION)
        part = ball.get_editor_property("tank_ball")
        mesh = u.load_asset("/Game/Gameplay/CoreLoop/NutTank/SM_NutTank_Ball")
        if not isinstance(mesh, u.StaticMesh):
            raise RuntimeError("Generated Tank ball has not been imported")
        part.set_static_mesh(mesh)
        _, ball_extent, _ = u.SystemLibrary.get_component_bounds(part)
        scale = ball_height / max(1., ball_extent.z * 2)
        part.set_relative_scale3d(u.Vector(scale, scale, scale))
        part.set_visibility(True)
        cue("Rolling aura", ball, u.MCNutCombatCue.ROLL_TELL, ball_floor,
            ball_floor + u.Vector(400, 0, 0), ball_height * .5 + 5, 1.2, 5, 2.)

        report = dict(created=True, cosmetic_only=True, actors=records, tongue=tongue.get_path_name(),
            recommended_camera=vector(anchor + u.Vector(-1050, -1300, 1050)),
            recommended_look_at=vector(anchor + u.Vector(140, 0, 230)),
            cleanup="import preview_nut_boss_vfx; preview_nut_boss_vfx.cleanup()",
            dirty_maps=[package.get_name() for package in u.EditorLoadingAndSavingUtils.get_dirty_map_packages()])
        REPORT.parent.mkdir(parents=True, exist_ok=True)
        REPORT.write_text(json.dumps(report, indent=2), encoding="utf8")
        u.log("MC_NUT_BOSS_VFX_EDITOR_PREVIEW_READY " + json.dumps(report))
        return report
    except Exception:
        cleanup()
        raise


if __name__ == "__main__":
    main()
