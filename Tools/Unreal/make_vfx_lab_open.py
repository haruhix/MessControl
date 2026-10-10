"""Remove arena scenery from the saved VFX lab; retain every gameplay fixture.

Run with L_VFXLab open and PIE stopped. Never loads/saves the main arena.
"""
import unreal as u,json
from pathlib import Path

LAB='/Game/Tests/L_VFXLab'

def main():
    editor=u.get_editor_subsystem(u.UnrealEditorSubsystem)
    actors=u.get_editor_subsystem(u.EditorActorSubsystem)
    levels=u.get_editor_subsystem(u.LevelEditorSubsystem)
    world=editor.get_editor_world()
    assert world.get_path_name().startswith(LAB+'.'),world.get_path_name()
    assert not levels.is_in_play_in_editor(),'Stop PIE before authoring'
    assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages(),'Save existing map edits first'
    before=actors.get_all_level_actors()
    stations=[a for a in before if a.actor_has_tag('MCVFXLabStation')]
    assert len(stations)==51
    station_paths=[a.get_path_name() for a in stations]
    removed=[]
    for a in before:
        label=a.get_actor_label()
        arena_geometry=(label.startswith('MCVFXLab_Arena_') or '_Arena_' in label) and isinstance(a,(u.StaticMeshActor,u.SkeletalMeshActor,u.MCThroat))
        atmosphere=isinstance(a,(u.SkyAtmosphere,u.ExponentialHeightFog))
        if arena_geometry or atmosphere:
            removed.append(label)
            assert actors.destroy_actor(a),label
    live=actors.get_all_level_actors()
    sun=next(a for a in live if isinstance(a,u.DirectionalLight))
    sun.set_actor_label('VFXLab_Open_KeyLight')
    sun.set_folder_path('VFXLab/Lighting')
    sun.set_actor_rotation(u.Rotator(pitch=-45,yaw=-35),False)
    sun.light_component.set_intensity(4.0)
    sun.light_component.set_light_color(u.LinearColor(.98,.98,1,1))
    sky=next(a for a in live if isinstance(a,u.SkyLight))
    sky.set_actor_label('VFXLab_Open_AmbientLight')
    sky.set_folder_path('VFXLab/Lighting')
    cube=u.load_asset('/Engine/MapTemplates/Sky/DaylightAmbientCubemap')
    assert cube
    sky.light_component.set_editor_property('source_type',u.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
    sky.light_component.set_editor_property('cubemap',cube)
    sky.light_component.set_intensity(.5)
    sky.light_component.set_editor_property('real_time_capture',False)
    post=next(a for a in live if isinstance(a,u.PostProcessVolume))
    post.set_actor_label('VFXLab_Open_Exposure')
    post.set_folder_path('VFXLab/Lighting')
    # Neutral fixed exposure keeps each burst comparable without eye adaptation.
    settings=u.PostProcessSettings()
    for key,value in dict(override_auto_exposure_min_brightness=True,auto_exposure_min_brightness=1.,
        override_auto_exposure_max_brightness=True,auto_exposure_max_brightness=1.,
        override_auto_exposure_bias=True,auto_exposure_bias=0.,
        override_bloom_intensity=True,bloom_intensity=.1,
        override_lens_flare_intensity=True,lens_flare_intensity=0.,
        override_vignette_intensity=True,vignette_intensity=0.,
        override_motion_blur_amount=True,motion_blur_amount=0.).items():
        settings.set_editor_property(key,value)
    post.set_editor_property('settings',settings)
    post.set_editor_property('unbound',True)
    remaining=actors.get_all_level_actors()
    assert sorted(a.get_path_name() for a in remaining if a.actor_has_tag('MCVFXLabStation'))==sorted(station_paths)
    assert not any(isinstance(a,(u.SkyAtmosphere,u.ExponentialHeightFog,u.MCThroat)) for a in remaining)
    assert not any((a.get_actor_label().startswith('MCVFXLab_Arena_') or '_Arena_' in a.get_actor_label()) and isinstance(a,(u.StaticMeshActor,u.SkeletalMeshActor)) for a in remaining)
    actors.set_selected_level_actors([])
    u.SystemLibrary.execute_console_command(world,'RebuildNavigation')
    assert levels.save_current_level()
    report=dict(map=LAB,layout='Open neutral platforms; no arena environment or visible tongue',removed_actors=len(removed),
        removed=removed,stations=51,static_supports=sum(a.get_class().get_path_name()=='/Script/MessControl.MCVFXLabFloor' for a in remaining),
        niagara_gallery_samples=sum(isinstance(a,u.NiagaraActor) and a.actor_has_tag('FabVFXLabSample') for a in remaining),
        key_intensity=4.,ambient_intensity=.5,exposure_min=1.,exposure_max=1.,exposure_bias=0.,bloom=.1,
        fog=False,sky_geometry=False,main_arena_saved=False)
    root=Path(u.Paths.project_dir()).resolve()
    (root/'Saved/Codex/vfx_lab_open_layout.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    print('VFX_LAB_OPEN '+json.dumps({k:v for k,v in report.items() if k!='removed'}))

main()
