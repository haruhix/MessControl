"""Step 2 assets. Creates missing assets only; never rewrites the map/designer values."""
import unreal as u
import os

lib = u.EditorAssetLibrary
assets = u.AssetToolsHelpers.get_asset_tools()
edit = u.MaterialEditingLibrary
path = '/Game/Art/Materials/M_ArenaTooth'
if not lib.does_asset_exist(path) or os.environ.get('MC_REBUILD_ARENA_MATERIAL') == '1':
    mat = lib.load_asset(path) if lib.does_asset_exist(path) else assets.create_asset('M_ArenaTooth', '/Game/Art/Materials', u.Material, u.MaterialFactoryNew())
    edit.delete_all_material_expressions(mat)
    def node(cls, x, y):
        return edit.create_material_expression(mat, cls, x, y)
    def scalar(name, value, y):
        n = node(u.MaterialExpressionScalarParameter, -700, y)
        n.set_editor_property('parameter_name', name)
        n.set_editor_property('default_value', value)
        return n
    def color(name, rgb, y):
        n = node(u.MaterialExpressionVectorParameter, -700, y)
        n.set_editor_property('parameter_name', name)
        n.set_editor_property('default_value', u.LinearColor(*rgb, 1))
        return n
    pos = node(u.MaterialExpressionPreSkinnedPosition, -700, -200)
    interpolated_pos = node(u.MaterialExpressionVertexInterpolator, -420, -200)
    edit.connect_material_expressions(pos,'',interpolated_pos,'')
    custom = node(u.MaterialExpressionCustom, -100, 0)
    custom.set_editor_property('description', 'Enamel + surface coffee + damage cracks')
    custom.set_editor_property('output_type', u.CustomMaterialOutputType.CMOT_FLOAT3)
    names = ['P', 'Coffee', 'Damage', 'Flash', 'Enamel', 'Stain']
    custom_inputs = []
    for name in names:
        entry = u.CustomInput()
        entry.set_editor_property('input_name',name)
        custom_inputs.append(entry)
    custom.set_editor_property('inputs', custom_inputs)
    custom.set_editor_property('code', '''
float waves = sin(P.y*.16 + sin(P.z*.09)*1.8) + sin(P.x*.18 + P.z*.115) + .55*cos(P.z*.26-P.y*.11);
float patch = smoothstep(.05,.35,waves) * Coffee;
float3 base = lerp(Enamel,Stain*(.8+.2*sin(P.z*.3)),patch);
float crack = 1-smoothstep(.025,.075,abs(sin(P.z*.13+P.y*.085+sin(P.y*.16)*.8)));
crack *= smoothstep(.15,.65,Damage) * smoothstep(25,65,P.z);
base = lerp(base, float3(.075,.024,.012), crack*.85);
base = lerp(base,float3(1,.3,.08),Flash*.55);
return base;
''')
    inputs = [interpolated_pos, scalar('Coffee',0,0), scalar('Damage',0,100), scalar('HitFlash',0,200),
              color('EnamelColor',(.94,.88,.72),300), color('CoffeeColor',(.18,.052,.008),450)]
    for source, name in zip(inputs,names):
        edit.connect_material_expressions(source,'',custom,name)
    edit.connect_material_property(custom,'',u.MaterialProperty.MP_BASE_COLOR)
    rough = scalar('EnamelRoughness',.22,600)
    edit.connect_material_property(rough,'',u.MaterialProperty.MP_ROUGHNESS)
    edit.recompile_material(mat)
    if not lib.save_loaded_asset(mat, only_if_is_dirty=False):
        raise RuntimeError('Could not save arena material')

path = '/Game/Data/DA_ArenaTooth'
if not lib.does_asset_exist(path):
    factory = u.DataAssetFactory()
    factory.set_editor_property('data_asset_class',u.MCArenaToothProfile)
    profile = assets.create_asset('DA_ArenaTooth','/Game/Data',u.MCArenaToothProfile,factory)
    if not profile or not lib.save_loaded_asset(profile, only_if_is_dirty=False):
        raise RuntimeError('Could not save arena profile')
u.log('MC_ARENA_ASSETS_PASS: material and profile ready; map unchanged')
