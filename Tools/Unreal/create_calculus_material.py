"""Import baked Substance mineral maps and author the opaque calculus material.

UV0 is surface centimetres / 20; vertex alpha identifies fresh fracture faces.
Only the owned material and its three textures are saved. No map changes.
"""
from pathlib import Path
import unreal as u

lib, edit = u.EditorAssetLibrary, u.MaterialEditingLibrary
root = Path(u.Paths.project_dir()).resolve()
folder = '/Game/Gameplay/Care'
maps = {}
for key, linear, normal in [('basecolor', False, False), ('normal', True, True), ('roughness', True, False)]:
    task = u.AssetImportTask()
    task.filename = str(root / 'SourceArt/CalculusMaterial/Textures' / ('MC_DentalCalculus_' + key + '.png'))
    assert Path(task.filename).is_file(), task.filename
    task.destination_path = folder + '/Textures'
    task.destination_name = 'T_Calculus_' + key.title()
    task.automated = True
    task.replace_existing = True
    task.save = False
    u.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    texture = lib.load_asset(task.imported_object_paths[0])
    texture.set_editor_property('srgb', not linear)
    texture.set_editor_property('compression_settings', u.TextureCompressionSettings.TC_NORMALMAP if normal else
                                u.TextureCompressionSettings.TC_GRAYSCALE if linear else u.TextureCompressionSettings.TC_DEFAULT)
    texture.set_editor_property('max_texture_size', 1024)
    assert lib.save_loaded_asset(texture, only_if_is_dirty=False)
    maps[key] = texture

path = folder + '/M_ToothCalculus'
material = lib.load_asset(path) if lib.does_asset_exist(path) else u.AssetToolsHelpers.get_asset_tools().create_asset(
    'M_ToothCalculus', folder, u.Material, u.MaterialFactoryNew())
assert isinstance(material, u.Material)
edit.delete_all_material_expressions(material)
material.set_editor_property('blend_mode', u.BlendMode.BLEND_OPAQUE)
material.set_editor_property('shading_model', u.MaterialShadingModel.MSM_DEFAULT_LIT)
material.set_editor_property('two_sided', False)
material.set_editor_property('dithered_lod_transition', False)

def node(cls, x=0, y=0):
    return edit.create_material_expression(material, cls, x, y)

def link(a, b, pin, output=''):
    assert edit.connect_material_expressions(a, output, b, pin), (pin, output)

def scalar(name, value):
    result = node(u.MaterialExpressionScalarParameter)
    result.set_editor_property('parameter_name', name)
    result.set_editor_property('default_value', value)
    result.set_editor_property('group', 'Mineral surface')
    return result

def custom(label, code, inputs, kind):
    result = node(u.MaterialExpressionCustom)
    result.set_editor_property('description', label)
    result.set_editor_property('code', code)
    result.set_editor_property('output_type', kind)
    entries = []
    for name, source, output in inputs:
        entry = u.CustomInput()
        entry.set_editor_property('input_name', name)
        entries.append(entry)
    result.set_editor_property('inputs', entries)
    for name, source, output in inputs:
        link(source, result, name, output)
    return result

uv = node(u.MaterialExpressionTextureCoordinate)
vertex = node(u.MaterialExpressionVertexColor)
samples = {}
for key, texture in maps.items():
    sample = node(u.MaterialExpressionTextureSampleParameter2D)
    sample.set_editor_property('parameter_name', 'Mineral_' + key.title())
    sample.set_editor_property('texture', texture)
    sample.set_editor_property('sampler_type', u.MaterialSamplerType.SAMPLERTYPE_NORMAL if key == 'normal' else
                               u.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE if key == 'roughness' else u.MaterialSamplerType.SAMPLERTYPE_COLOR)
    link(uv, sample, 'UVs')
    samples[key] = sample

fresh_tint = node(u.MaterialExpressionVectorParameter)
fresh_tint.set_editor_property('parameter_name', 'FreshChalk')
fresh_tint.set_editor_property('default_value', u.LinearColor(.94, .88, .72, 1))
color = custom('Calculus: mineral pigment and fresh fracture', '''
float pigment = saturate(dot(Vertex, float3(.25,.5,.25)));
float3 crust = Aged * lerp(.92,1.08,pigment);
float3 chalk = Chalk * lerp(.94,1.02,dot(Aged,float3(.333,.333,.333)));
return lerp(crust,chalk,saturate(Fresh));
''', [('Aged', samples['basecolor'], ''), ('Vertex', vertex, ''), ('Fresh', vertex, 'A'), ('Chalk', fresh_tint, '')], u.CustomMaterialOutputType.CMOT_FLOAT3)
rough = custom('Calculus: damp crust and dry broken core',
               'return lerp(clamp(Rough*(1-saturate(Damp)),.42,.78),.88,saturate(Fresh));',
               [('Rough', samples['roughness'], 'R'), ('Damp', scalar('Dampness', .18), ''), ('Fresh', vertex, 'A')], u.CustomMaterialOutputType.CMOT_FLOAT1)
normal = custom('Calculus: fine baked mineral pores',
                'return normalize(lerp(float3(0,0,1),Detail,saturate(Strength)*lerp(1,.3,saturate(Fresh))));',
                [('Detail', samples['normal'], ''), ('Strength', scalar('PoreStrength', .4), ''), ('Fresh', vertex, 'A')], u.CustomMaterialOutputType.CMOT_FLOAT3)
for source, output in [(color, u.MaterialProperty.MP_BASE_COLOR), (rough, u.MaterialProperty.MP_ROUGHNESS),
                       (normal, u.MaterialProperty.MP_NORMAL), (scalar('Specular', .3), u.MaterialProperty.MP_SPECULAR),
                       (scalar('Metallic', 0), u.MaterialProperty.MP_METALLIC)]:
    assert edit.connect_material_property(source, '', output)
edit.layout_material_expressions(material)
errors = edit.recompile_material(material)
assert not errors, str(errors)
assert lib.save_loaded_asset(material, only_if_is_dirty=False)
u.log('MC_CALCULUS_MATERIAL_SAVED baked_mineral_3_samples alpha_fracture')
