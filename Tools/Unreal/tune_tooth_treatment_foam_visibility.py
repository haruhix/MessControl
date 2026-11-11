import unreal as u,sys,shutil
from pathlib import Path
sys.path.insert(0,'D:/PROJECT/GAME/WaifuTD/MessControl/Tools/Unreal');import author_menu_brush_foam_v2 as f
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor() and not u.EditorLoadingAndSavingUtils.get_dirty_content_packages()
path='/Game/Gameplay/Care/VFX/ToothTreatmentFoam/NS_ToothTreatmentFoam';s=u.load_asset(path);backup=Path('E:/CodexWork/MessControl/Validation/ToothTreatmentFoam/BeforeGameplayFix/NS_ToothTreatmentFoam.uasset');backup.parent.mkdir(parents=True,exist_ok=True)
if not backup.exists():shutil.copy2(Path(u.Paths.project_content_dir())/'Gameplay/Care/VFX/ToothTreatmentFoam/NS_ToothTreatmentFoam.uasset',backup)
for e in f._emitters(s):
 role,renderers=f._role(e);mods=f._modules(e);sizes={'foam':(95.,145.),'bubbles':(24.,38.)}[role];rate={'foam':32.,'bubbles':7.}[role]
 f._sprite_range(mods['InitializeParticle'],*sizes);f._vec2(mods['ShapeLocation'],'PlaneSize',30.,30.)
 spawns=[x.copy() for x in e.get_editor_property('SpawnInfos')]
 for sp in spawns:f._float_range(sp,'Rate',rate,rate)
 e.set_editor_property('SpawnInfos',spawns);e.set_editor_property('Modules',list(mods.values()));e.set_editor_property('RendererProperties',renderers)
assert u.EditorAssetLibrary.save_loaded_asset(s,False);print('FOAM_VISIBILITY_TUNED')
