"""Connect the artist's skeletal player and create its gameplay material and physics.
Existing assets are preserved. Does not change the artist's mesh, material, or any map.
Run after building the Editor target, with PIE stopped.
"""
import unreal as u

lib = u.EditorAssetLibrary
edit = u.MaterialEditingLibrary
assets = u.AssetToolsHelpers.get_asset_tools()
material_path = '/Game/Art/Materials/M_TeethGameplay'
instance_path = '/Game/Art/Materials/MI_TeethPlayer'

if not lib.does_asset_exist(material_path):
    mat = lib.duplicate_asset('/Game/Art/Materials/MasterMaterial', material_path)
    if not mat:
        raise RuntimeError('Missing artist MasterMaterial')
    base = edit.get_material_property_input_node(mat, u.MaterialProperty.MP_BASE_COLOR)
    base_output = edit.get_material_property_input_node_output_name(mat, u.MaterialProperty.MP_BASE_COLOR)
    if not base:
        raise RuntimeError('Artist material has no Base Color input')
    def node(cls, x, y):
        return edit.create_material_expression(mat, cls, x, y)
    def scalar(name, y):
        n = node(u.MaterialExpressionScalarParameter, 300, y)
        n.set_editor_property('parameter_name', name)
        n.set_editor_property('default_value', 0)
        return n
    pos = node(u.MaterialExpressionPreSkinnedPosition, 0, 800)
    interp = node(u.MaterialExpressionVertexInterpolator, 220, 800)
    assert edit.connect_material_expressions(pos, '', interp, '')
    custom = node(u.MaterialExpressionCustom, 650, 600)
    custom.set_editor_property('description', 'Gameplay stains over the original textured enamel')
    custom.set_editor_property('output_type', u.CustomMaterialOutputType.CMOT_FLOAT3)
    inputs = []
    for name in ['Enamel', 'P', 'Coffee', 'Damage', 'Flash']:
        entry = u.CustomInput()
        entry.set_editor_property('input_name', name)
        inputs.append(entry)
    custom.set_editor_property('inputs', inputs)
    custom.set_editor_property('code', '''
float waves = sin(P.y*.16 + sin(P.z*.09)*1.8) + sin(P.x*.18 + P.z*.115) + .55*cos(P.z*.26-P.y*.11);
float patch = smoothstep(.05,.35,waves) * saturate(Coffee);
float3 color = lerp(Enamel,float3(.18,.052,.008)*(.8+.2*sin(P.z*.3)),patch);
float crack = 1-smoothstep(.025,.075,abs(sin(P.z*.13+P.y*.085+sin(P.y*.16)*.8)));
crack *= smoothstep(.15,.65,Damage) * smoothstep(25,65,P.z);
color = lerp(color,float3(.075,.024,.012),crack*.85);
return lerp(color,float3(1,.3,.08),saturate(Flash)*.55);
''')
    assert edit.connect_material_expressions(base, base_output, custom, 'Enamel')
    for source, name in [(interp, 'P'), (scalar('Coffee', 950), 'Coffee'),
                         (scalar('Damage', 1080), 'Damage'), (scalar('HitFlash', 1210), 'Flash')]:
        assert edit.connect_material_expressions(source, '', custom, name)
    assert edit.connect_material_property(custom, '', u.MaterialProperty.MP_BASE_COLOR)
    errors = edit.recompile_material(mat)
    if errors:
        raise RuntimeError(str(errors))
    assert lib.save_loaded_asset(mat, only_if_is_dirty=False)

if not lib.does_asset_exist(instance_path):
    instance = lib.duplicate_asset('/Game/Art/Materials/MI_Teeth', instance_path)
    if not instance:
        raise RuntimeError('Missing artist MI_Teeth')
    edit.set_material_instance_parent(instance, lib.load_asset(material_path))
    edit.update_material_instance(instance)
    assert lib.save_loaded_asset(instance, only_if_is_dirty=False)

mat = lib.load_asset(material_path)
edit.set_base_material_usage(mat, u.MaterialUsage.MATUSAGE_SKELETAL_MESH)
if 'BodyStretch' not in [str(n) for n in edit.get_scalar_parameter_names(mat)]:
    world = edit.create_material_expression(mat, u.MaterialExpressionWorldPosition, 100, 1500)
    local = edit.create_material_expression(mat, u.MaterialExpressionTransformPosition, 320, 1500)
    local.set_editor_property('transform_source_type', u.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_WORLD)
    local.set_editor_property('transform_type', u.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_LOCAL)
    assert edit.connect_material_expressions(world, '', local, '')
    amount = edit.create_material_expression(mat, u.MaterialExpressionScalarParameter, 320, 1650)
    amount.set_editor_property('parameter_name', 'BodyStretch')
    offset = edit.create_material_expression(mat, u.MaterialExpressionCustom, 550, 1500)
    offset.set_editor_property('description', 'Cosmetic squash/stretch; collision shapes stay stable')
    offset.set_editor_property('output_type', u.CustomMaterialOutputType.CMOT_FLOAT3)
    inputs=[]
    for name in ['P','S']:
        entry=u.CustomInput();entry.set_editor_property('input_name',name);inputs.append(entry)
    offset.set_editor_property('inputs', inputs)
    offset.set_editor_property('code', 'float h=clamp(1+S,.65,1.4); float w=rsqrt(h); return float3(P.xy*(w-1),P.z*(h-1));')
    assert edit.connect_material_expressions(local, '', offset, 'P')
    assert edit.connect_material_expressions(amount, '', offset, 'S')
    transform=edit.create_material_expression(mat,u.MaterialExpressionTransform,800,1500)
    transform.set_editor_property('transform_source_type',u.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL)
    transform.set_editor_property('transform_type',u.MaterialVectorCoordTransform.TRANSFORM_WORLD)
    assert edit.connect_material_expressions(offset,'',transform,'')
    assert edit.connect_material_property(transform,'',u.MaterialProperty.MP_WORLD_POSITION_OFFSET)
errors=edit.recompile_material(mat)
if errors: raise RuntimeError(str(errors))
assert lib.save_loaded_asset(mat,only_if_is_dirty=False)

path = '/Game/Data/DA_PlayerAppearance'
if not lib.does_asset_exist(path):
    factory = u.DataAssetFactory()
    factory.set_editor_property('data_asset_class', u.MCPlayerAppearance)
    profile = assets.create_asset('DA_PlayerAppearance', '/Game/Data', u.MCPlayerAppearance, factory)
profile = lib.load_asset(path)
if not profile.get_editor_property('skeletal_mesh'):
    profile.set_editor_property('skeletal_mesh', lib.load_asset('/Game/Art/Meshes/Character/SM_Teeth_rig'))
    profile.set_editor_property('material', lib.load_asset(instance_path))
    profile.set_editor_property('mesh_transform', u.Transform(location=u.Vector(0,0,-58), rotation=u.Rotator(pitch=0,yaw=-90,roll=0)))
    profile.set_editor_property('brush_transform', u.Transform(location=u.Vector(-8,3,-4), rotation=u.Rotator(pitch=0,yaw=90,roll=0)))
    profile.set_editor_property('bone_map', {'body':'root_x','arm_l':'arm_stretch_l','arm_r':'arm_stretch_r',
        'forearm_l':'forearm_stretch_l','forearm_r':'forearm_stretch_r','leg_l':'thigh_stretch_l','leg_r':'thigh_stretch_r',
        'knee_l':'leg_stretch_l','knee_r':'leg_stretch_r','fingertip_l':'index3_l','fingertip_r':'index3_r','toe_l':'toes_01_l','toe_r':'toes_01_r'})
if not profile.get_editor_property('physics_asset'):
    physics = u.MCPhysicsAssetBuilder.build_player_physics_asset(profile)
    if not physics: raise RuntimeError('Player Physics Asset failed')
    profile.set_editor_property('physics_asset', physics)
assert lib.save_loaded_asset(profile, only_if_is_dirty=False)
u.log('MC_PLAYER_APPEARANCE_ASSETS_PASS')
