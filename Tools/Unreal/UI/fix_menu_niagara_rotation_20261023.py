"""Keep the menu foam orientation fixed while its origin follows the brush."""
import unreal as u,json,re,shutil
from pathlib import Path
ROOT=Path(u.Paths.project_dir()).resolve();OUT=ROOT/'Saved/MenuNiagaraBubbles20261023'
ls=u.get_editor_subsystem(u.LevelEditorSubsystem);es=u.get_editor_subsystem(u.EditorActorSubsystem)
assert not ls.is_in_play_in_editor()
assert u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world().get_path_name()=='/Game/Maps/L_MainMenu.L_MainMenu'
aa=list(es.get_all_level_actors());actor=next(a for a in aa if 'MC_MenuNiagaraBrushFoam' in [str(t) for t in a.tags]);c=actor.get_component_by_class(u.NiagaraComponent)
before=OUT/'BeforeRotationFix';before.mkdir(exist_ok=True)
for name in ['Integration.json','Validation.json']:
    if (OUT/name).exists() and not (before/name).exists():shutil.copy2(OUT/name,before/name)
if not (before/'L_MainMenu.umap').exists():shutil.copy2(ROOT/'Content/Maps/L_MainMenu.umap',before/'L_MainMenu.umap')
r=json.loads((OUT/'Integration.json').read_text())
snapshot={'actors_before':{a.get_path_name():str(a.get_actor_transform()) for a in aa if a!=actor},'lighting':{},'postprocess':{},'camera':r['camera'],'fov':next(a for a in aa if a.get_path_name()==r['camera']).camera_component.field_of_view}
for a in aa:
    if isinstance(a,u.Light):snapshot['lighting'][a.get_path_name()]=u.ToolsetLibrary.get_object_properties(a.light_component,['Intensity','LightColor','Mobility'])
    if isinstance(a,u.PostProcessVolume):snapshot['postprocess'][a.get_path_name()]=str(a.settings)
old_transform=actor.get_actor_transform();old_rotation=actor.get_actor_rotation()
old_relative=c.get_editor_property('relative_location');old_scale=c.get_editor_property('relative_scale3d')
with u.ScopedEditorTransaction('Fix menu Niagara world rotation'):
    c.modify();c.set_absolute(False,True,False);actor.set_actor_rotation(old_rotation,False)
assert c.get_editor_property('absolute_rotation') and not c.get_editor_property('absolute_location') and not c.get_editor_property('absolute_scale')
assert c.get_editor_property('relative_location')==old_relative and c.get_editor_property('relative_scale3d')==old_scale
assert (actor.get_actor_location()-old_transform.translation).length()<.001
assert ls.save_current_level()
for a in aa:
    if a!=actor:assert re.sub('0x[0-9A-Fa-f]+','PTR',str(a.get_actor_transform()))==re.sub('0x[0-9A-Fa-f]+','PTR',snapshot['actors_before'][a.get_path_name()])
r.update(rotation_fix=snapshot,relative_offset=list(old_relative.to_tuple()),relative_scale=list(old_scale.to_tuple()),world_scale=list(actor.get_actor_scale3d().to_tuple()),fixed_world_rotation_quat=list(actor.get_actor_transform().rotation.to_tuple()),absolute_rotation=True,rotation_behavior='World orientation fixed; position still follows the animated brush socket; artist offset and scale preserved.',validation='rotation_fix_pending')
(OUT/'Integration.json').write_text(json.dumps(r,indent=2),encoding='utf8')
print('MENU_NIAGARA_ROTATION_FIXED',json.dumps({k:r[k] for k in ['relative_offset','relative_scale','fixed_world_rotation_quat','absolute_rotation']}))
