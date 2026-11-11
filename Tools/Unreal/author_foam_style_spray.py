import unreal as u,sys,json
sys.path.insert(0,'D:/PROJECT/GAME/WaifuTD/MessControl/Tools/Unreal');import author_menu_brush_foam_v2 as f
L=u.EditorAssetLibrary;folder='/Game/Gameplay/Care/VFX/ToothTreatmentFoam';ns=folder+'/NS_ToothFoamSpray';profile=folder+'/DA_ToothFoamSprayProfile';orig='/Game/Gameplay/VFX/P1Original67/Profiles/DA_Tool_Spray_p1_bind67_03'
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor();assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages();assert not L.does_asset_exist(ns) and not L.does_asset_exist(profile)
s=L.duplicate_asset(f.SOURCE,ns);assert s
for e in f._emitters(s):
 role,renderers=f._role(e);mods=f._modules(e);init=mods['InitializeParticle'];velocity=mods['AddVelocity'];shape=mods['ShapeLocation']
 lo,hi,smin,smax,vmin,vmax,rate,cone={'foam':(.36,.45,13.,23.,550.,650.,65.,12.),'bubbles':(.33,.43,7.,13.,560.,660.,12.,12.)}[role]
 f._float_range(init,'LifetimeDistribution',lo,hi);f._sprite_range(init,smin,smax);f._position(init,0,0,1.)
 f._vec2(shape,'PlaneSize',3.,3.);shape.set_editor_property('bPlaneEdgesOnly',False)
 assert u.ToolsetLibrary.set_object_properties(velocity,json.dumps({'coordinateSpace':'Local'}));f._float_range(velocity,'ConeVelocityDistribution',vmin,vmax);f._float_range(velocity,'ConeVelocityScale',1.,1.);f._vec3_range_constant(velocity,'ConeDirection',0,0,1);velocity.set_editor_property('ConeAngle',cone)
 if 'Drag' in mods:f._float_range(mods['Drag'],'DragDistribution',.1,.1)
 spawns=[x.copy() for x in e.get_editor_property('SpawnInfos')]
 for i,x in enumerate(spawns):x.set_editor_property('bEnabled',i==0);x.set_editor_property('bLoopCountLimitEnabled',False);f._float_range(x,'Rate',rate,rate)
 e.set_editor_property('SpawnInfos',spawns);e.set_editor_property('Modules',list(mods.values()));e.set_editor_property('RendererProperties',renderers)
 bounds=u.Box(min=u.Vector(-100,-100,-40),max=u.Vector(100,100,340));e.set_editor_property('FixedBounds',bounds)
s.set_editor_property('fixed_bounds',u.Box(min=u.Vector(-100,-100,-40),max=u.Vector(100,100,340)))
assert L.save_loaded_asset(s,False)
p=L.duplicate_asset(orig,profile);assert p;slots=dict(p.get_editor_property('slots'));start=slots[u.Name('Start')];start.set_editor_property('system',s);slots[u.Name('Start')]=start;p.set_editor_property('slots',slots);assert L.save_loaded_asset(p,False)
print('FOAM_SPRAY_AUTHORED',s.get_path_name(),p.get_path_name())
