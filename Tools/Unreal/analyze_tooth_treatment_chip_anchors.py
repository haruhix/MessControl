import unreal as u,json
from pathlib import Path
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
p=u.load_asset('/Game/Gameplay/Arena/Damage/DA_ToothDamage');out=[]
for row in p.get_editor_property('teeth'):
 intact=row.get_editor_property('intact');b=intact.get_bounding_box();length=max(b.max.x-b.min.x,b.max.y-b.min.y,b.max.z-b.min.z);result={'intact':intact.get_path_name(),'scale':length/100.,'stages':{}}
 for key in ('chipped','broken'):
  mesh=row.get_editor_property(key);names=[str(x.material_slot_name) for x in mesh.get_editor_property('static_materials')];assert 'MC_Damage_Dentin' in names,(mesh.get_path_name(),names);material=names.index('MC_Damage_Dentin')
  dm=u.new_object(u.DynamicMesh);dm,status=u.GeometryScript_AssetUtils.copy_mesh_from_static_mesh(mesh,dm,u.GeometryScriptCopyMeshFromAssetOptions(),u.GeometryScriptMeshReadLOD());assert status==u.GeometryScriptOutcomePins.SUCCESS,mesh.get_path_name()
  ids=u.GeometryScript_List.convert_index_list_to_array(u.GeometryScript_MeshQueries.get_all_triangle_i_ds(dm)[1]);acc=u.Vector();area=0;tri=0
  for i in ids:
   mat,valid=u.GeometryScript_Materials.get_triangle_material_id(dm,i)
   if not valid or mat!=material:continue
   good,a,b,c=u.GeometryScript_MeshQueries.get_triangle_positions(dm,i)
   if not good:continue
   ar=u.MathLibrary.cross_vector_vector(b-a,c-a).length()*.5
   acc+=(a+b+c)*(ar/3);area+=ar;tri+=1
  assert area>0 and tri>0,mesh.get_path_name()
  center=acc/area;center.z+=length*.045
  result['stages'][key]={'mesh':mesh.get_path_name(),'point':list(center.to_tuple()),'area':area,'triangles':tri}
  assert mesh.find_socket('MC_TreatmentChip') is None,mesh.get_path_name()
 assert intact.find_socket('MC_TreatmentChip') is None,intact.get_path_name()
 out.append(result)
path=(Path(u.Paths.project_dir())/'Tools/Unreal/ToothTreatmentFoam/ChipAnchors.json');path.write_text(json.dumps(out,indent=2));print('CHIP_ANCHOR_ANALYSIS',len(out),'rows',sum(v['triangles'] for r in out for v in r['stages'].values()))

