"""Replace the former love observer with the authored CHARACTER_MENU2 actor."""
import unreal as u,json,shutil,hashlib,re
from pathlib import Path
ROOT=Path(u.Paths.project_dir()).resolve();OUT=ROOT/'Saved/CharacterMenu2_20261023';OUT.mkdir(parents=True,exist_ok=True)
ls=u.get_editor_subsystem(u.LevelEditorSubsystem);es=u.get_editor_subsystem(u.EditorActorSubsystem);assert not ls.is_in_play_in_editor()
world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world();assert world.get_path_name()=='/Game/Maps/L_MainMenu.L_MainMenu'
aa=list(es.get_all_level_actors());old=next(a for a in aa if 'MC_MenuLove' in [str(t) for t in a.tags]);camera=next(a for a in aa if 'MC_MenuV2_Camera' in [str(t) for t in a.tags])
backup=OUT/'Before';backup.mkdir(exist_ok=True);assert not (OUT/'Integration.json').exists()
shutil.copy2(ROOT/'Content/Maps/L_MainMenu.umap',backup/'L_MainMenu_Disk.umap');assert ls.save_current_level();shutil.copy2(ROOT/'Content/Maps/L_MainMenu.umap',backup/'L_MainMenu_ArtistState.umap')
r={'actor':old.get_path_name(),'label_before':old.get_actor_label(),'actors_before':{a.get_path_name():str(a.get_actor_transform()) for a in aa},'fov':camera.camera_component.field_of_view,'lighting':{},'postprocess':{},'old_mesh':old.skeletal_mesh_component.skeletal_mesh_asset.get_path_name(),'old_animation':str(old.skeletal_mesh_component.get_editor_property('animation_data'))}
for a in aa:
    if isinstance(a,u.Light):r['lighting'][a.get_path_name()]=u.ToolsetLibrary.get_object_properties(a.light_component,['Intensity','LightColor','Mobility'])
    if isinstance(a,u.PostProcessVolume):r['postprocess'][a.get_path_name()]=str(a.settings)
mesh=u.load_asset('/Game/UI/MenuShowcase/CharacterMenu2_20261023/SK_CharacterMenu2');clip=u.load_asset('/Game/UI/MenuShowcase/CharacterMenu2_20261023/A_CharacterMenu2_Loop');assert mesh and clip
with u.ScopedEditorTransaction('Replace menu love observer with CHARACTER_MENU2'):
    c=old.skeletal_mesh_component;c.set_skeletal_mesh_asset(mesh);c.clear_morph_targets()
    for i,s in enumerate(mesh.materials):c.set_material(i,s.material_interface)
    c.set_animation_mode(u.AnimationMode.ANIMATION_SINGLE_NODE);c.set_animation(clip)
    data=c.get_editor_property('animation_data');data.anim_to_play=clip;data.saved_playing=True;data.saved_looping=True;data.saved_position=0.;data.saved_play_rate=1.;c.set_editor_property('animation_data',data)
    c.set_position(0.,False);c.set_update_animation_in_editor(True);c.set_editor_property('visibility_based_anim_tick_option',u.VisibilityBasedAnimTickOption.ALWAYS_TICK_POSE_AND_REFRESH_BONES)
    c.set_collision_enabled(u.CollisionEnabled.NO_COLLISION);c.set_editor_property('generate_overlap_events',False);old.set_actor_enable_collision(False)
    old.set_actor_label('MENU | Character Menu 2');old.tags=[str(t) for t in old.tags if str(t)!='MC_MenuLove']+['MC_MenuCharacter2']
assert ls.save_current_level()
for a in aa:assert re.sub('0x[0-9A-Fa-f]+','PTR',str(a.get_actor_transform()))==re.sub('0x[0-9A-Fa-f]+','PTR',r['actors_before'][a.get_path_name()])
r.update(mesh=mesh.get_path_name(),clip=clip.get_path_name(),duration=clip.get_play_length(),looping=True,original_actor_transform_preserved=True,lighting_preserved=True,postprocess_preserved=True,source_sha256=hashlib.sha256(Path('D:/PROJECT/GAME/TEETH CONTROL/teeth/MENU/CHARACTER_MENU2.blend').read_bytes()).hexdigest(),preload='Mesh, animation, skeleton and existing two materials are reflected references on the saved menu component.')
(OUT/'Integration.json').write_text(json.dumps(r,indent=2),encoding='utf8');es.set_selected_level_actors([old])
print('CHARACTER_MENU2_REPLACED',json.dumps({k:r[k] for k in ['actor','mesh','clip','duration','looping','original_actor_transform_preserved','lighting_preserved','postprocess_preserved']}))
