import unreal as u,json,shutil,hashlib
from pathlib import Path
L=u.EditorAssetLibrary;root=Path(u.Paths.project_dir()).resolve();out=Path('E:/CodexWork/MessControl/Validation/ToothTreatmentFoam');rows=json.loads((root/'Tools/Unreal/ToothTreatmentFoam/ChipAnchors.json').read_text());backup=out/'BeforeChipAnchors';compath='/Game/Gameplay/Care/VFX/ToothTreatmentFoam/DA_ToothTreatmentFoamComposition';assets=[]
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages()
for row in rows:
 for path in [row['intact'],row['stages']['chipped']['mesh'],row['stages']['broken']['mesh']]:
  m=u.load_asset(path);assert m and m.find_socket('MC_TreatmentChip') is None,path;assets.append(path)
assert len(assets)==len(set(assets))==60
for path in assets+[compath]:
 stem=path.split('.')[0];src=root/('Content/'+stem.removeprefix('/Game/')+'.uasset');dest=backup/src.relative_to(root)
 assert src.exists() and not dest.exists(),str(dest)
 dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src,dest);assert hashlib.sha256(src.read_bytes()).digest()==hashlib.sha256(dest.read_bytes()).digest()
for row in rows:
 for path,stage in [(row['intact'],'chipped'),(row['stages']['chipped']['mesh'],'chipped'),(row['stages']['broken']['mesh'],'broken')]:
  m=u.load_asset(path);socket=u.new_object(u.StaticMeshSocket,outer=m);socket.set_editor_property('socket_name','MC_TreatmentChip');socket.set_editor_property('relative_location',u.Vector(*row['stages'][stage]['point']))
  sc=row['scale'];socket.set_editor_property('relative_scale',u.Vector(sc,sc,sc));m.add_socket(socket)
  assert m.find_socket('MC_TreatmentChip');assert L.save_loaded_asset(m,False),path
comp=u.load_asset(compath);layers=list(comp.get_editor_property('layers'));foam=next(x for x in layers if str(x.get_editor_property('name'))=='Treatment foam')
foam.set_editor_property('binding',u.MCVFXCompositionBinding.SUBJECT);foam.set_editor_property('socket','MC_TreatmentChip');comp.set_editor_property('layers',layers);assert L.save_loaded_asset(comp,False)
print('CHIP_ANCHORS_SAVED',len(assets),'meshes, composition follows MC_TreatmentChip')
