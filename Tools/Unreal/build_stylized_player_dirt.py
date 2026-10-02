"""Apply the reference-inspired player grime in a stopped Unreal Editor.

Run through the editor's Python connection. Existing artist textures, status
parameters and deformation stay connected; the saved material is the result.
"""
import json
import shutil
from datetime import datetime, timezone
from pathlib import Path

import unreal as u


MATERIAL = '/Game/Art/Materials/M_TeethGameplay'
INSTANCES = (
    '/Game/Gameplay/CharacterCurrent/Materials/MI_Character',
    '/Game/Gameplay/CharacterCurrent/Materials/MI_Bag',
    '/Game/Art/Materials/MI_TeethPlayer',
)
SCALARS = {'GrimePatchSize': 16.0, 'GrimeEdgeSoftness': .010, 'GrimeOpacity': .94}
COLORS = {
    'GrimeLightColor': (.36, .16, .044, 1),
    'GrimeDarkColor': (.18, .064, .014, 1),
    'GrimeRimColor': (.50, .26, .07, 1),
}

# P is interpolated pre-skinned position, so stains follow the animated mesh.
# Face exclusion ellipsoids match CharacterCurrent's reference-pose coordinates.
MASK_CODE = '''
struct RoundedGrime {
    float hash(float3 p) {
        p = frac(p * .1031);
        p += dot(p, p.yzx + 33.33);
        return frac((p.x + p.y) * p.z);
    }
    float noise(float3 p) {
        float3 i = floor(p), f = frac(p);
        f = f * f * (3 - 2 * f);
        float a = lerp(hash(i), hash(i + float3(1,0,0)), f.x);
        float b = lerp(hash(i + float3(0,1,0)), hash(i + float3(1,1,0)), f.x);
        float c = lerp(hash(i + float3(0,0,1)), hash(i + float3(1,0,1)), f.x);
        float d = lerp(hash(i + float3(0,1,1)), hash(i + float3(1,1,1)), f.x);
        return lerp(lerp(a,b,f.y), lerp(c,d,f.y), f.z);
    }
    float protect(float3 p, float3 center, float3 radius) {
        return 1 - smoothstep(.85, 1.15, length((p - center) / radius));
    }
};
RoundedGrime G;
float amount = saturate(Coffee);
float3 q = P / max(Size, 8) + float3(3.71,8.13,1.47);
float field = .65 * G.noise(q)
    + .25 * G.noise(q * 2.3 + float3(9.2,2.7,5.1))
    + .10 * G.noise(q * 5.5 + float3(4.7,3.1,8.2));
field += .045 * smoothstep(25,42,abs(P.x))
    + .055 * (1 - smoothstep(25,55,P.z));
float threshold = lerp(.80,.55,pow(amount,.8));
float edge = max(max(Softness,.008),fwidth(field) * .8);
float coverage = smoothstep(threshold-edge,threshold+edge,field)
    * smoothstep(0,.035,amount);
float core = smoothstep(threshold+.055,threshold+.17,field);
float rim = 1 - smoothstep(threshold+.02,threshold+.065,field);
float face = max(
    G.protect(P,float3(17.67,20.21,80.20),float3(15,16,16)),
    G.protect(P,float3(-17.67,20.21,80.20),float3(15,16,16)));
face = max(face,G.protect(P,float3(0,27,55),float3(19,17,13)));
return float3(coverage * (1-face), core, rim);
'''

COLOR_CODE = '''
float3 dirt = lerp(GrimeLight,GrimeDark,Stain.y);
dirt = lerp(dirt,GrimeRim,Stain.z * .55);
float enamel = smoothstep(.20,.48,min(Enamel.r,min(Enamel.g,Enamel.b)));
float patch = Stain.x * saturate(GrimeOpacity) * enamel;
float3 color = lerp(Enamel,dirt,patch);
'''

assert not u.EditorLevelLibrary.get_pie_worlds(False), 'Stop PIE before authoring the material.'
lib = u.EditorAssetLibrary
edit = u.MaterialEditingLibrary
mat = lib.load_asset(MATERIAL)
assert isinstance(mat, u.Material)
nodes = list(edit.get_material_expressions(mat))
by_name = {node.get_name(): node for node in nodes}
color_node = by_name['MaterialExpressionCustom_0']
assert color_node.get_editor_property('description') == 'Gameplay stains over the original textured enamel'
original_code = color_node.get_editor_property('code')
assert 'float crack' in original_code and 'saturate(Flash)' in original_code
damage_tail = 'float crack' + original_code.split('float crack', 1)[1]
enamel = by_name['MaterialExpressionLinearInterpolate_1']
position = by_name['MaterialExpressionVertexInterpolator_0']
roughness = by_name['MaterialExpressionMultiply_13']
specular = by_name['MaterialExpressionMultiply_12']
parameters = {
    str(node.get_editor_property('parameter_name')): node
    for node in nodes
    if isinstance(node, (u.MaterialExpressionScalarParameter, u.MaterialExpressionVectorParameter))
}
for name in ('Coffee', 'Damage', 'HitFlash', 'BodyStretch'):
    assert name in parameters, name

project = Path(u.Paths.project_dir())
saved = Path(u.Paths.project_saved_dir())
backup = saved / 'CharacterDirtBackups' / datetime.now(timezone.utc).strftime('%Y%m%d_%H%M%S_%f')
backup.mkdir(parents=True)
shutil.copy2(project / 'Content/Art/Materials/M_TeethGameplay.uasset', backup / 'M_TeethGameplay.uasset')
(backup / 'PreviousColor.hlsl').write_text(original_code, encoding='utf-8')


def scalar(name, value, x, y):
    node = parameters.get(name)
    if node is None:
        node = edit.create_material_expression(mat, u.MaterialExpressionScalarParameter, x, y)
        node.set_editor_property('parameter_name', name)
    node.set_editor_property('default_value', value)
    node.set_editor_property('group', 'Character grime')
    return node


def vector(name, value, x, y):
    node = parameters.get(name)
    if node is None:
        node = edit.create_material_expression(mat, u.MaterialExpressionVectorParameter, x, y)
        node.set_editor_property('parameter_name', name)
    node.set_editor_property('default_value', u.LinearColor(*value))
    node.set_editor_property('group', 'Character grime')
    return node


def custom(description, code, inputs, output_type, x, y, node=None):
    if node is None:
        node = next((n for n in nodes if isinstance(n, u.MaterialExpressionCustom)
                     and n.get_editor_property('description') == description), None)
    if node is None:
        node = edit.create_material_expression(mat, u.MaterialExpressionCustom, x, y)
    node.set_editor_property('description', description)
    node.set_editor_property('output_type', output_type)
    # Keep other status-layer pins when reapplied after fracture authoring.
    entries = list(node.get_editor_property('inputs'))
    names = {str(entry.get_editor_property('input_name')) for entry in entries}
    for name in inputs:
        if name not in names:
            entry = u.CustomInput()
            entry.set_editor_property('input_name', name)
            entries.append(entry)
    node.set_editor_property('inputs', entries)
    node.set_editor_property('code', code)
    for name, source in inputs.items():
        assert edit.connect_material_expressions(source, '', node, name), name
    return node


with u.ScopedEditorTransaction('Stylized player grime'):
    size = scalar('GrimePatchSize', SCALARS['GrimePatchSize'], 800, 2700)
    softness = scalar('GrimeEdgeSoftness', SCALARS['GrimeEdgeSoftness'], 800, 2900)
    opacity = scalar('GrimeOpacity', SCALARS['GrimeOpacity'], 800, 3100)
    light = vector('GrimeLightColor', COLORS['GrimeLightColor'], 1300, 2900)
    dark = vector('GrimeDarkColor', COLORS['GrimeDarkColor'], 1300, 3100)
    rim = vector('GrimeRimColor', COLORS['GrimeRimColor'], 1300, 3300)
    mask = custom('Rounded player grime mask', MASK_CODE,
                  {'P': position, 'Coffee': parameters['Coffee'], 'Size': size, 'Softness': softness},
                  u.CustomMaterialOutputType.CMOT_FLOAT3, 1100, 2600)
    custom('Gameplay stains over the original textured enamel', COLOR_CODE + damage_tail,
           {'Enamel': enamel, 'P': position, 'Coffee': parameters['Coffee'],
            'Damage': parameters['Damage'], 'Flash': parameters['HitFlash'],
            'Stain': mask, 'GrimeLight': light, 'GrimeDark': dark,
            'GrimeRim': rim, 'GrimeOpacity': opacity},
           u.CustomMaterialOutputType.CMOT_FLOAT3, 1800, 2600, color_node)
    surface_guard = 'float enamel = smoothstep(.20,.48,min(Enamel.r,min(Enamel.g,Enamel.b)));\n'
    rough = custom('Matte player grime', surface_guard +
                   'return lerp(Base,max(Base,.64),Stain.x * enamel * .8);',
                   {'Enamel': enamel, 'Base': roughness, 'Stain': mask},
                   u.CustomMaterialOutputType.CMOT_FLOAT1, 1800, 2900)
    spec = custom('Reduced player grime shine', surface_guard +
                  'return lerp(Base,min(Base,.28),Stain.x * enamel * .75);',
                  {'Enamel': enamel, 'Base': specular, 'Stain': mask},
                  u.CustomMaterialOutputType.CMOT_FLOAT1, 1800, 3100)
    broken_enamel = next((n for n in nodes if isinstance(n,u.MaterialExpressionCustom)
                         and n.get_editor_property('description')=='Matte broken enamel'),None)
    if broken_enamel:
        assert edit.connect_material_expressions(rough,'',broken_enamel,'Base')
    assert edit.connect_material_property(broken_enamel or rough, '', u.MaterialProperty.MP_ROUGHNESS)
    assert edit.connect_material_property(spec, '', u.MaterialProperty.MP_SPECULAR)
    errors = list(edit.recompile_material(mat))
    assert not errors, errors
    assert lib.save_loaded_asset(mat, only_if_is_dirty=False)

updated_instances = []
for path in INSTANCES:
    instance = lib.load_asset(path)
    if instance:
        assert instance.get_editor_property('parent') == mat, path
        edit.update_material_instance(instance)
        assert lib.save_loaded_asset(instance, only_if_is_dirty=True)
        updated_instances.append(path)

report = {
    'material': MATERIAL,
    'updated_instances': updated_instances,
    'backup': str(backup),
    'compile_errors': errors,
    'scalar_defaults': SCALARS,
    'color_defaults_linear': COLORS,
    'preserved': ['artist textures', 'Coffee', 'Damage', 'HitFlash', 'BodyStretch', 'Normal'],
    'cleaning': 'Coffee raises the noise threshold as it decreases, shrinking patches to zero.',
}
(saved / 'StylizedCharacterDirt.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
u.log('STYLIZED_CHARACTER_DIRT ' + json.dumps(report))
