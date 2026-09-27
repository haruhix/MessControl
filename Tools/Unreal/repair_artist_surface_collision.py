"""Repair Tong_Height contact without replacing the artist's geometry or textures.

Pressure depth is authored in DA_Tongue (centimetres) and applied to the mesh.
The material must not displace those vertices again. Static walls collide against
their own triangles; the hidden shell from the previous composition is disabled.
"""
import unreal as u

lib = u.EditorAssetLibrary
edit = u.MaterialEditingLibrary
levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
actors = u.get_editor_subsystem(u.EditorActorSubsystem)

def save(asset):
    assert lib.save_loaded_asset(asset, only_if_is_dirty=False), asset.get_path_name()

mat = lib.load_asset('/Game/Gameplay/Arena/M_TonguePain')
world = edit.get_material_property_input_node(mat, u.MaterialProperty.MP_WORLD_POSITION_OFFSET)
if world:
    assert isinstance(world, u.MaterialExpressionTransform)
    assert str(world.get_editor_property('desc')) == 'MC weight WPO world'
    upstream = edit.get_inputs_for_material_expression(mat, world)[0]
    if isinstance(upstream, u.MaterialExpressionMultiply):
        inputs = edit.get_inputs_for_material_expression(mat, upstream)
        depth = next(n for n in inputs if isinstance(n, u.MaterialExpressionCustom))
        gain = next(n for n in inputs if isinstance(n, u.MaterialExpressionScalarParameter))
        assert str(gain.get_editor_property('parameter_name')) == 'Heiht'
        edit.delete_material_expression(mat, upstream)
        edit.delete_material_expression(mat, gain)
    else:
        depth = upstream
    assert isinstance(depth, u.MaterialExpressionCustom), 'Unexpected pressure graph'
    assert depth.get_editor_property('code') == 'return float3(0,0,-Weight.x);'
    uv = edit.get_inputs_for_material_expression(mat, depth)[0]
    assert isinstance(uv, u.MaterialExpressionTextureCoordinate) and uv.coordinate_index == 1
    for node in (world, depth, uv):
        edit.delete_material_expression(mat, node)
assert not edit.recompile_material(mat)
save(mat)
mi = lib.load_asset('/Game/Gameplay/Arena/MI_TonguePain')
values = mi.get_editor_property('scalar_parameter_values')
mi.set_editor_property('scalar_parameter_values', [v for v in values if str(v.parameter_info.name) != 'Heiht'])
edit.update_material_instance(mi)
save(mi)

assert levels.load_level('/Game/Maps/L_Mouth')
all_actors = actors.get_all_level_actors()
for name in ('SM_Gum', 'SM_Hole', 'SM_Wall_01'):
    mesh = lib.load_asset('/Game/Art/Meshes/Arena/' + name)
    assert mesh
    body = mesh.get_editor_property('body_setup')
    body.set_editor_property('collision_trace_flag', u.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
    body.set_editor_property('double_sided_geometry', True)
    save(mesh)
    instances = [a for a in all_actors if isinstance(a, u.StaticMeshActor) and a.static_mesh_component.static_mesh == mesh]
    assert instances, 'Mesh not placed: ' + name
    for actor in instances:
        actor.set_actor_enable_collision(True)
        actor.static_mesh_component.set_collision_profile_name('BlockAll')
        actor.static_mesh_component.set_collision_enabled(u.CollisionEnabled.QUERY_AND_PHYSICS)

old_shell = next(a for a in all_actors if a.get_actor_label() == 'COLLISION | Artist mouth')
assert old_shell.static_mesh_component.static_mesh.get_name() == 'SM_MouthShell'
old_shell.set_actor_enable_collision(False)
old_shell.static_mesh_component.set_collision_profile_name('NoCollision')
old_shell.static_mesh_component.set_collision_enabled(u.CollisionEnabled.NO_COLLISION)
assert levels.save_current_level()
u.log('MC_ARTIST_CONTACT_REPAIRED: shared physical depth; visible walls own collision; old shell disabled')
u.SystemLibrary.quit_editor()
