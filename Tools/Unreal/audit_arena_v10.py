"""Audit V10 assignments, save its compiled care material and preserve artist data."""
import unreal as u,json,hashlib,re
from pathlib import Path
root=Path(u.Paths.project_dir());out=root/'Artifacts/ArenaRefresh_20261017'
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
lib=u.EditorAssetLibrary;e=u.MaterialEditingLibrary
ss=u.get_editor_subsystem(u.StaticMeshEditorSubsystem)
actors=u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors()
integration=json.loads((out/'Integration.json').read_text())
geometry={r['mesh']:r for r in json.loads((out/'GeometryBefore.json').read_text())}
before=json.loads((out/'MaterialsBefore.json').read_text())
report={'checks':[],'meshes':[]}
def check(name,value):
 report['checks'].append(dict(name=name,passed=bool(value)))
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
baseline=json.loads((out/'ProtectedAssetHashes.json').read_text())
report['protected_asset_differences']=[name for name,value in baseline.items() if sha(root/name)!=value]
check('35 original artist and tongue assets remain byte-identical',len(baseline)==35 and not report['protected_asset_differences'])
mat=lib.load_asset('/Game/Gameplay/Arena/V10/M_ArenaToothCareV10')
report['material_compile_errors']=list(e.recompile_material(mat))
check('care and gum-contact material compiles',not report['material_compile_errors'])
if not report['material_compile_errors']:assert lib.save_loaded_asset(mat,only_if_is_dirty=True)
for row in integration['active']+integration['decorative']:
 mesh=lib.load_asset(row['mesh']);source=geometry['/Game/FromBlender10/'+mesh.get_name()+'.'+mesh.get_name()]
 section=u.ProceduralMeshLibrary.get_section_from_static_mesh(mesh,0,0)
 old={}
 for p,uv in zip(source['vertices'],source['uv']):old.setdefault(tuple(round(v,2) for v in p),[]).append(uv)
 errors=[]
 for p,uv in zip(section[0],section[3]):
  key=tuple(round(v,2) for v in [p.x,p.y,p.z])
  if key not in old:errors.append('position changed');break
  if min(max(abs(uv.x-v[0]),abs(uv.y-v[1])) for v in old[key])>.001:errors.append('UV0 changed');break
 desc=mesh.get_static_mesh_description(0)
 distances=[desc.get_vertex_instance_uv(u.VertexInstanceID(i),2).x for i in range(desc.get_vertex_instance_count())]
 report['meshes'].append(dict(mesh=mesh.get_path_name(),uv_channels=ss.get_num_uv_channels(mesh,0),geometry_errors=errors,
  triangles_before=len(source['triangles'])//3,triangles_after=len(section[1])//3,mask_range=[min(distances),max(distances)]))
check('all 26 meshes retain shape, UV0 and triangle count',all(not r['geometry_errors'] and r['triangles_before']==r['triangles_after'] for r in report['meshes']))
check('all 26 meshes retain baked contact UVs and signed seam masks',all(r['uv_channels']==3 and r['mask_range'][0]<0<r['mask_range'][1] for r in report['meshes']))
markers=sorted([a for a in actors if isinstance(a,u.MCArenaToothSocket)],key=lambda a:a.tooth_id)
check('saved map has twelve unique active sockets',len(markers)==12 and [a.tooth_id for a in markers]==list(range(1,13)))
check('saved run rules enable all twelve sockets',lib.load_asset('/Game/Data/DA_RunRules').get_editor_property('settings').get_editor_property('initial_arena_teeth')==12)
tongues=[a for a in actors if isinstance(a,u.MCTongue)]
check('saved map has one new tongue with the original surface material',len(tongues)==1 and tongues[0].get_editor_property('surface_material')==lib.load_asset('/Game/Gameplay/Arena/Pressure/MI_Pressure_Soft'))
gum=lib.load_asset('/Game/Art/Materials/Arena/MI_Gum_v2');old=before['/Game/Art/Materials/Arena/MI_Gum_v2']
differences=[]
if gum.parent.get_path_name()!=old['parent']:differences.append('parent')
for name,value in old['scalars'].items():
 if abs(e.get_material_instance_scalar_parameter_value(gum,name)-value)>1e-5:differences.append(name)
for name,value in old['textures'].items():
 texture=e.get_material_instance_texture_parameter_value(gum,name)
 if (texture.get_path_name() if texture else None)!=value:differences.append(name)
for name,value in old['vectors'].items():
 v=e.get_material_instance_vector_parameter_value(gum,name)
 original=[float(x) for x in re.findall(r'(?:r|g|b|a): ([\d.\-]+)',value)]
 if max(abs(a-b) for a,b in zip([v.r,v.g,v.b,v.a],original))>1e-5:differences.append(name)
report['live_gum_material_differences']=differences
check('live user gum material parameters remain unchanged',not differences)
report['dirty_content']=[p.get_path_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
report['dirty_maps']=[p.get_path_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_map_packages()]
check('unrelated unsaved gum material and textures are preserved',set(json.loads((out/'BeforeLive.json').read_text(encoding='utf-8'))['dirty_content']).issubset(set(report['dirty_content'])))
solo=json.loads((out/'SoloCheck.json').read_text())
check('bounded solo gameplay check passed',solo['passed'])
engine=next(o for o in u.ObjectIterator(u.load_class(None,'/Script/Engine.Engine')) if o.get_path_name().startswith('/Engine/Transient.'))
perf=u.get_default_object(u.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'))
report['engine_settings']=dict(fixed=engine.get_editor_property('bUseFixedFrameRate'),fps=engine.get_editor_property('FixedFrameRate'),throttle=perf.get_editor_property('bThrottleCPUWhenNotForeground'))
check('probe frame settings restored',report['engine_settings']==json.loads((out/'ProbeEngineBaseline.json').read_text()))
report['passed']=all(c['passed'] for c in report['checks'])
(out/'FinalAudit.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print('ARENA_V10_AUDIT',report['passed'],report['checks'])
assert report['passed']
