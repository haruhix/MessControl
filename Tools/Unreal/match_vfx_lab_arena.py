"""Deprecated opt-in: reproduce the previous arena-copy VFX lab layout.

The default L_VFXLab is an open, flat test area. This helper is not part of its
authoring flow. Explicitly pass --legacy-arena-copy only to reproduce the old
arena-reference layout; it restores scenery and lighting to the active lab.

Requires stopped PIE and saved maps. Original meshes/materials/light components
are duplicated by Unreal, so later lab edits cannot overwrite the arena actors.
"""
import sys
if '--legacy-arena-copy' not in sys.argv[1:]:
    raise RuntimeError('Deprecated arena-copy helper is disabled by default. Use author_vfx_lab_scenarios.py for the open lab; --legacy-arena-copy explicitly restores the previous layout.')
import unreal as u, json
from pathlib import Path
u.log_warning('Legacy arena-copy layout requested; this replaces the current open-lab scenery and lighting.')

LAB='/Game/Tests/L_VFXLab'
ARENA='/Game/Maps/L_Mouth'
PREFIX='MCVFXLab_Arena_'
editor=u.get_editor_subsystem(u.UnrealEditorSubsystem)
levels=u.get_editor_subsystem(u.LevelEditorSubsystem)
actors=u.get_editor_subsystem(u.EditorActorSubsystem)
lib=u.EditorAssetLibrary
assert editor.get_editor_world().get_path_name().startswith(LAB+'.')
assert not u.EditorLevelLibrary.get_pie_worlds(True), 'Stop PIE before authoring'
assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages(), 'Save map edits first'

def remove_previous_reference():
    # EditorActorSubsystem only destroys actors in the active editor world.
    # Remove the old reference before leaving the lab, rather than from an
    # inactive loaded UWorld where DestroyActor silently refuses the operation.
    for a in list(actors.get_all_level_actors()):
        label=a.get_actor_label()
        if label.startswith(PREFIX) or label in ('VFXLab_Sun','VFXLab_Fill_1','VFXLab_Fill_2'):
            assert actors.destroy_actor(a),label
    assert levels.save_current_level()

remove_previous_reference()

def copy_saved_reference():
    assert levels.load_level(ARENA)
    source=actors.get_all_level_actors()
    # Keep the current artist environment, including the roof that shades the arena.
    # Empty legacy mesh actors and artist boss/ice reference props are not scenery.
    scenery=[]
    for a in source:
        if not isinstance(a,u.StaticMeshActor): continue
        c=a.static_mesh_component
        if not c.static_mesh: continue
        path=c.static_mesh.get_path_name()
        if path.startswith(('/Game/FromBlender2/','/Game/FromBlender3/')) and a.get_actor_label().startswith(('SM_Gum','SM_Teeth','SM_Plane')):
            scenery.append(a)
    scenery += [a for a in source if isinstance(a,u.SkeletalMeshActor) and a.get_actor_label()=='SM_Exit' and a.skeletal_mesh_component.skeletal_mesh_asset]
    scenery += [a for a in source if isinstance(a,u.MCThroat)]
    look=[a for a in source if isinstance(a,(u.Light,u.SkyLight,u.PostProcessVolume,u.SkyAtmosphere,u.ExponentialHeightFog))]
    lab=u.load_asset(LAB)
    assert lab and lab != editor.get_editor_world()
    copied=actors.duplicate_actors(scenery+look,lab,u.Vector())
    assert len(copied)==len(scenery+look), (len(copied),len(scenery+look))
    for a, original in zip(copied,scenery+look):
        a.set_actor_label(PREFIX+original.get_actor_label())
        a.set_folder_path('VFXLab/ArenaReference')
        a.tags=list(a.tags)+[u.Name('MCVFXLabArenaReference')]
        # Far station replicas use the same authored components, culled independently.
        for c in a.get_components_by_class(u.PrimitiveComponent):
            c.set_editor_property('ld_max_draw_distance',12000.0)
    assert lib.save_loaded_asset(lab,only_if_is_dirty=False)
    report=dict(source=ARENA,destination=LAB,scenery=len(scenery),look=len(look),tongue_copied=False,
                originals_saved=False,actors=[a.get_actor_label() for a in copied])
    (Path(u.Paths.project_saved_dir())/'Codex'/'lab_arena_look.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    return report

# Release every Python actor/world reference before switching maps. A loaded
# inactive destination world held across LoadLevel triggers editor GC assertions.
report=copy_saved_reference()
assert levels.load_level(LAB)
print('LAB_ARENA_LOOK '+json.dumps(report))
