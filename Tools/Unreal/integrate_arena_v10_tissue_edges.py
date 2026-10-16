"""Replace the obsolete throat UV mask with each new tissue mesh's own border.

Creates four derived meshes, one material and one instance. Copies the live gum
palette/texture settings; original gum instance, textures and imports stay intact.
"""
import unreal as u,json,hashlib
from pathlib import Path
root=Path(u.Paths.project_dir());out=root/'Artifacts/ArenaEdges_20261017'
e=u.MaterialEditingLibrary;lib=u.EditorAssetLibrary
levels=u.get_editor_subsystem(u.LevelEditorSubsystem)
actors=u.get_editor_subsystem(u.EditorActorSubsystem)
ss=u.get_editor_subsystem(u.StaticMeshEditorSubsystem)
assert not levels.is_in_play_in_editor()
assert u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world().get_path_name()=='/Game/Maps/L_Mouth.L_Mouth'
source_instance=lib.load_asset('/Game/Art/Materials/Arena/MI_Gum_v2')
source_parent=source_instance.parent
bakes=json.loads((out/'EdgeBake.json').read_text())
lookup={a.get_path_name():a for a in actors.get_all_level_actors()}
assert len(bakes)==4
for row in bakes:assert lookup[row['actor']].static_mesh_component.static_mesh.get_path_name()==row['mesh']
folder='/Game/Gameplay/Arena/V10/Edges'
assert not lib.list_assets(folder,recursive=True,include_folder=False),'Preserve an existing edge integration'
lib.make_directory(folder)
protected=[]
for directory in ['Content/FromBlender10','Content/Gameplay/Arena/V10','Content/Art/Materials/ThroatEdgeFade']:
 protected.extend(p for p in (root/directory).rglob('*.uasset') if p.is_file())
protected.extend(root/p for p in ['Content/Art/Materials/Arena/MI_Gum_v2.uasset','Content/Art/Texture/Arena/gum_low_DefaultMaterial_BaseColor.uasset','Content/Art/Texture/Arena/gum_low_DefaultMaterial_Normal.uasset','Content/Art/Texture/Arena/gum_low_DefaultMaterial_OcclusionRoughnessMetallic.uasset','Content/Data/DA_ArenaTooth.uasset','Content/Data/DA_RunRules.uasset'])
baseline={str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in protected}
(out/'ProtectedHashes.json').write_text(json.dumps(baseline,indent=2))
def save(o):assert lib.save_loaded_asset(o,only_if_is_dirty=False),o.get_path_name()
def parameters(o):
 return dict(scalars={str(n):e.get_material_instance_scalar_parameter_value(o,n) for n in e.get_scalar_parameter_names(o)},vectors={str(n):list(e.get_material_instance_vector_parameter_value(o,n).to_tuple()) for n in e.get_vector_parameter_names(o)},textures={str(n):(e.get_material_instance_texture_parameter_value(o,n).get_path_name() if e.get_material_instance_texture_parameter_value(o,n) else None) for n in e.get_texture_parameter_names(o)})
report=dict(source_instance=source_instance.get_path_name(),source_parent=source_parent.get_path_name(),original_parameters=parameters(source_instance),actors=[])
with u.ScopedEditorTransaction('Soften new gum and side wall borders'):
 material=lib.duplicate_asset(source_parent.get_path_name(),folder+'/M_TissueV10_EdgeFade');assert material
 target=next(n for n in e.get_material_expressions(material) if isinstance(n,u.MaterialExpressionCustom) and n.get_editor_property('description')=='Smooth transparency at the outer perimeter')
 uv=e.create_material_expression(material,u.MaterialExpressionTextureCoordinate,1200,2800)
 uv.set_editor_property('coordinate_index',1);uv.set_editor_property('desc','V10: normalized geodesic distance to this tissue mesh border')
 mask=e.create_material_expression(material,u.MaterialExpressionComponentMask,1400,2800)
 mask.set_editor_property('r',True)
 assert e.connect_material_expressions(uv,'',mask,'')
 assert e.connect_material_expressions(mask,'',target,'Distance')
 errors=list(e.recompile_material(material));assert not errors,errors
 save(material)
 instance=lib.duplicate_asset(source_instance.get_path_name(),folder+'/MI_TissueV10_EdgeFade');assert instance
 e.set_material_instance_parent(instance,material);e.update_material_instance(instance);save(instance)
 report['new_parameters']=parameters(instance)
 assert report['new_parameters']==report['original_parameters'],'Preserve all live tissue tuning'
 report['material']=material.get_path_name();report['instance']=instance.get_path_name()
 for row in bakes:
  actor=lookup[row['actor']];c=actor.static_mesh_component;original=c.static_mesh
  mesh=lib.duplicate_asset(original.get_path_name(),folder+'/'+original.get_name());assert mesh
  assert ss.get_num_uv_channels(mesh,0)==1
  assert ss.add_uv_channel(mesh,0)
  desc=mesh.get_static_mesh_description(0)
  points={tuple(round(v,3) for v in s['local']):s for s in row['samples']}
  for i in range(desc.get_vertex_instance_count()):
   vi=u.VertexInstanceID(i);p=desc.get_vertex_position(desc.get_vertex_instance_vertex(vi));p=[p.x,p.y,p.z]
   sample=points.get(tuple(round(v,3) for v in p))
   if sample is None:
    sample=min(row['samples'],key=lambda s:sum((a-b)**2 for a,b in zip(s['local'],p)))
    assert sum((a-b)**2 for a,b in zip(sample['local'],p))<.0001
   desc.set_vertex_instance_uv(vi,u.Vector2D(sample['distance_cm']/200.,0),1)
  mesh.build_from_static_mesh_descriptions([desc],False,False)
  settings=ss.get_lod_build_settings(mesh,0);settings.set_editor_property('use_full_precision_u_vs',True);ss.set_lod_build_settings(mesh,0,settings)
  mesh.set_editor_property('allow_cpu_access',True)
  for slot in range(len(mesh.get_editor_property('static_materials'))):mesh.set_material(slot,instance)
  save(mesh)
  c.set_static_mesh(mesh)
  for slot in range(c.get_num_materials()):c.set_material(slot,instance)
  report['actors'].append(dict(actor=actor.get_path_name(),label=actor.get_actor_label(),source_mesh=original.get_path_name(),mesh=mesh.get_path_name(),boundary_edges=row['boundary_edges'],transform=str(c.get_world_transform())))
 assert levels.save_current_level()
report['dirty_content']=[p.get_path_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
report['dirty_maps']=[p.get_path_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_map_packages()]
(out/'Integration.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print('ARENA_TISSUE_EDGES_INTEGRATED',[r['label'] for r in report['actors']])
