"""Legacy initial construction of the reward-claim material pair.

The runtime keeps the ring hidden. For the current airborne sparkle appearance,
run author_reward_chest_spark_readability.py after this initial construction.
No map/Blueprint changes are authored here.
"""
import json
import shutil
from pathlib import Path
import unreal as u

OUT = Path(globals().get('MC_CHEST_VFX_AUTHOR_OUTPUT',
    'E:/CodexWork/HubChestWardrobe_20261022/ChestVFXAuthoring')).resolve()
assert OUT.drive.upper() == 'E:'
assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages(), 'Preserve unsaved map edits'
assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages(), 'Preserve unsaved content edits'
OUT.mkdir(parents=True, exist_ok=True)
LIB, MEL = u.EditorAssetLibrary, u.MaterialEditingLibrary
FOLDER = '/Game/Gameplay/VFX/RewardChest'
ROOT = Path(u.Paths.project_dir())
tools = u.AssetToolsHelpers.get_asset_tools()
report = []

RING = '''float2 p=(UV-.5)*2;
float r=length(p);
float aa=max(fwidth(r)*1.5,.006);
float ring=1-smoothstep(.022,.022+aa,abs(r-.73));
float soft=exp(-pow((r-.73)*19,2))*.32;
float inner=(1-smoothstep(.0,.68,r))*.055;
return saturate((ring+soft+inner)*(1-smoothstep(.94,1,r)));'''
SPARK = '''float2 p=abs((UV-.5)*2);
float aa=max(fwidth(p.x)+fwidth(p.y),.015);
float diamond=1-smoothstep(.16,.16+aa,p.x+p.y);
float cross=exp(-p.x*p.x*190)*exp(-p.y*p.y*8)
    +exp(-p.y*p.y*190)*exp(-p.x*p.x*8);
float core=exp(-dot(p,p)*38);
return saturate(diamond+cross*.65+core*.5);'''

for name, code, tint in [
        ('M_RewardChest_ClaimRing', RING, u.LinearColor(.35,1.,.76,1.)),
        ('M_RewardChest_ClaimSpark', SPARK, u.LinearColor(1.,.64,.14,1.))]:
    asset_path = FOLDER + '/' + name
    material = LIB.load_asset(asset_path)
    existing = bool(material)
    if existing:
        package = ROOT / ('Content/' + asset_path.removeprefix('/Game/') + '.uasset')
        backup = OUT / 'BeforePackages' / package.name
        backup.parent.mkdir(parents=True, exist_ok=True)
        if not backup.exists():
            shutil.copy2(package, backup)
        MEL.delete_all_material_expressions(material)
    else:
        material = tools.create_asset(name, FOLDER, u.Material, u.MaterialFactoryNew())
    material.set_editor_properties(dict(blend_mode=u.BlendMode.BLEND_ADDITIVE,
        shading_model=u.MaterialShadingModel.MSM_UNLIT, two_sided=True,
        disable_depth_test=False, used_with_instanced_static_meshes=('Spark' in name)))
    uv = MEL.create_material_expression(material, u.MaterialExpressionTextureCoordinate, -650, 0)
    shape = MEL.create_material_expression(material, u.MaterialExpressionCustom, -450, 0)
    item = u.CustomInput(); item.set_editor_property('input_name', 'UV')
    shape.set_editor_properties(dict(code=code, inputs=[item],
        output_type=u.CustomMaterialOutputType.CMOT_FLOAT1,
        description='Reward claim ring' if 'Ring' in name else 'Reward claim sparkle'))
    MEL.connect_material_expressions(uv, '', shape, 'UV')
    color = MEL.create_material_expression(material, u.MaterialExpressionVectorParameter, -450, 200)
    color.set_editor_properties(dict(parameter_name='BurstColor', default_value=tint))
    opacity = MEL.create_material_expression(material, u.MaterialExpressionScalarParameter, -450, 350)
    opacity.set_editor_properties(dict(parameter_name='BurstOpacity', default_value=0.))
    multiply = MEL.create_material_expression(material, u.MaterialExpressionMultiply, -200, 30)
    MEL.connect_material_expressions(shape, '', multiply, 'A')
    MEL.connect_material_expressions(opacity, '', multiply, 'B')
    MEL.connect_material_property(multiply, '', u.MaterialProperty.MP_OPACITY)
    MEL.connect_material_property(color, '', u.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.recompile_material(material)
    assert LIB.save_loaded_asset(material, only_if_is_dirty=False)
    report.append(dict(asset=asset_path, replaced=existing,
        scalar_parameters=[str(p) for p in MEL.get_scalar_parameter_names(material)],
        vector_parameters=[str(p) for p in MEL.get_vector_parameter_names(material)]))
(OUT / 'Authoring.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
u.log('MC_CHEST_CLAIM_VFX_AUTHORED ' + str(OUT / 'Authoring.json'))
