"""Apply a selective depth guard to our delivery guide material only.

Run through Unreal editor Python or -ExecutePythonScript, with PIE stopped.
Pass --restore to restore the original opacity connection and depth-test flag.
The original emissive/color graph, the open map and every other asset are untouched.
Runtime code must reserve stencil 244 for the authored intake-mouth mesh and
enable the custom-depth stencil pass; this script does not change those settings.
"""
import json
import sys
from pathlib import Path

import unreal as u

ASSET = '/Game/Gameplay/Delivery/M_DeliveryZoneGuide'
PREFIX = 'MC Delivery: selective authored-mouth depth | '
BACKUP = Path(u.Paths.project_saved_dir()) / 'DeliveryGuideDepthPatch.json'
MODE = 'restore' if '--restore' in sys.argv else 'apply'
edit = u.MaterialEditingLibrary
lib = u.EditorAssetLibrary
material = lib.load_asset(ASSET)
assert isinstance(material, u.Material), 'The owned delivery guide material must exist'
assert material.get_editor_property('blend_mode') == u.BlendMode.BLEND_TRANSLUCENT
# The commandlet has no LevelEditor play-session context; querying it crashes UE.
if '-run=pythonscript' not in u.SystemLibrary.get_command_line().lower():
    level_editor = u.get_editor_subsystem(u.LevelEditorSubsystem)
    assert not level_editor or not level_editor.is_in_play_in_editor(), 'Stop PIE before authoring'
nodes = list(edit.get_material_expressions(material))
emissive_before = edit.get_material_property_input_node(material, u.MaterialProperty.MP_EMISSIVE_COLOR)
emissive_pin = edit.get_material_property_input_node_output_name(material, u.MaterialProperty.MP_EMISSIVE_COLOR)


def tagged(expression):
    return expression.get_editor_property('desc').startswith(PREFIX)


if BACKUP.exists():
    state = json.loads(BACKUP.read_text(encoding='utf-8'))
    assert state['asset'] == ASSET and state['version'] == 1, 'Unexpected backup ownership/version'
else:
    assert MODE == 'apply', 'The original-state backup is required to restore'
    assert not any(tagged(n) for n in nodes), 'Patched nodes exist without an original-state backup'
    original = edit.get_material_property_input_node(material, u.MaterialProperty.MP_OPACITY)
    assert original, 'Preserve the existing VertexAlpha * Opacity connection'
    state = dict(version=1, asset=ASSET, opacity_node=original.get_path_name(),
                 opacity_pin=edit.get_material_property_input_node_output_name(material, u.MaterialProperty.MP_OPACITY),
                 disable_depth_test=bool(material.get_editor_property('disable_depth_test')))
    BACKUP.parent.mkdir(parents=True, exist_ok=True)
    BACKUP.write_text(json.dumps(state, indent=2), encoding='utf-8')

original = next((n for n in nodes if n.get_path_name() == state['opacity_node']), None)
assert original and not tagged(original), 'The preserved opacity node must still belong to this material'

if MODE == 'restore':
    assert edit.connect_material_property(original, state['opacity_pin'] or '', u.MaterialProperty.MP_OPACITY)
    material.set_editor_property('disable_depth_test', state['disable_depth_test'])
    for expression in nodes:
        if tagged(expression):
            edit.delete_material_expression(material, expression)
else:
    def node(cls, name, x, y, **properties):
        result = next((n for n in nodes if n.get_editor_property('desc') == PREFIX + name), None)
        if result is None:
            result = edit.create_material_expression(material, cls, x, y)
            result.set_editor_property('desc', PREFIX + name)
            nodes.append(result)
        assert isinstance(result, cls), 'Unexpected expression type: ' + name
        for key, value in properties.items():
            result.set_editor_property(key, value)
        return result

    pixel = node(u.MaterialExpressionPixelDepth, 'guide pixel depth', 0, 180)
    scene = node(u.MaterialExpressionSceneDepth, 'opaque scene depth', 0, 300)
    custom_depth = node(u.MaterialExpressionSceneTexture, 'authored-mouth custom depth', 0, 420,
                        scene_texture_id=u.SceneTextureId.PPI_CUSTOM_DEPTH, filtered=False)
    stencil = node(u.MaterialExpressionSceneTexture, 'authored-mouth custom stencil', 0, 560,
                   scene_texture_id=u.SceneTextureId.PPI_CUSTOM_STENCIL, filtered=False)
    tolerance = node(u.MaterialExpressionScalarParameter, 'depth tolerance', 0, 700,
                     parameter_name='DeliveryDepthTolerance', default_value=2.0, group='Delivery | Depth guard')
    limit = node(u.MaterialExpressionScalarParameter, 'maximum decorative-mouth overlap', 0, 820,
                 parameter_name='DeliveryMouthOverlapLimit', default_value=180.0, group='Delivery | Depth guard')
    stencil_value = node(u.MaterialExpressionScalarParameter, 'reserved authored-mouth stencil', 0, 940,
                         parameter_name='DeliveryMouthStencil', default_value=244.0, group='Delivery | Depth guard')
    guard = node(u.MaterialExpressionCustom, 'visibility guard', 360, 420,
                 description=PREFIX + 'visibility guard', output_type=u.CustomMaterialOutputType.CMOT_FLOAT1)
    sources = {'PixelZ': (pixel, ''), 'SceneZ': (scene, ''), 'MouthDepth': (custom_depth, 'Color'),
               'Stencil': (stencil, 'Color'), 'Tolerance': (tolerance, ''),
               'MaxOverlap': (limit, ''), 'MouthStencil': (stencil_value, '')}
    inputs = []
    for name in sources:
        entry = u.CustomInput()
        entry.set_editor_property('input_name', name)
        inputs.append(entry)
    guard.set_editor_property('inputs', inputs)
    for name, (source, pin) in sources.items():
        assert edit.connect_material_expressions(source, pin, guard, name)
    guard.set_editor_property('code', r'''
float tolerance = max(Tolerance, 0.0);
float gap = PixelZ - SceneZ;
// Reproduce ordinary depth occlusion for all closer characters and props.
float unobstructed = (gap <= tolerance) ? 1.0 : 0.0;
// Only the named decorative mouth may hide a small part of the floor guide.
// A foreground hero/prop changes SceneZ, breaking the custom-depth equality.
float isMouth = (abs(Stencil.r - MouthStencil) < 0.5) ? 1.0 : 0.0;
float mouthIsVisible = (abs(SceneZ - MouthDepth.r) <= tolerance) ? 1.0 : 0.0;
float boundedOverlap = (gap >= 0.0 && gap <= max(MaxOverlap, 0.0)) ? 1.0 : 0.0;
return saturate(max(unobstructed, isMouth * mouthIsVisible * boundedOverlap));
''')
    opacity = node(u.MaterialExpressionMultiply, 'preserve original guide opacity', 720, 180)
    assert edit.connect_material_expressions(original, state['opacity_pin'] or '', opacity, 'A')
    assert edit.connect_material_expressions(guard, '', opacity, 'B')
    assert edit.connect_material_property(opacity, '', u.MaterialProperty.MP_OPACITY)
    material.set_editor_property('disable_depth_test', True)

assert edit.get_material_property_input_node(material, u.MaterialProperty.MP_EMISSIVE_COLOR) == emissive_before
assert edit.get_material_property_input_node_output_name(material, u.MaterialProperty.MP_EMISSIVE_COLOR) == emissive_pin
errors = edit.recompile_material(material)
assert not errors, 'Delivery guide shader compilation failed: ' + str(errors)
assert lib.save_loaded_asset(material, only_if_is_dirty=False), 'Could not save the owned delivery guide material'
u.log('MC_DELIVERY_GUIDE_DEPTH_' + MODE.upper() + ' ' + ASSET)
