"""Assemble the real native 3D menu scene; preserve the existing arena/menu."""
import json
from pathlib import Path
import unreal as u

BASE = '/Game/UI/MainMenu3D'
LEVEL = BASE+'/L_MainMenu3D'
OUT = Path(u.Paths.project_dir())/'ArtSource/UI/MainMenu3D/20261015_refB_v1/Assembly/UnrealHandoff/v04_SeatedSymmetricNative'
levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
actors = u.get_editor_subsystem(u.EditorActorSubsystem)
assert not levels.is_in_play_in_editor()
assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages(), 'Preserve unrelated unsaved maps'
assert u.load_asset(BASE+'/Meshes/SK_DAY03_Seated_v04')
if u.EditorAssetLibrary.does_asset_exist(LEVEL):
    assert levels.load_level(LEVEL)
else:
    assert levels.new_level(LEVEL)
world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
assert world.get_path_name().startswith(LEVEL)
assert not actors.get_all_level_actors() or not any(str(a.get_actor_label()).startswith('Menu3D_') for a in actors.get_all_level_actors()), 'Do not duplicate this assembly'
mode = u.load_class(None,BASE+'/BP_Menu3DGameMode.BP_Menu3DGameMode_C')
assert mode
world.get_world_settings().set_editor_property('default_game_mode',mode)

def tag(actor,label):
    actor.set_actor_label(label)
    actor.set_editor_property('tags',[u.Name(label)])
    actor.set_folder_path('MainMenu3D')
    return actor

camera = tag(actors.spawn_actor_from_class(u.CameraActor,u.Vector(0,4000,0),u.Rotator(pitch=0.,yaw=-90.,roll=0.)),'Menu3D_Camera')
cam = camera.get_component_by_class(u.CameraComponent)
cam.set_projection_mode(u.CameraProjectionMode.ORTHOGRAPHIC)
cam.set_ortho_width(1600.)
cam.set_editor_property('aspect_ratio',16./9.)
cam.set_editor_property('constrain_aspect_ratio',True)
cam.set_auto_calculate_ortho_planes(False)
cam.set_ortho_near_clip_plane(1.)
cam.set_ortho_far_clip_plane(20000.)
pp = u.PostProcessSettings()
for name,value in [('tone_curve_amount',0.),('expand_gamut',0.),('blue_correction',0.),
                   ('auto_exposure_bias',0.),('auto_exposure_apply_physical_camera_exposure',False),
                   ('bloom_intensity',0.),('vignette_intensity',0.),('motion_blur_amount',0.),
                   ('lens_flare_intensity',0.),('ambient_occlusion_intensity',0.)]:
    pp.set_editor_property('override_'+name,True)
    pp.set_editor_property(name,value)
pp.set_editor_property('override_auto_exposure_method',True)
pp.set_editor_property('auto_exposure_method',u.AutoExposureMethod.AEM_MANUAL)
cam.set_editor_property('post_process_settings',pp)
cam.set_editor_property('post_process_blend_weight',1.)

mouth = tag(actors.spawn_actor_from_class(u.SkeletalMeshActor,u.Vector(0,0,0)),'Menu3D_DAY03')
mesh = mouth.get_component_by_class(u.SkeletalMeshComponent)
mesh.set_skeletal_mesh_asset(u.load_asset(BASE+'/Meshes/SK_DAY03_Seated_v04'))
mesh.set_editor_property('cast_shadow',False)
mesh.set_collision_enabled(u.CollisionEnabled.NO_COLLISION)
interior = []
for name in ('Interior','Tongue'):
    actor = tag(actors.spawn_actor_from_class(u.StaticMeshActor,u.Vector(0,0,0)),'Menu3D_DAY03_'+name)
    component = actor.get_component_by_class(u.StaticMeshComponent)
    component.set_static_mesh(u.load_asset(BASE+'/Meshes/SM_DAY03_'+name+'_Seated_v04'))
    component.set_editor_property('mobility',u.ComponentMobility.MOVABLE)
    component.set_editor_property('cast_shadow',False)
    component.set_collision_enabled(u.CollisionEnabled.NO_COLLISION)
    actor.attach_to_actor(mouth,'',u.AttachmentRule.KEEP_WORLD,u.AttachmentRule.KEEP_WORLD,u.AttachmentRule.KEEP_WORLD,False)
    interior.append(actor)

# Existing game character + existing native authored greeting. No reimport/re-rig.
character = tag(actors.spawn_actor_from_class(u.SkeletalMeshActor,u.Vector(-575.7,-288.9,-217.78),u.Rotator(pitch=0.,yaw=195.,roll=0.)),'Menu3D_DAY03_Character')
char = character.get_component_by_class(u.SkeletalMeshComponent)
char.set_skeletal_mesh_asset(u.load_asset('/Game/Gameplay/CharacterCurrent/SK_CharacterGameplay'))
char.set_collision_enabled(u.CollisionEnabled.NO_COLLISION)
character.set_actor_scale3d(u.Vector(2.,2.,2.))
char.override_animation_data(u.load_asset('/Game/Gameplay/Character/Animations/Teeth3/A_Teeth_Hello1'),True,True,.75,1.)
character.attach_to_actor(mouth,'',u.AttachmentRule.KEEP_WORLD,u.AttachmentRule.KEEP_WORLD,u.AttachmentRule.KEEP_WORLD,False)

# Only the existing PBR character needs lights; mouth radiance is fixed-view baked lighting.
for label,loc,intensity,size in [('Key',(-600,1000,600),120.,800.),('Fill',(500,600,100),45.,600.)]:
    light = tag(actors.spawn_actor_from_class(u.RectLight,u.Vector(*loc)),'Menu3D_'+label)
    light.set_actor_rotation(u.MathLibrary.find_look_at_rotation(light.get_actor_location(),character.get_actor_location()),False)
    comp = light.get_component_by_class(u.RectLightComponent)
    comp.set_editor_property('intensity_units',u.LightUnits.CANDELAS)
    comp.set_editor_property('intensity',intensity)
    comp.set_editor_property('source_width',size)
    comp.set_editor_property('source_height',size)
    comp.set_editor_property('attenuation_radius',4000.)

# Physical foreground masks clip world meshes at the UI's header/footer boundaries.
assets = u.AssetToolsHelpers.get_asset_tools()
blackpath = BASE+'/Materials/M_Menu3D_Background'
black = u.load_asset(blackpath) if u.EditorAssetLibrary.does_asset_exist(blackpath) else assets.create_asset('M_Menu3D_Background',BASE+'/Materials',u.Material,u.MaterialFactoryNew())
black.set_editor_property('shading_model',u.MaterialShadingModel.MSM_UNLIT)
black.set_editor_property('two_sided',True)
u.EditorAssetLibrary.save_loaded_asset(black)
for label,center_z in [('Header',800.),('Footer',-800.)]:
    mask = tag(actors.spawn_actor_from_class(u.StaticMeshActor,u.Vector(0,1500,center_z)),'Menu3D_Mask_'+label)
    comp = mask.get_component_by_class(u.StaticMeshComponent)
    comp.set_static_mesh(u.load_asset('/Engine/BasicShapes/Cube'))
    comp.set_material(0,black)
    comp.set_collision_enabled(u.CollisionEnabled.NO_COLLISION)
    comp.set_editor_property('cast_shadow',False)
    mask.set_actor_scale3d(u.Vector(40.,.1,8.6))
levels.pilot_level_actor(camera)
assert levels.save_current_level()
report = {'level':LEVEL,'game_mode':mode.get_path_name(),'camera':camera.get_path_name(),
    'mouth':mouth.get_path_name(),'actual_morphs':[str(x) for x in mesh.get_skeletal_mesh_asset().get_all_morph_target_names()],
    'character':char.get_skeletal_mesh_asset().get_path_name(),'existing_animation':'A_Teeth_Hello1',
    'coordinate_policy':'FBX source XYZ cm, convertScene false. Native camera +Y looking -Y; actual render must verify handedness.',
    'not_final_art_approval':True}
(OUT/'native_level_assembly_report.json').write_text(json.dumps(report,indent=2),encoding='utf8')
u.log('MC_MENU3D_NATIVE_LEVEL '+json.dumps(report))
