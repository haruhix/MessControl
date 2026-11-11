import sys,json,unreal as u
sys.path.insert(0,'D:/PROJECT/GAME/WaifuTD/MessControl/Tools/Unreal')
import author_menu_brush_foam_v2 as f
L=u.EditorAssetLibrary
base='/Game/Gameplay/Care/VFX/ToothTreatmentFoam'
ns=base+'/NS_ToothTreatmentFoam'
comp=base+'/DA_ToothTreatmentFoamComposition'
prof=base+'/DA_ToothTreatmentFoamProfile'
orig='/Game/Gameplay/VFX/Bulk59/Profiles/DA_EditorOnly_RepairArenaTooth_core22_compiled61_01'
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages()
assert all(not L.does_asset_exist(x) for x in (ns,comp,prof))
source=u.load_asset(f.SOURCE);old=u.load_asset(orig)
system=L.duplicate_asset(f.SOURCE,ns);assert system
for e in f._emitters(system):
 role,renderers=f._role(e);mods=f._modules(e);init=mods['InitializeParticle'];vel=mods['AddVelocity'];shape=mods['ShapeLocation']
 params={'foam':(2.6,3.4,36.,55.,44.,44.,4.,10.,18.),'bubbles':(2.2,3.,14.,25.,42.,42.,8.,16.,5.)}[role]
 lo,hi,smin,smax,px,py,vmin,vmax,rate=params
 f._float_range(init,'LifetimeDistribution',lo,hi);f._sprite_range(init,smin,smax);f._position(init,0,0,5)
 f._vec2(shape,'PlaneSize',px,py);shape.set_editor_property('bPlaneEdgesOnly',False)
 f._float_range(vel,'ConeVelocityDistribution',vmin,vmax);vel.set_editor_property('ConeAngle',55.)
 spawns=[s.copy() for s in e.get_editor_property('SpawnInfos')]
 for i,s in enumerate(spawns):
  s.set_editor_property('bEnabled',i==0);s.set_editor_property('bLoopCountLimitEnabled',False);f._float_range(s,'Rate',rate,rate)
 e.set_editor_property('SpawnInfos',spawns);e.set_editor_property('Modules',list(mods.values()));e.set_editor_property('RendererProperties',renderers)
assert L.save_loaded_asset(system,False)
factory=u.DataAssetFactory();factory.set_editor_property('data_asset_class',u.MCVFXComposition)
composition=u.AssetToolsHelpers.get_asset_tools().create_asset(comp.rsplit('/',1)[1],base,u.MCVFXComposition,factory);assert composition
oldslot=dict(old.get_editor_property('slots'))[u.Name('Contact')];previous=oldslot.get_editor_property('system');assert previous
layers=[]
for name,asset in [('Original repair',previous),('Treatment foam',system)]:
 l=u.MCVFXCompositionLayer();l.set_editor_property('name',name);l.set_editor_property('system',asset);l.set_editor_property('binding',u.MCVFXCompositionBinding.COMPOSITION);layers.append(l)
composition.set_editor_property('layers',layers);composition.set_editor_property('duration_seconds',30.);composition.set_editor_property('tail_seconds',4.)
assert L.save_loaded_asset(composition,False)
profile=L.duplicate_asset(orig,prof);assert profile
slots=dict(profile.get_editor_property('slots'));slot=slots[u.Name('Contact')];slot.set_editor_property('mode',u.MCPerkVFXSlotMode.COMPOSITION);slot.set_editor_property('composition',composition);slots[u.Name('Contact')]=slot;profile.set_editor_property('slots',slots)
assert L.save_loaded_asset(profile,False)
print('FOAM_DRAFT '+json.dumps({'system':system.get_path_name(),'composition':composition.get_path_name(),'profile':profile.get_path_name(),'snapshot':f._snapshot(system)},ensure_ascii=False))
