import unreal as u,sys,json,shutil
from pathlib import Path
sys.path.insert(0,'D:/PROJECT/GAME/WaifuTD/MessControl/Tools/Unreal');import author_menu_brush_foam_v2 as f
asset='/Game/Gameplay/Care/VFX/ToothTreatmentFoam/NS_ToothFoamSpray';L=u.EditorAssetLibrary
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor();assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages()
s=u.load_asset(asset);assert s
out=Path('E:/CodexWork/MessControl/Validation/FoamSprayTaper');out.mkdir(parents=True,exist_ok=True);src=Path(u.Paths.project_content_dir())/'Gameplay/Care/VFX/ToothTreatmentFoam/NS_ToothFoamSpray.uasset';backup=out/'NS_ToothFoamSpray_before.uasset'
assert not backup.exists();shutil.copy2(src,backup)
curves={'foam':[(0,.32),(.18,.58),(.55,1.25),(1,1.95)],'bubbles':[(0,.38),(.2,.65),(.55,1.15),(1,1.65)]}
rates={'foam':180.,'bubbles':26.};snap={}
for e in f._emitters(s):
 role,renderers=f._role(e);mods=f._modules(e);scale=mods['ScaleSpriteSize'];keys=','.join('(InterpMode=RCIM_Linear,Time=%s,Value=%s)'%(a,b) for a,b in curves[role]);f._dist(scale,'ScaleDistribution','(Mode=UniformCurve,ValuesTimeRange=(X=0,Y=1),ChannelCurves=((Keys=(%s))))'%keys)
 velocity=mods['AddVelocity'];velocity.set_editor_property('ConeAngle',10.)
 spawns=[x.copy() for x in e.get_editor_property('SpawnInfos')]
 for x in spawns:f._float_range(x,'Rate',rates[role],rates[role])
 e.set_editor_property('SpawnInfos',spawns);e.set_editor_property('Modules',list(mods.values()));e.set_editor_property('RendererProperties',renderers)
 snap[role]={'scale':scale.get_editor_property('ScaleDistribution').export_text(),'rate':rates[role],'cone_angle':10.}
assert L.save_loaded_asset(s,False)
result=u.EditorLoadingAndSavingUtils.reload_packages([s.get_outer()],interaction_mode=u.ReloadPackagesInteractionMode.ASSUME_POSITIVE);assert result[0],str(result)
s=u.load_asset(asset);after={}
for e in f._emitters(s):
 role,_=f._role(e);m=f._modules(e);after[role]=m['ScaleSpriteSize'].get_editor_property('ScaleDistribution').export_text();assert snap[role]['scale']==after[role],role
(out/'Authoring.json').write_text(json.dumps({'before':str(backup),'curves':curves,'rates':rates,'snapshots':snap,'reload_verified':True},indent=2));print('TAPER_SAVED_RELOADED',json.dumps({'curves':curves,'rates':rates},ensure_ascii=False))
