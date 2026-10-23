"""Replace the active main menu cleaner; preserve the current scene lighting."""
import unreal as u,json,shutil
from pathlib import Path

ROOT=Path(u.Paths.project_dir()).resolve();OUT=ROOT/'Artifacts/CharacterMenu20261023';OUT.mkdir(parents=True,exist_ok=True)
levels=u.get_editor_subsystem(u.LevelEditorSubsystem);es=u.get_editor_subsystem(u.EditorActorSubsystem)
world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
assert not levels.is_in_play_in_editor() and world.get_path_name()=='/Game/Maps/L_MainMenu.L_MainMenu'
assert not (OUT/'Integration.json').exists(),'Already integrated; do not rescale keys twice'
seq=u.load_asset('/Game/UI/MenuShowcase/AnatomyV2/LS_MainMenuCleaningV2')
hero=next(a for a in es.get_all_level_actors() if 'MC_MenuV2_Hero' in [str(t) for t in a.tags])
brush=next(a for a in es.get_all_level_actors() if 'MC_MenuV2_Brush' in [str(t) for t in a.tags])
mesh=u.load_asset('/Game/UI/MenuShowcase/CharacterMenu20261023/SK_CharacterMenu');clip=u.load_asset('/Game/UI/MenuShowcase/CharacterMenu20261023/A_CharacterMenu_Cleaning_Loop')
assert mesh and clip and seq.get_playback_end()==288
u.LevelSequenceEditorBlueprintLibrary.close_level_sequence()
backup=OUT/'Before';backup.mkdir(exist_ok=True)
for relative in ['Content/Maps/L_MainMenu.umap','Content/UI/MenuShowcase/AnatomyV2/LS_MainMenuCleaningV2.uasset']:
    dest=backup/'Disk'/relative;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(ROOT/relative,dest)
# Capture current artist edits to the target menu before changing the character.
assert levels.save_current_level()
dest=backup/'CurrentArtistMenu.umap';shutil.copy2(ROOT/'Content/Maps/L_MainMenu.umap',dest)
report={'before_hero':str(hero.get_actor_transform()),'before_mesh':hero.skeletal_mesh_component.skeletal_mesh_asset.get_path_name(),'old_brush':brush.get_path_name(),'retimed_sections':[]}
component=hero.skeletal_mesh_component;component.set_skeletal_mesh_asset(mesh)
for i,slot in enumerate(mesh.materials):component.set_material(i,slot.material_interface)
component.set_animation_mode(u.AnimationMode.ANIMATION_SINGLE_NODE);component.set_animation(clip)
data=component.get_editor_property('animation_data');data.anim_to_play=clip;data.saved_looping=True;data.saved_playing=True;data.saved_position=0.;data.saved_play_rate=1.;component.set_editor_property('animation_data',data)
component.set_update_animation_in_editor(True);component.clear_morph_targets()
hero.set_actor_location(u.Vector(-650.,250.,3.525917),False,True)
hero.set_actor_rotation(u.Rotator(yaw=120.),False)
contacts={'MENU V2 | Sole contact L':(-659.324832,272.569466,4.777722),'MENU V2 | Sole contact R':(-651.585333,230.467369,2.574113),'MENU V2 | Soft sole contact':(-655.,251.,3.7)}
for a in es.get_all_level_actors():
    if a.get_actor_label() in contacts:a.set_actor_location(u.Vector(*contacts[a.get_actor_label()]),False,True)
brush.set_actor_hidden_in_game(True);brush.set_is_temporarily_hidden_in_editor(True);brush.static_mesh_component.set_visibility(False,True)
for binding in list(seq.get_bindings()):
    if binding.get_name()=='MENU V2 | Brush':binding.remove();continue
    for track in binding.get_tracks():
        for section in track.get_sections():
            old_range=[section.get_start_frame(),section.get_end_frame()]
            if isinstance(section,u.MovieSceneSkeletalAnimationSection):
                params=section.params;params.animation=clip;section.params=params
            else:
                for channel in section.get_all_channels():
                    for key in channel.get_keys():
                        ft=key.get_time();v=(ft.frame_number.value+ft.sub_frame)*219./288.
                        key.set_time(u.FrameNumber(round(v)))
            section.set_range(round(old_range[0]*219/288),round(old_range[1]*219/288))
            report['retimed_sections'].append({'binding':binding.get_name(),'class':section.get_class().get_name(),'old':old_range,'new':[section.get_start_frame(),section.get_end_frame()]})
for track in seq.get_tracks():
    for section in track.get_sections():section.set_range(0,219)
seq.set_playback_end(219)
actor=next(a for a in es.get_all_level_actors() if isinstance(a,u.LevelSequenceActor))
settings=actor.playback_settings;settings.auto_play=True;settings.loop_count=u.MovieSceneSequenceLoopCount(-1);actor.set_editor_property('playback_settings',settings)
assert u.EditorAssetLibrary.save_loaded_asset(seq);assert levels.save_current_level()
report.update(mesh=mesh.get_path_name(),clip=clip.get_path_name(),duration=clip.get_play_length(),hero_transform=str(hero.get_actor_transform()),sequence=seq.get_path_name(),infinite_loop=actor.playback_settings.loop_count.value==-1,preload='Mesh, animation and three materials have reflected references on the saved menu component; the active LevelSequence also references the animation.')
(OUT/'Integration.json').write_text(json.dumps(report,indent=2),encoding='utf8')
print('CHARACTER_MENU_INTEGRATED',json.dumps(report))
print('PREVIEW_API',[n for n in dir(u.MCGameplayUIEditorLibrary) if any(s in n for s in ['pie','play','capture','preview'])])
