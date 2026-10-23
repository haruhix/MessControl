"""Add three passive, independently looping menu characters; retain artist placements."""
import unreal as u,json,math,shutil,hashlib
from pathlib import Path
ROOT=Path(u.Paths.project_dir()).resolve();OUT=ROOT/'Saved/MenuAdditionalCharacters20261023';OUT.mkdir(parents=True,exist_ok=True)
DEST='/Game/UI/MenuShowcase/AdditionalCharacters20261023'
ls=u.get_editor_subsystem(u.LevelEditorSubsystem);es=u.get_editor_subsystem(u.EditorActorSubsystem)
world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
assert not ls.is_in_play_in_editor() and world and world.get_path_name()=='/Game/Maps/L_MainMenu.L_MainMenu'
assert not (OUT/'Integration.json').exists(),'Already added; use actor tags to refine placement'
assert not any('MC_MenuAdditionalCharacter' in [str(t) for t in a.tags] for a in es.get_all_level_actors())
backup=OUT/'Before';backup.mkdir(exist_ok=True)
shutil.copy2(ROOT/'Content/Maps/L_MainMenu.umap',backup/'L_MainMenu_Disk.umap')
assert ls.save_current_level();shutil.copy2(ROOT/'Content/Maps/L_MainMenu.umap',backup/'L_MainMenu_ArtistState.umap')
actors=list(es.get_all_level_actors());camera=next(a for a in actors if 'MC_MenuV2_Camera' in [str(t) for t in a.tags])
report={'retained_actors':{a.get_path_name():str(a.get_actor_transform()) for a in actors},'camera_before':str(camera.get_actor_transform()),'camera_fov':camera.camera_component.field_of_view,'added':[]}
mesh=u.load_asset('/Game/Gameplay/CharacterCurrent/SK_CharacterGameplay');skeleton=mesh.get_editor_property('skeleton')
love_skeleton=u.EditorAssetLibrary.duplicate_asset(skeleton.get_path_name(),DEST+'/SK_MenuLove_Skeleton')
love_mesh=u.EditorAssetLibrary.duplicate_asset(mesh.get_path_name(),DEST+'/SK_MenuLove');love_mesh.skeleton=love_skeleton
for n in ['Eyes_delight','Eyes_Blink']:
    u.AnimationLibrary.add_curve_meta_data(love_skeleton,n);u.AnimationLibrary.set_curve_meta_data_morph_target(love_skeleton,n,True)
# A real saved animation curve repeats in Editor and packaged menu without gameplay scripts.
factory=u.AnimSequenceFactory();factory.target_skeleton=love_skeleton;factory.preview_skeletal_mesh=love_mesh
face=u.AssetToolsHelpers.get_asset_tools().create_asset('A_Menu_Love_Face_Loop',DEST,u.AnimSequence,factory)
assert face
c=face.controller;c.open_bracket('Menu love: fixed body and authored delight eyes',False)
try:
    c.set_frame_rate(u.FrameRate(30,1),False);c.set_number_of_frames(u.FrameNumber(180),False)
    ref=love_skeleton.get_reference_pose()
    for name in ref.get_bone_names():
        t=ref.get_bone_pose(name,u.AnimPoseSpaces.LOCAL)
        assert c.add_bone_curve(name,False)
        assert c.set_bone_track_keys(name,[t.translation],[t.rotation],[t.scale3d],False)
finally:c.close_bracket(False)
times=[i/30 for i in range(181)];blink=[];delight=[]
for t in times:
    closure=max(max(0.,1.-abs(t-center)/.13) for center in [1.8,4.5])
    blink.append(closure);delight.append((.94+.06*math.cos(t*math.tau/6))*(1-closure))
for name,values in [('Eyes_delight',delight),('Eyes_Blink',blink)]:
    u.AnimationLibrary.add_curve(face,name,u.RawCurveTrackTypes.RCT_FLOAT,False)
    u.AnimationLibrary.add_float_curve_keys(face,name,times,values)
face.set_editor_property('enable_root_motion',False);assert u.EditorAssetLibrary.save_loaded_asset(face)
for asset in [love_mesh,love_skeleton]:assert u.EditorAssetLibrary.save_loaded_asset(asset)
floor=next(a for a in actors if a.get_actor_label()=='MENU | Tongue')
dyn=u.new_object(u.DynamicMesh);u.GeometryScript_AssetUtils.copy_mesh_from_static_mesh(floor.static_mesh_component.static_mesh,dyn,u.GeometryScriptCopyMeshFromAssetOptions(),u.GeometryScriptMeshReadLOD());u.GeometryScript_MeshTransforms.transform_mesh(dyn,floor.get_actor_transform());dyn,bvh=u.GeometryScript_MeshSpatial.build_bvh_for_mesh(dyn)
placements=[('Dance 1','MC_MenuDance1',-1200.,1090.,'/Game/Gameplay/Character/Animations/Teeth3/A_Teeth_Dance1',.2,1.35),('Dance 2','MC_MenuDance2',-1000.,1460.,'/Game/Gameplay/Character/Animations/Teeth3/A_Teeth_Dance2',.65,1.35),('Love','MC_MenuLove',-1500.,1500.,DEST+'/A_Menu_Love_Face_Loop',0.,1.25)]
with u.ScopedEditorTransaction('Add three animated menu characters'):
    for label,tag,x,y,clip_path,start,scale in placements:
        _,hit,_=u.GeometryScript_MeshSpatial.find_nearest_ray_intersection_with_mesh(dyn,bvh,u.Vector(x,y,3000),u.Vector(0,0,-1),u.GeometryScriptSpatialQueryOptions())
        assert hit.hit,(label,x,y)
        position=u.Vector(x,y,hit.hit_position.z+1.)
        yaw=u.MathLibrary.find_look_at_rotation(position,camera.get_actor_location()).yaw-90.
        actor=es.spawn_actor_from_class(u.SkeletalMeshActor,position,u.Rotator(yaw=yaw));actor.set_actor_label('MENU | '+label);actor.set_folder_path('Menu/Characters')
        actor.set_actor_scale3d(u.Vector(scale,scale,scale));actor.tags=['MC_MenuAdditionalCharacter',tag];actor.set_actor_enable_collision(False);actor.set_actor_tick_enabled(False)
        comp=actor.skeletal_mesh_component;comp.set_mobility(u.ComponentMobility.MOVABLE);actor_mesh=love_mesh if tag=='MC_MenuLove' else mesh;comp.set_skeletal_mesh_asset(actor_mesh)
        for i,slot in enumerate(actor_mesh.materials):comp.set_material(i,slot.material_interface)
        comp.set_collision_enabled(u.CollisionEnabled.NO_COLLISION);comp.set_editor_property('generate_overlap_events',False)
        comp.set_animation_mode(u.AnimationMode.ANIMATION_SINGLE_NODE);clip=u.load_asset(clip_path);assert clip and clip.get_editor_property('skeleton')==actor_mesh.get_editor_property('skeleton')
        data=comp.get_editor_property('animation_data');data.anim_to_play=clip;data.saved_looping=True;data.saved_playing=True;data.saved_position=start;data.saved_play_rate=1.;comp.set_editor_property('animation_data',data)
        comp.set_update_animation_in_editor(True);comp.set_editor_property('visibility_based_anim_tick_option',u.VisibilityBasedAnimTickOption.ALWAYS_TICK_POSE_AND_REFRESH_BONES)
        comp.set_animation(clip);comp.set_position(start,False)
        report['added'].append({'label':actor.get_actor_label(),'tag':tag,'actor':actor.get_path_name(),'mesh':actor_mesh.get_path_name(),'clip':clip.get_path_name(),'duration':clip.get_play_length(),'transform':str(actor.get_actor_transform()),'floor_z':hit.hit_position.z})
assert ls.save_current_level()
report['face_curves']=['Eyes_delight','Eyes_Blink'];report['preload']='Saved component references include the mesh, animation and materials; the normal map collector discovers all roots.'
(OUT/'Integration.json').write_text(json.dumps(report,indent=2),encoding='utf8')
u.AutomationLibrary.take_high_res_screenshot(1600,900,str(OUT/'Candidate.png'),camera=camera,force_game_view=True)
print('MENU_ADDITIONAL_CHARACTERS_SAVED',json.dumps(report['added']))

