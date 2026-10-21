"""Run in Unreal Editor to add independent road roughness/normal controls."""
import json
import shutil
from datetime import datetime
from pathlib import Path

import unreal as u


ROOT = Path(u.Paths.project_dir())
OUT = ROOT / 'Saved/HubGroundPaint_20261021/RoadControls'
OUT.mkdir(parents=True, exist_ok=True)
LIB = u.MaterialEditingLibrary
MAT = u.load_asset('/Game/Art/Materials/hub/M_HubGroundPaint')
MI = u.load_asset('/Game/Art/Materials/hub/MI_HubGroundPaint')
assert MAT and MI and MI.get_editor_property('parent') == MAT
assert hasattr(LIB, 'set_material_instance_static_switch_parameter_value')

NODES = [n for n in u.ObjectIterator(u.MaterialExpression.static_class()) if n.get_outer() == MAT]
BEFORE_TEXTURES = {str(n): LIB.get_material_instance_texture_parameter_value(MI, n)
                   for n in LIB.get_texture_parameter_names(MAT)}
BEFORE_DIRTY = {'maps': [p.get_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_map_packages()],
                'content': [p.get_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()]}
stamp = datetime.now().strftime('%Y%m%d_%H%M%S')
for name in ['M_HubGroundPaint', 'MI_HubGroundPaint']:
    shutil.copy2(ROOT / f'Content/Art/Materials/hub/{name}.uasset', OUT / f'{name}_{stamp}.uasset')


def find_desc(desc):
    return next(n for n in NODES if n.get_editor_property('desc') == desc)


def find_param(name):
    for n in NODES:
        try:
            if str(n.get_editor_property('parameter_name')) == name:
                return n
        except Exception:
            pass
    raise KeyError(name)


def node(cls, desc, x, y, **props):
    existing = [n for n in NODES if n.get_editor_property('desc') == desc]
    if existing:
        return existing[0]
    n = LIB.create_material_expression(MAT, getattr(u, cls), x, y)
    n.set_editor_property('desc', desc)
    for key, value in props.items():
        n.set_editor_property(key, value)
    NODES.append(n)
    return n


def connect(source, output, dest, pin):
    inputs = list(LIB.get_material_expression_input_names(dest))
    if pin not in inputs and len(inputs) == 1:
        pin = inputs[0]
    assert LIB.connect_material_expressions(source, output, dest, pin), (source, dest, pin)


def scalar(name, value, min_value, max_value, x, y, priority):
    return node('MaterialExpressionScalarParameter', name, x, y,
                parameter_name=name, default_value=value, group='Road',
                slider_min=min_value, slider_max=max_value, sort_priority=priority)


def separate_switch(desc, x, y, packed, packed_pin, separate, separate_pin):
    n = node('MaterialExpressionStaticSwitchParameter', desc, x, y,
             parameter_name='Road Use Separate Roughness', default_value=False,
             group='Road', sort_priority=9)
    connect(separate, separate_pin, n, 'True')
    connect(packed, packed_pin, n, 'False')
    return n


MAT.modify()
MI.modify()
normal = find_param('Road Normal')
orm = find_param('Road ORM')
normal_blend = find_desc('Grass / Road MP_NORMAL')
rough_blend = find_desc('Grass / Road MP_ROUGHNESS')
ao_blend = find_desc('Grass / Road ambient occlusion')
metal_blend = find_desc('Grass / Road MP_METALLIC')

# Scale the tangent-space XY components and retain positive Z. Zero is flat;
# one preserves the source normal, and larger values increase its slope.
normal_strength = scalar('Road Normal Strength', 1, 0, 3, -800, 3700, 5)
xy = node('MaterialExpressionComponentMask', 'Road normal XY', -500, 2600,
          r=True, g=True, b=False, a=False)
z = node('MaterialExpressionComponentMask', 'Road normal Z', -500, 2780,
         r=False, g=False, b=True, a=False)
scaled_xy = node('MaterialExpressionMultiply', 'Road normal strength XY', -220, 2600)
append = node('MaterialExpressionAppendVector', 'Road scaled tangent normal', 40, 2600)
normalized = node('MaterialExpressionNormalize', 'Road strength normalized', 300, 2600)
connect(normal, 'RGB', xy, 'Input')
connect(normal, 'RGB', z, 'Input')
connect(xy, '', scaled_xy, 'A')
connect(normal_strength, '', scaled_xy, 'B')
connect(scaled_xy, '', append, 'A')
connect(z, '', append, 'B')
connect(append, '', normalized, 'VectorInput')
connect(normalized, '', normal_blend, 'B')

# Support a standalone roughness map as well as the original packed ORM set.
current_orm = BEFORE_TEXTURES['Road ORM']
texture_name = current_orm.get_name().lower()
is_standalone = ('roughness' in texture_name.split('_')
                 and not any(t in texture_name for t in ['occlusion', 'metallic', 'orm', 'arm', 'rma', 'packed']))
if is_standalone:
    # Roughness is linear data, not an sRGB color image.
    current_orm.modify()
    current_orm.set_editor_property('srgb', False)
    current_orm.set_editor_property('compression_settings', u.TextureCompressionSettings.TC_MASKS)
rough_texture = node('MaterialExpressionTextureSampleParameter2D', 'Road Roughness Texture',
                     -800, 4050, parameter_name='Road Roughness Texture',
                     texture=current_orm, sampler_type=u.MaterialSamplerType.SAMPLERTYPE_MASKS,
                     group='Road', sort_priority=6)
connect(find_desc('Road UV scale'), '', rough_texture, 'UVs')
source = separate_switch('Road packed or separate roughness', -350, 4050,
                         orm, 'G', rough_texture, 'R')
rough_strength = scalar('Road Roughness Strength', 1, 0, 2, -350, 4350, 7)
rough_offset = scalar('Road Roughness Offset', 0, -1, 1, -350, 4540, 8)
mul = node('MaterialExpressionMultiply', 'Road roughness scale', -50, 4050)
add = node('MaterialExpressionAdd', 'Road roughness offset', 210, 4050)
clamp = node('MaterialExpressionSaturate', 'Road roughness 0 to 1', 470, 4050)
connect(source, '', mul, 'A')
connect(rough_strength, '', mul, 'B')
connect(mul, '', add, 'A')
connect(rough_offset, '', add, 'B')
connect(add, '', clamp, 'Input')
connect(clamp, '', rough_blend, 'B')
one = node('MaterialExpressionConstant', 'Road separate map AO default', -350, 4770, r=1)
zero = node('MaterialExpressionConstant', 'Road separate map metal default', -350, 4930, r=0)
ao = separate_switch('Road packed or separate AO', -50, 4770, orm, 'R', one, '')
metal = separate_switch('Road packed or separate metal', -50, 5000, orm, 'B', zero, '')
connect(ao, '', ao_blend, 'B')
connect(metal, '', metal_blend, 'B')

LIB.recompile_material(MAT)
overridden_scalars = {str(v.parameter_info.name) for v in MI.get_editor_property('scalar_parameter_values')}
for name, value in [('Road Normal Strength', 1), ('Road Roughness Strength', 1),
                    ('Road Roughness Offset', 0)]:
    if name not in overridden_scalars:
        # UE 5.8 setters return false even after a successful write; read back.
        LIB.set_material_instance_scalar_parameter_value(MI, name, value)
        assert LIB.get_material_instance_scalar_parameter_value(MI, name) == value
if not (OUT / 'authoring.json').exists():
    LIB.set_material_instance_static_switch_parameter_value(MI, 'Road Use Separate Roughness', is_standalone)
    assert LIB.get_material_instance_static_switch_parameter_value(MI, 'Road Use Separate Roughness') == is_standalone
    if is_standalone:
        LIB.set_material_instance_texture_parameter_value(MI, 'Road Roughness Texture', current_orm)
        assert LIB.get_material_instance_texture_parameter_value(MI, 'Road Roughness Texture') == current_orm

LIB.update_material_instance(MI)
assert all(LIB.get_material_instance_texture_parameter_value(MI, name) == value
           for name, value in BEFORE_TEXTURES.items())
report = {'material': MAT.get_path_name(), 'instance': MI.get_path_name(),
          'standalone_roughness_detected': is_standalone, 'dirty_before': BEFORE_DIRTY,
          'existing_textures': {k: v.get_path_name() if v else None for k, v in BEFORE_TEXTURES.items()},
          'new_controls': ['Road Normal Strength', 'Road Roughness Strength',
                           'Road Roughness Offset', 'Road Roughness Texture', 'Road Use Separate Roughness']}
(OUT / 'authoring.json').write_text(json.dumps(report, indent=2))
print(json.dumps(report))
