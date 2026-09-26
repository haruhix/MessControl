"""Import complete mouth morphs and compatible materials; preserve the original rig."""
from pathlib import Path
import json
import unreal as u
root=Path(u.Paths.project_dir()).resolve();lib=u.EditorAssetLibrary
edit=u.MaterialEditingLibrary;assets=u.AssetToolsHelpers.get_asset_tools()
u.SystemLibrary.execute_console_command(None,'Interchange.FeatureFlags.Import.FBX 0')
original=lib.load_asset('/Game/Art/Meshes/Character/SM_Teeth_rig')
appearance=lib.load_asset('/Game/Data/DA_PlayerAppearance')
task=u.AssetImportTask();task.filename=str(root/'ArtSource/CharacterFace/SK_TeethFace.fbx')
task.destination_path='/Game/Gameplay/Character';task.destination_name='SK_TeethFace'
task.automated=True;task.replace_existing=True;task.save=True;task.factory=u.FbxFactory()
opt=u.FbxImportUI();opt.import_mesh=True;opt.import_as_skeletal=True
opt.mesh_type_to_import=u.FBXImportType.FBXIT_SKELETAL_MESH
opt.import_materials=False;opt.import_textures=False;opt.import_animations=False;opt.create_physics_asset=False
opt.set_editor_property('reset_to_fbx_on_material_conflict',True)
opt.skeleton=original.get_editor_property('skeleton')
data=opt.skeletal_mesh_import_data
data.set_editor_property('import_morph_targets',True)
data.set_editor_property('update_skeleton_reference_pose',False)
data.set_editor_property('use_t0_as_ref_pose',False)
data.set_editor_property('normal_import_method',u.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
task.options=opt;assets.import_asset_tasks([task])
mesh=lib.load_asset('/Game/Gameplay/Character/SK_TeethFace');assert isinstance(mesh,u.SkeletalMesh)
assert mesh.get_editor_property('skeleton')==opt.skeleton
shape_report=json.loads((root/'ArtSource/CharacterFace/MouthShapes.json').read_text())
expected=(set(shape_report['shape_keys'])-{'Basis'}) | set(shape_report.get('pupils',{}).get('shape_keys',{}))
actual={str(m.get_name()) for m in mesh.get_editor_property('morph_targets')}
assert actual==expected, (actual,expected)

# Every material inherits the exact same local-space BodyStretch WPO as the enamel.
# Per-character dynamic instances receive the same value; the inner and outer surfaces stay together.
colors=[('M_MouthLip',(.48,.13,.17),.43),('M_MouthCavity',(.055,.009,.017),.66)]
materials=[appearance.get_editor_property('material')]
for name,color,roughness in colors:
    path='/Game/Gameplay/Character/Materials/'+name
    mat=lib.load_asset(path) if lib.does_asset_exist(path) else lib.duplicate_asset('/Game/Art/Materials/M_TeethGameplay',path)
    assert mat
    edit.set_base_material_usage(mat,u.MaterialUsage.MATUSAGE_SKELETAL_MESH)
    edit.set_base_material_usage(mat,u.MaterialUsage.MATUSAGE_MORPH_TARGETS)
    for prop,values in [(u.MaterialProperty.MP_BASE_COLOR,color),(u.MaterialProperty.MP_NORMAL,(0,0,1))]:
        node=edit.get_material_property_input_node(mat,prop)
        if not isinstance(node,u.MaterialExpressionConstant3Vector):
            node=edit.create_material_expression(mat,u.MaterialExpressionConstant3Vector,100,-300)
        node.set_editor_property('constant',u.LinearColor(*values,1))
        assert edit.connect_material_property(node,'',prop)
    for prop,value in [(u.MaterialProperty.MP_ROUGHNESS,roughness),(u.MaterialProperty.MP_METALLIC,0),
                       (u.MaterialProperty.MP_AMBIENT_OCCLUSION,1)]:
        node=edit.get_material_property_input_node(mat,prop)
        if not isinstance(node,u.MaterialExpressionConstant):
            node=edit.create_material_expression(mat,u.MaterialExpressionConstant,100,-100)
        node.set_editor_property('r',value);assert edit.connect_material_property(node,'',prop)
    errors=edit.recompile_material(mat);assert not errors,str(errors)
    assert lib.save_loaded_asset(mat,only_if_is_dirty=False)
    materials.append(mat)
enamel=lib.load_asset('/Game/Art/Materials/M_TeethGameplay')
edit.set_base_material_usage(enamel,u.MaterialUsage.MATUSAGE_MORPH_TARGETS)
edit.recompile_material(enamel);lib.save_loaded_asset(enamel,only_if_is_dirty=False)
slots=list(mesh.get_editor_property('materials'))
# Legacy FBX reimport retains removed material slots. The first three preserve
# their order in this derived asset; trim only the unused trailing prototype slot.
assert [str(s.material_slot_name) for s in slots[:3]]==['Material','M_MouthLip','M_MouthCavity']
slots=slots[:3]
# Match the imported slot names instead of relying on FBX polygon order.
for slot in slots:
    name=str(slot.get_editor_property('material_slot_name'))
    index=next((i+1 for i,(n,_,_) in enumerate(colors) if name==n),0)
    slot.set_editor_property('material_interface',materials[index])
mesh.set_editor_property('materials',slots)
mesh.set_editor_property('physics_asset',appearance.get_editor_property('physics_asset'))
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
        assert a.translation.distance(b.translation)<.05,str(name)
        assert a.scale3d.distance(b.scale3d)<.002,str(name)
        assert abs(sum(getattr(a.rotation,k)*getattr(b.rotation,k) for k in ['x','y','z','w']))>.99999,str(name)
finally:
    for actor in actors:world.destroy_actor(actor)
assert lib.save_loaded_asset(mesh,only_if_is_dirty=False)
appearance.set_editor_property('skeletal_mesh',mesh)
assert lib.save_loaded_asset(appearance,only_if_is_dirty=False)
u.log('MC_MOUTH_IMPORT_PASS morphs='+str(len(actual))+' bones='+str(len(names))+' slots='+str([str(s.material_slot_name) for s in slots]))
u.SystemLibrary.quit_editor()
