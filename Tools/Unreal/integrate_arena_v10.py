"""Connect the October 17 arena; preserve artist placement and gum materials.

Run inside the existing stopped L_Mouth editor after the read-only geometry
snapshot and Tools/Blender/bake_arena_v10_gum_seams.py. New derived tooth meshes
retain UV0/positions and add gum-contact UV1 and signed distance in UV2.x.
"""
import unreal as u,json,hashlib
from pathlib import Path

root=Path(u.Paths.project_dir());out=root/'Artifacts/ArenaRefresh_20261017'
lib=u.EditorAssetLibrary;e=u.MaterialEditingLibrary
actors=u.get_editor_subsystem(u.EditorActorSubsystem)
levels=u.get_editor_subsystem(u.LevelEditorSubsystem)
meshes=u.get_editor_subsystem(u.StaticMeshEditorSubsystem)
assert not levels.is_in_play_in_editor(),'Preserve active PIE'
world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
assert world.get_path_name()=='/Game/Maps/L_Mouth.L_Mouth'
all_actors=actors.get_all_level_actors()
assert not any(isinstance(a,u.MCArenaToothSocket) for a in all_actors),'Inspect existing tooth markers before migrating'
assert not any(isinstance(a,u.MCTongue) for a in all_actors),'Inspect existing tongue before migrating'
folder='/Game/Gameplay/Arena/V10';lib.make_directory(folder)
before=json.loads((out/'BeforeLive.json').read_text(encoding='utf-8'))
bakes=json.loads((out/'GumSeamBake.json').read_text(encoding='utf-8'))
geometry=json.loads((out/'GeometryBefore.json').read_text(encoding='utf-8'))
by_path={a.get_path_name():a for a in all_actors}
assert len(bakes)==26
protected=[p for p in (root/'Content/FromBlender10').glob('*.uasset')]
protected += [root/p for p in ['Content/Art/Materials/Arena/MI_Gum_v2.uasset','Content/Data/DA_Tongue.uasset','Content/Gameplay/Arena/Pressure/MI_Pressure_Soft.uasset']]
baseline={str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in protected}
if (out/'ProtectedAssetHashes.json').exists():
 assert json.loads((out/'ProtectedAssetHashes.json').read_text())==baseline,'Protected assets changed since baseline'
else:(out/'ProtectedAssetHashes.json').write_text(json.dumps(baseline,indent=2),encoding='utf-8')

def save(o):assert lib.save_loaded_asset(o,only_if_is_dirty=False),o.get_path_name()
def duplicate(source,dest):
 if lib.does_asset_exist(dest) and dest==folder+'/M_ArenaToothCareV10':
  o=lib.load_asset(dest)
  original=lib.load_asset(source)
  names={n.get_name() for n in e.get_material_expressions(original)}
  current={n.get_name():n for n in e.get_material_expressions(o)}
  for prop in [u.MaterialProperty.MP_BASE_COLOR,u.MaterialProperty.MP_ROUGHNESS]:
   src=e.get_material_property_input_node(original,prop)
   assert e.connect_material_property(current[src.get_name()],e.get_material_property_input_node_output_name(original,prop) or '',prop)
  for n in list(e.get_material_expressions(o)):
   if n.get_name() not in names:e.delete_material_expression(o,n)
  return o
 assert not lib.does_asset_exist(dest),'Preserve existing destination '+dest
 o=lib.duplicate_asset(source,dest);assert o;return o
def node(mat,cls,**kw):
 n=e.create_material_expression(mat,cls)
 for k,v in kw.items():n.set_editor_property(k,v)
 return n
def custom(mat,label,code,inputs,kind):
 n=node(mat,u.MaterialExpressionCustom,description=label,code=code,output_type=kind)
 entries=[]
 for k in inputs:
  a=u.CustomInput();a.set_editor_property('input_name',k);entries.append(a)
 n.set_editor_property('inputs',entries)
 for k,(src,pin) in inputs.items():assert e.connect_material_expressions(src,pin,n,k)
 return n

report=dict(active=[],decorative=[],materials={},tongue={},gum_material_preserved=True)
with u.ScopedEditorTransaction('Connect new arena teeth, tongue and gum seams'):
 mat=duplicate('/Game/Gameplay/Arena/V2/M_ArenaToothCareV2',folder+'/M_ArenaToothCareV10')
 distance=node(mat,u.MaterialExpressionTextureCoordinate,coordinate_index=2)
 component=node(mat,u.MaterialExpressionComponentMask,r=True)
 assert e.connect_material_expressions(distance,'',component,'')
 width=node(mat,u.MaterialExpressionScalarParameter,parameter_name='Gum Seam Width',default_value=40.,group='Gum Seam')
 mask=custom(mat,'Arena V10: actual tooth to gum contact mask',
             'return 1.0-smoothstep(0.0,max(Width,1.0),D);',{'D':(component,''),'Width':(width,'')},u.CustomMaterialOutputType.CMOT_FLOAT1)
 gum_uv=node(mat,u.MaterialExpressionTextureCoordinate,coordinate_index=1)
 gum=u.load_asset('/Game/Art/Materials/Arena/MI_Gum_v2')
 gum_color=node(mat,u.MaterialExpressionTextureSampleParameter2D,parameter_name='Gum Contact Albedo',texture=e.get_material_instance_texture_parameter_value(gum,'Albedo Texture'),sampler_type=u.MaterialSamplerType.SAMPLERTYPE_COLOR)
 arm_texture=e.get_material_instance_texture_parameter_value(gum,'ARM')
 gum_arm=node(mat,u.MaterialExpressionTextureSampleParameter2D,parameter_name='Gum Contact ARM',texture=arm_texture,sampler_type=u.MaterialSamplerType.SAMPLERTYPE_COLOR if arm_texture.get_editor_property('srgb') else u.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
 for sample in [gum_color,gum_arm]:assert e.connect_material_expressions(gum_uv,'',sample,'')
 tint=node(mat,u.MaterialExpressionVectorParameter,parameter_name='Gum Contact Tint',default_value=e.get_material_instance_vector_parameter_value(gum,'Albedo Color'),group='Gum Seam')
 base=e.get_material_property_input_node(mat,u.MaterialProperty.MP_BASE_COLOR)
 base_pin=e.get_material_property_input_node_output_name(mat,u.MaterialProperty.MP_BASE_COLOR) or ''
 color=custom(mat,'Arena V10: blend enamel into unchanged gum palette',
              'return lerp(Base,Gum*Tint,saturate(Mask));',{'Base':(base,base_pin),'Gum':(gum_color,'RGB'),'Tint':(tint,'RGB'),'Mask':(mask,'')},u.CustomMaterialOutputType.CMOT_FLOAT3)
 assert e.connect_material_property(color,'',u.MaterialProperty.MP_BASE_COLOR)
 rough=e.get_material_property_input_node(mat,u.MaterialProperty.MP_ROUGHNESS)
 rough_pin=e.get_material_property_input_node_output_name(mat,u.MaterialProperty.MP_ROUGHNESS) or ''
 rough_blend=custom(mat,'Arena V10: continuous gum roughness',
                    'return lerp(Base,Gum,saturate(Mask));',{'Base':(rough,rough_pin),'Gum':(gum_arm,'G'),'Mask':(mask,'')},u.CustomMaterialOutputType.CMOT_FLOAT1)
 assert e.connect_material_property(rough_blend,'',u.MaterialProperty.MP_ROUGHNESS)
 # A narrow temporal blend removes the hard polygon intersection; the wider
 # color mask gives the root a continuous gum-colored collar.
 coverage=custom(mat,'Arena V10: soft contact coverage','return smoothstep(-4.0,12.0,D);',{'D':(component,'')},u.CustomMaterialOutputType.CMOT_FLOAT1)
 dither=node(mat,u.MaterialExpressionMaterialFunctionCall,desc='Arena V10: soft gum contact')
 dither.set_material_function(u.load_asset('/Engine/Functions/Engine_MaterialFunctions02/Utility/DitherTemporalAA'))
 assert e.connect_material_expressions(coverage,'',dither,'')
 original=e.get_material_property_input_node(mat,u.MaterialProperty.MP_OPACITY_MASK)
 if original:
  mul=node(mat,u.MaterialExpressionMultiply)
  assert e.connect_material_expressions(original,e.get_material_property_input_node_output_name(mat,u.MaterialProperty.MP_OPACITY_MASK) or '',mul,'A')
  assert e.connect_material_expressions(dither,'',mul,'B');dither=mul
 assert e.connect_material_property(dither,'',u.MaterialProperty.MP_OPACITY_MASK)
 mat.set_editor_property('blend_mode',u.BlendMode.BLEND_MASKED)
 errors=list(e.recompile_material(mat));assert not errors,errors
 save(mat);report['materials']['teeth']=mat.get_path_name()

 lower=[]
 for bake in bakes:
  a=by_path[bake['actor']];c=a.static_mesh_component;source=c.static_mesh
  assert source.get_path_name()==bake['mesh']
  mesh=duplicate(source.get_path_name(),folder+'/'+source.get_name())
  assert meshes.get_num_uv_channels(mesh,0)==1
  assert meshes.add_uv_channel(mesh,0);assert meshes.add_uv_channel(mesh,0)
  desc=mesh.get_static_mesh_description(0)
  samples={tuple(round(v,3) for v in r['local']):r for r in bake['samples']}
  for i in range(desc.get_vertex_instance_count()):
   vi=u.VertexInstanceID(i);p=desc.get_vertex_position(desc.get_vertex_instance_vertex(vi))
   sample=samples.get(tuple(round(v,3) for v in [p.x,p.y,p.z]))
   if sample is None:
    sample=min(bake['samples'],key=lambda r:sum((r['local'][j]-v)**2 for j,v in enumerate([p.x,p.y,p.z])))
    assert sum((sample['local'][j]-v)**2 for j,v in enumerate([p.x,p.y,p.z]))<.0001
   desc.set_vertex_instance_uv(vi,u.Vector2D(*sample['gum_uv']),1)
   desc.set_vertex_instance_uv(vi,u.Vector2D(sample['distance_cm'],0),2)
  mesh.build_from_static_mesh_descriptions([desc],False,False)
  settings=meshes.get_lod_build_settings(mesh,0);settings.set_editor_property('use_full_precision_u_vs',True)
  meshes.set_lod_build_settings(mesh,0,settings)
  mesh.set_editor_property('allow_cpu_access',True)
  body=mesh.get_editor_property('body_setup')
  body.set_editor_property('collision_trace_flag',u.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
  body.set_editor_property('double_sided_geometry',True)
  for slot in range(len(mesh.get_editor_property('static_materials'))):mesh.set_material(slot,mat)
  save(mesh)
  if not bake['upper'] and 'Forwart' not in bake['label']:
   lower.append((a,mesh,bake))
  else:
   c.set_static_mesh(mesh)
   for slot in range(c.get_num_materials()):c.set_material(slot,mat)
   c.set_collision_response_to_channel(u.CollisionChannel.ECC_CAMERA,u.CollisionResponseType.ECR_IGNORE)
   report['decorative'].append(dict(actor=a.get_path_name(),mesh=mesh.get_path_name(),upper=bake['upper']))
 assert len(lower)==12
 left=sorted([r for r in lower if r[0].get_actor_location().y<-170],key=lambda r:r[0].get_actor_location().x)
 right=sorted([r for r in lower if r[0].get_actor_location().y>-170],key=lambda r:r[0].get_actor_location().x)
 assert len(left)==len(right)==6
 for side,items in enumerate([left,right]):
  for depth,(source,mesh,bake) in enumerate(items):
   c=source.static_mesh_component;number=depth*2+side+1
   marker=actors.spawn_actor_from_class(u.MCArenaToothSocket,c.get_world_location(),c.get_world_rotation())
   marker.set_actor_scale3d(c.get_world_scale());marker.set_editor_property('tooth_id',number)
   marker.preview.set_static_mesh(mesh);marker.preview.set_material(0,mat)
   marker.set_actor_label('ARENA | Tooth %02d'%number);marker.set_folder_path('Gameplay/ArenaTeeth')
   marker.set_editor_property('tags',['ArtistArenaIntegrationV10'])
   report['active'].append(dict(id=number,source=source.get_path_name(),marker=marker.get_path_name(),mesh=mesh.get_path_name(),location=list(marker.get_actor_location().to_tuple())))
   actors.destroy_actor(source)

 profile=u.load_asset('/Game/Data/DA_ArenaTooth');profile.set_editor_property('gameplay_material',mat);save(profile)
 rules=u.load_asset('/Game/Data/DA_RunRules');settings=rules.get_editor_property('settings')
 settings.set_editor_property('initial_arena_teeth',12);rules.set_editor_property('settings',settings);save(rules)

 source=next(a for a in all_actors if a.get_path_name()==next(r['path'] for r in geometry if r['label']=='SM_Gum_Up'))
 c=source.static_mesh_component
 mesh=duplicate(c.static_mesh.get_path_name(),folder+'/SM_TongueSurface')
 mesh.set_editor_property('allow_cpu_access',True);mesh.set_material(0,c.get_material(0));save(mesh)
 tongue=actors.spawn_actor_from_class(u.MCTongue,c.get_world_location(),c.get_world_rotation())
 tongue.set_actor_scale3d(c.get_world_scale());tongue.set_actor_label('GAMEPLAY | Moving tongue');tongue.set_folder_path('Gameplay/Tongue')
 tongue.set_editor_property('source_mesh',mesh);tongue.set_editor_property('surface_material',c.get_material(0))
 tongue.set_editor_property('profile',u.load_asset('/Game/Data/DA_Tongue'))
 tongue.set_editor_property('tags',['ArtistArenaIntegrationV10']);tongue.call_method('RebuildSurface')
 tongue.surface.set_collision_response_to_channel(u.CollisionChannel.ECC_CAMERA,u.CollisionResponseType.ECR_IGNORE)
 report['tongue']=dict(actor=tongue.get_path_name(),source_mesh=mesh.get_path_name(),material=c.get_material(0).get_path_name(),replaced=source.get_path_name())
 actors.destroy_actor(source)
 assert levels.save_current_level()

report['active'].sort(key=lambda r:r['id'])
report['dirty_content_after']=[p.get_path_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
report['dirty_maps_after']=[p.get_path_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_map_packages()]
(out/'Integration.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print('ARENA_V10_INTEGRATED',len(report['active']),len(report['decorative']),report['tongue'])
