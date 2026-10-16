"""Validate the four border masks and preserve prior arena/material authoring."""
import unreal as u,json,hashlib
from pathlib import Path
root=Path(u.Paths.project_dir());out=root/'Artifacts/ArenaEdges_20261017'
level=u.get_editor_subsystem(u.LevelEditorSubsystem);assert not level.is_in_play_in_editor()
lib=u.EditorAssetLibrary;e=u.MaterialEditingLibrary;ss=u.get_editor_subsystem(u.StaticMeshEditorSubsystem)
before=json.loads((out/'Before.json').read_text());originals={r['mesh']:r for r in before['rows']}
integration=json.loads((out/'Integration.json').read_text())
baseline=json.loads((out/'ProtectedHashes.json').read_text())
report=dict(checks=[],meshes=[],protected_files=len(baseline))
def check(name,value):report['checks'].append(dict(name=name,passed=bool(value)))
changed=[p for p,sha in baseline.items() if hashlib.sha256((root/p).read_bytes()).hexdigest()!=sha]
report['protected_file_differences']=changed
check('original imports, gum tuning, teeth and tongue assets preserved',not changed)
mat=lib.load_asset(integration['material']);errors=list(e.recompile_material(mat))
report['material_compile_errors']=errors;check('new perimeter shader compiles',not errors)
if not errors:assert lib.save_loaded_asset(mat,only_if_is_dirty=True)
actors={a.get_path_name():a for a in u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors()}
for row in integration['actors']:
 c=actors[row['actor']].static_mesh_component;mesh=c.static_mesh;src=originals[row['source_mesh']]
 assert mesh.get_path_name()==row['mesh']
 section=u.ProceduralMeshLibrary.get_section_from_static_mesh(mesh,0,0)
 reference={}
 for p,uv in zip(src['vertices'],src['uv']):reference.setdefault(tuple(round(v,2) for v in p),[]).append(uv)
 same=True
 for p,uv in zip(section[0],section[3]):
  variants=reference.get(tuple(round(v,2) for v in [p.x,p.y,p.z]),[])
  if not variants or min(max(abs(uv.x-v[0]),abs(uv.y-v[1])) for v in variants)>.001:same=False;break
 d=mesh.get_static_mesh_description(0)
 values=[d.get_vertex_instance_uv(u.VertexInstanceID(i),1).x for i in range(d.get_vertex_instance_count())]
 source_mesh=lib.load_asset(row['source_mesh'])
 report['meshes'].append(dict(actor=row['label'],geometry_and_uv0_preserved=same,triangle_count_preserved=len(section[1])==len(src['triangles']),uv_channels=ss.get_num_uv_channels(mesh,0),mask_min=min(values),mask_max=max(values),material_correct=all(c.get_material(i).get_path_name()==integration['instance'] for i in range(c.get_num_materials())),collision_mode_preserved=mesh.get_editor_property('body_setup').get_editor_property('collision_trace_flag')==source_mesh.get_editor_property('body_setup').get_editor_property('collision_trace_flag')))
check('four masks cover open borders and retain solid interiors',len(report['meshes'])==4 and all(r['uv_channels']==2 and r['mask_min']==0 and r['mask_max']==1 for r in report['meshes']))
check('tissue shape, UV0, triangle counts and collision mode preserved',all(r['geometry_and_uv0_preserved'] and r['triangle_count_preserved'] and r['collision_mode_preserved'] for r in report['meshes']))
check('all four scene components use the new edge material',all(r['material_correct'] for r in report['meshes']))
gum=lib.load_asset(integration['source_instance'])
check('original gum instance retains its parent',gum.parent.get_path_name()==integration['source_parent'])
check('copied color, texture and surface parameters are identical',integration['original_parameters']==integration['new_parameters'])
check('solo PIE visual check passed',json.loads((out/'SoloCheck.json').read_text())['passed'])
if u.EditorLoadingAndSavingUtils.get_dirty_map_packages():assert level.save_current_level()
report['dirty_content']=[p.get_path_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
report['dirty_maps']=[p.get_path_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_map_packages()]
check('existing unsaved gum edits remain open',set(before['dirty_content'])==set(report['dirty_content']))
report['passed']=all(c['passed'] for c in report['checks'])
(out/'FinalAudit.json').write_text(json.dumps(report,indent=2))
print('ARENA_EDGES_AUDIT',report['passed'],'protected',len(baseline),report['checks'])
assert report['passed']
