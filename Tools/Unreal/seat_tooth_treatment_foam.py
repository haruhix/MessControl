import unreal as u,sys,json
from pathlib import Path
sys.path.insert(0,'D:/PROJECT/GAME/WaifuTD/MessControl/Tools/Unreal');import author_menu_brush_foam_v2 as f
L=u.EditorAssetLibrary;rows=json.loads((Path(u.Paths.project_dir())/'Tools/Unreal/ToothTreatmentFoam/ChipAnchors.json').read_text())
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor() and not u.EditorLoadingAndSavingUtils.get_dirty_content_packages()
for row in rows:
 for path,stage in [(row['intact'],'chipped'),(row['stages']['chipped']['mesh'],'chipped'),(row['stages']['broken']['mesh'],'broken')]:
  mesh=u.load_asset(path);s=mesh.find_socket('MC_TreatmentChip');assert s
  pos=u.Vector(*row['stages'][stage]['point']);assert (s.get_editor_property('relative_location')-pos).length()<.01
  pos.z-=row['scale']*4.5;s.set_editor_property('relative_location',pos);assert L.save_loaded_asset(mesh,False)
s=u.load_asset('/Game/Gameplay/Care/VFX/ToothTreatmentFoam/NS_ToothTreatmentFoam')
for e in f._emitters(s):
 role,renderers=f._role(e);mods=f._modules(e);lo,hi={'foam':(.3,1.5),'bubbles':(2.,5.)}[role];f._float_range(mods['AddVelocity'],'ConeVelocityDistribution',lo,hi);e.set_editor_property('Modules',list(mods.values()));e.set_editor_property('RendererProperties',renderers)
assert L.save_loaded_asset(s,False)
print('CHIP_FILL_SEATED',len(rows)*3,'meshes; reduced drift')
