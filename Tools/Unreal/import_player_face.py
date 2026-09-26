"""Import the repaired face as a derived mesh, preserving the colleague's skeleton and Physics Asset."""
from pathlib import Path
import unreal as u
root=Path(u.Paths.project_dir()).resolve();lib=u.EditorAssetLibrary
u.SystemLibrary.execute_console_command(None,'Interchange.FeatureFlags.Import.FBX 0')
original=lib.load_asset('/Game/Art/Meshes/Character/SM_Teeth_rig')
task=u.AssetImportTask();task.filename=str(root/'ArtSource/CharacterGameplay/SK_TeethGameplay.fbx')
task.destination_path='/Game/Gameplay/Character';task.destination_name='SK_TeethGameplay'
task.automated=True;task.replace_existing=True;task.save=True;task.factory=u.FbxFactory()
opt=u.FbxImportUI();opt.import_mesh=True;opt.import_as_skeletal=True
opt.mesh_type_to_import=u.FBXImportType.FBXIT_SKELETAL_MESH
opt.import_materials=False;opt.import_textures=False;opt.import_animations=False;opt.create_physics_asset=False
opt.skeleton=original.get_editor_property('skeleton')
opt.skeletal_mesh_import_data.set_editor_property('update_skeleton_reference_pose',False)
opt.skeletal_mesh_import_data.set_editor_property('use_t0_as_ref_pose',False)
opt.skeletal_mesh_import_data.set_editor_property('normal_import_method',u.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
task.options=opt;u.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
mesh=lib.load_asset('/Game/Gameplay/Character/SK_TeethGameplay');assert isinstance(mesh,u.SkeletalMesh)
assert mesh.get_editor_property('skeleton')==opt.skeleton
# Ref-pose socket transforms must match before changing the gameplay appearance.
world=u.get_editor_subsystem(u.EditorActorSubsystem);actors=[];components=[]
try:
    for asset in [original,mesh]:
        actor=world.spawn_actor_from_class(u.SkeletalMeshActor,u.Vector(0,0,-5000));actors.append(actor)
        comp=actor.get_component_by_class(u.SkeletalMeshComponent);comp.set_skeletal_mesh_asset(asset);components.append(comp)
    names=list(components[0].get_all_socket_names())
    for name in names:
        assert components[1].does_socket_exist(name),str(name)
        a=components[0].get_socket_transform(name,u.RelativeTransformSpace.RTS_COMPONENT)
        b=components[1].get_socket_transform(name,u.RelativeTransformSpace.RTS_COMPONENT)
        assert a.translation.distance(b.translation)<.05, 'Ref position differs: '+str(name)+' '+str(a)+' '+str(b)
        assert a.scale3d.distance(b.scale3d)<.002,'Ref scale differs: '+str(name)
        assert abs(sum(getattr(a.rotation,k)*getattr(b.rotation,k) for k in ['x','y','z','w']))>.99999,'Ref rotation differs: '+str(name)
finally:
    for actor in actors:world.destroy_actor(actor)
appearance=lib.load_asset('/Game/Data/DA_PlayerAppearance')
mesh.set_editor_property('physics_asset',appearance.get_editor_property('physics_asset'))
mesh.set_editor_property('materials',original.get_editor_property('materials'))
assert lib.save_loaded_asset(mesh,only_if_is_dirty=False)
appearance.set_editor_property('skeletal_mesh',mesh)
appearance.set_editor_property('upper_lid_degrees',48)
appearance.set_editor_property('lower_lid_degrees',-28)
mapping=dict(appearance.get_editor_property('bone_map'))
for key in list(mapping):
    if str(key) in ['eye_pivot_l','eye_pivot_r']: del mapping[key]
appearance.set_editor_property('bone_map',mapping)
assert lib.save_loaded_asset(appearance,only_if_is_dirty=False)
u.log('MC_FACE_IMPORT_PASS bones='+str(len(names)))
u.SystemLibrary.quit_editor()
