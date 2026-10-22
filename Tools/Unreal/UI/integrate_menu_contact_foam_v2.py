"""Import reviewed opaque foam geometry and animate its menu contact volume."""
import unreal as u
from pathlib import Path
ROOT = Path('E:/DEVGAME/MessControl')
BASE = '/Game/UI/MenuShowcase/AnatomyV2'
ls = u.get_editor_subsystem(u.LevelEditorSubsystem)
es = u.get_editor_subsystem(u.EditorActorSubsystem)
assert not ls.is_in_play_in_editor()
assert u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world().get_path_name().startswith(BASE + '/L_MenuAnatomyV2.')
lib, edit = u.EditorAssetLibrary, u.MaterialEditingLibrary
path = BASE + '/VFX/SM_MenuContactFoam'
mesh = lib.load_asset(path)
if not mesh:
    flag = 'Interchange.FeatureFlags.Import.FBX'
    previous = u.SystemLibrary.get_console_variable_int_value(flag)
    try:
        u.SystemLibrary.execute_console_command(None, flag + ' 0')
        task = u.AssetImportTask()
        task.filename = str(ROOT / 'ArtSource/UI/MenuSceneV2_20261022/Foam/SM_MenuContactFoam.fbx')
        task.destination_path = BASE + '/VFX'
        task.destination_name = 'SM_MenuContactFoam'
        task.automated = True
        task.save = False
        task.factory = u.FbxFactory()
        opt = u.FbxImportUI()
        opt.automated_import_should_detect_type = False
        opt.mesh_type_to_import = u.FBXImportType.FBXIT_STATIC_MESH
        opt.import_mesh = True
        opt.import_as_skeletal = False
        opt.import_materials = False
        opt.import_textures = False
        opt.static_mesh_import_data.combine_meshes = True
        opt.static_mesh_import_data.auto_generate_collision = False
        task.options = opt
        u.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    finally:
        u.SystemLibrary.execute_console_command(None, flag + ' ' + str(previous))
    mesh = lib.load_asset(path)
assert isinstance(mesh, u.StaticMesh)
extent = mesh.get_bounds().box_extent
assert abs(extent.x - 7.) < .01 and abs(extent.y - 4.5) < .01 and abs(extent.z - 4.) < .01, extent
matpath = BASE + '/Materials/M_MenuContactFoam'
material = lib.load_asset(matpath)
if not material:
    material = u.AssetToolsHelpers.get_asset_tools().create_asset('M_MenuContactFoam', BASE + '/Materials', u.Material, u.MaterialFactoryNew())
edit.delete_all_material_expressions(material)
material.set_editor_property('shading_model', u.MaterialShadingModel.MSM_SUBSURFACE)
def constant(value, prop):
    node = edit.create_material_expression(material, u.MaterialExpressionConstant)
    node.set_editor_property('r', value)
    assert edit.connect_material_property(node, '', prop)
color = edit.create_material_expression(material, u.MaterialExpressionConstant3Vector)
color.set_editor_property('constant', u.LinearColor(.86, .91, .94, 1))
assert edit.connect_material_property(color, '', u.MaterialProperty.MP_BASE_COLOR)
scatter = edit.create_material_expression(material, u.MaterialExpressionConstant3Vector)
scatter.set_editor_property('constant', u.LinearColor(.68, .79, .85, 1))
assert edit.connect_material_property(scatter, '', u.MaterialProperty.MP_SUBSURFACE_COLOR)
constant(.38, u.MaterialProperty.MP_ROUGHNESS)
constant(.48, u.MaterialProperty.MP_SPECULAR)
constant(.70, u.MaterialProperty.MP_OPACITY)
assert not list(edit.recompile_material(material))
assert lib.save_loaded_asset(material)
mesh.set_material(0, material)
assert lib.save_loaded_asset(mesh)
actors = es.get_all_level_actors()
brush = next(a for a in actors if 'MC_MenuV2_Brush' in [str(t) for t in a.tags])
foam = next((a for a in actors if a.get_actor_label() == 'MENU V2 | Volumetric brush contact'), None)
if not foam:
    foam = es.spawn_actor_from_class(u.StaticMeshActor, u.Vector())
    foam.set_actor_label('MENU V2 | Volumetric brush contact')
    foam.tags = ['MC_MenuV2_ContactMesh']
component = foam.static_mesh_component
component.set_mobility(u.ComponentMobility.MOVABLE)
component.set_static_mesh(mesh)
component.set_collision_enabled(u.CollisionEnabled.NO_COLLISION)
component.set_cast_shadow(True)
foam.attach_to_actor(brush, '', u.AttachmentRule.KEEP_RELATIVE, u.AttachmentRule.KEEP_RELATIVE, u.AttachmentRule.KEEP_RELATIVE, False)
component.set_relative_location(u.Vector(80, 13, -14), False, True)
component.set_relative_rotation(u.Rotator(), False, True)
component.set_relative_scale3d(u.Vector(1.35, 1.35, .8))
for a in actors:
    if 'MC_MenuV2_Foam' in [str(t) for t in a.tags]:
        a.set_actor_hidden_in_game(True)
        for c in a.get_components_by_class(u.NiagaraComponent):
            c.set_auto_activate(False)
            c.deactivate()
            c.set_visibility(False, True)
seq = lib.load_asset(BASE + '/LS_MenuCleaningV2')
for b in list(seq.get_bindings()):
    if b.get_name() == 'MenuContactFoamV2':
        b.remove()
binding = seq.add_possessable(foam)
binding.set_name('MenuContactFoamV2')
track = binding.add_track(u.MovieScene3DTransformTrack)
section = track.add_section()
section.set_range(0, 288)
channels = section.get_all_channels()
for channel, value in zip(channels, [80., 13., -14., 0., 0., 0., 1.35, 1.35, .8]):
    channel.set_default(value)
for frame, amount in [(0, .8), (38, 1.), (78, .85), (106, 1.15), (128, .7), (165, 1.24), (192, .75), (230, .55), (264, .7), (288, .8)]:
    for channel, basis in zip(channels[6:], [1.35, 1.35, .8]):
        channel.add_key(u.FrameNumber(frame), basis * amount, interpolation=u.MovieSceneKeyInterpolation.AUTO)
# The centre of a foam volume underneath an opaque brush head is occluded by
# the bristles. Keep froth on the head perimeter where it creates a readable
# irregular edge, with no projected particle cards or material sorting.
for suffix, location, scale in [('Top', (94., 0., -14.), (1.2, 1.4, .65)), ('Lower', (78., -13., -14.), (1.05, 1.3, .60))]:
    label = 'MENU V2 | Froth rim ' + suffix
    actor = next((a for a in actors if a.get_actor_label() == label), None)
    if not actor:
        actor = es.spawn_actor_from_class(u.StaticMeshActor, u.Vector())
        actor.set_actor_label(label)
        actor.tags = ['MC_MenuV2_ContactMesh']
    c = actor.static_mesh_component
    c.set_mobility(u.ComponentMobility.MOVABLE)
    c.set_static_mesh(mesh)
    c.set_collision_enabled(u.CollisionEnabled.NO_COLLISION)
    c.set_cast_shadow(True)
    actor.attach_to_actor(brush, '', u.AttachmentRule.KEEP_RELATIVE, u.AttachmentRule.KEEP_RELATIVE, u.AttachmentRule.KEEP_RELATIVE, False)
    c.set_relative_location(u.Vector(*location), False, True)
    c.set_relative_rotation(u.Rotator(), False, True)
    c.set_relative_scale3d(u.Vector(*scale))
    name = 'MenuFrothRim' + suffix
    for b in list(seq.get_bindings()):
        if b.get_name() == name:
            b.remove()
    b = seq.add_possessable(actor)
    b.set_name(name)
    t = b.add_track(u.MovieScene3DTransformTrack)
    s = t.add_section()
    s.set_range(0, 288)
    cs = s.get_all_channels()
    for channel, value in zip(cs, list(location) + [0., 0., 0.] + list(scale)):
        channel.set_default(value)
    for frame, amount in [(0, .8), (38, 1.), (78, .85), (106, 1.15), (128, .7), (165, 1.24), (192, .75), (230, .55), (264, .7), (288, .8)]:
        for channel, basis in zip(cs[6:], scale):
            channel.add_key(u.FrameNumber(frame), basis * amount, interpolation=u.MovieSceneKeyInterpolation.AUTO)
assert lib.save_loaded_asset(seq)
assert ls.save_current_level()
u.log('MENU_CONTACT_FOAM_MESH_SAVED_PENDING_NATIVE_REVIEW')
