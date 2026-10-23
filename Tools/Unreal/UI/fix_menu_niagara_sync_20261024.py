"""Attach foam to the fitted bristle contact, with final-pose Niagara updates."""
import unreal as u,json,shutil,re,math,hashlib
from pathlib import Path
ROOT=Path(u.Paths.project_dir()).resolve();OUT=ROOT/'Saved/MenuNiagaraBubbles20261023';BACK=OUT/'BeforeSyncFix20261024';BACK.mkdir(exist_ok=True)
ls=u.get_editor_subsystem(u.LevelEditorSubsystem);es=u.get_editor_subsystem(u.EditorActorSubsystem)
assert not ls.is_in_play_in_editor()
assert u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world().get_path_name()=='/Game/Maps/L_MainMenu.L_MainMenu'
aa=list(es.get_all_level_actors());foam=next(a for a in aa if 'MC_MenuNiagaraBrushFoam' in [str(t) for t in a.tags]);hero=next(a for a in aa if 'MC_MenuV2_Hero' in [str(t) for t in a.tags]);c=foam.get_component_by_class(u.NiagaraComponent);mesh=hero.skeletal_mesh_component
assert c.get_attach_parent()==mesh and str(c.get_attach_socket_name())=='MenuBrush_brush'
r=json.loads((OUT/'Integration.json').read_text())
for name in ['Integration.json','Validation.json']:
    if (OUT/name).exists() and not (BACK/name).exists():shutil.copy2(OUT/name,BACK/name)
if not (BACK/'L_MainMenu.umap').exists():shutil.copy2(ROOT/'Content/Maps/L_MainMenu.umap',BACK/'L_MainMenu.umap')
snapshot={'actors_before':{a.get_path_name():str(a.get_actor_transform()) for a in aa if a!=foam},'lighting':{},'postprocess':{},'camera':r['camera'],'fov':next(a for a in aa if a.get_path_name()==r['camera']).camera_component.field_of_view}
for a in aa:
    if isinstance(a,u.Light):snapshot['lighting'][a.get_path_name()]=u.ToolsetLibrary.get_object_properties(a.light_component,['Intensity','LightColor','Mobility'])
    if isinstance(a,u.PostProcessVolume):snapshot['postprocess'][a.get_path_name()]=str(a.settings)
fit=json.loads((ROOT/'ArtSource/UI/MenuDenseBrushFoam20261023/ExportReport.json').read_text());contact=u.Vector(*fit['center_in_bone_cm'])*.01+u.Vector(*fit['normal_in_bone'])*.05
old_offset=c.get_editor_property('relative_location');old_scale=c.get_editor_property('relative_scale3d');old_rotation=foam.get_actor_rotation()
gpu_default=u.ToolsetLibrary.get_object_properties(u.get_default_object(u.load_class(None,'/Script/Niagara.NiagaraSettings')),['DefaultGpuTranslucentLatency'])
assert json.loads(gpu_default)['DefaultGpuTranslucentLatency']=='Immediate'
with u.ScopedEditorTransaction('Synchronize menu foam with animated bristle contact'):
    c.modify();mesh.modify()
    foam.set_actor_relative_location(contact,False,True)
    c.set_absolute(False,True,False);foam.set_actor_rotation(old_rotation,False)
    # Sequencer / parallel skeletal evaluation must finish before Niagara reads its transform.
    c.set_tick_behavior(u.NiagaraTickBehavior.FORCE_TICK_LAST)
    mesh.set_editor_property('visibility_based_anim_tick_option',u.VisibilityBasedAnimTickOption.ALWAYS_TICK_POSE_AND_REFRESH_BONES)
assert c.get_editor_property('relative_scale3d')==old_scale
assert c.get_tick_behavior()==u.NiagaraTickBehavior.FORCE_TICK_LAST
assert ls.save_current_level()
for a in aa:
    if a!=foam:assert re.sub('0x[0-9A-Fa-f]+','PTR',str(a.get_actor_transform()))==re.sub('0x[0-9A-Fa-f]+','PTR',snapshot['actors_before'][a.get_path_name()])
effect_disk=ROOT/'Content'/(r['target'][6:]+'.uasset')
r.update(sync_fix=snapshot,relative_offset=list(contact.to_tuple()),relative_scale=list(old_scale.to_tuple()),world_scale=list(foam.get_actor_scale3d().to_tuple()),fixed_world_rotation_quat=list(foam.get_actor_transform().rotation.to_tuple()),absolute_rotation=True,validation='sync_fix_pending',sync_fix_details={'old_offset':list(old_offset.to_tuple()),'corrected_contact_offset':list(contact.to_tuple()),'old_contact_error_cm':(old_offset-contact).length()*100,'tick_behavior':'ForceTickLast','bone_update':'AlwaysTickPoseAndRefreshBones','gpu_translucent_default':'Immediate','niagara_asset_sha256':hashlib.sha256(effect_disk.read_bytes()).hexdigest(),'note':'Stateless emitters are local-space. Preserve particle settings, artist scale and fixed world rotation; correct the off-center attachment and evaluate Niagara after the final skeletal pose.'})
(OUT/'Integration.json').write_text(json.dumps(r,indent=2),encoding='utf8')
print('MENU_FOAM_SYNC_SAVED',json.dumps(r['sync_fix_details']))
