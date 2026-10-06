"""Animate rounded delivery arrows in their existing guarded guide material.

Run through Unreal editor Python or -ExecutePythonScript, with PIE stopped.
Only /Game/Gameplay/Delivery/M_DeliveryZoneGuide is compiled and saved. Re-running
updates the same tagged nodes, without wrapping opacity more than once. Pass
--restore to remove this patch and reconnect the captured opacity output.
If Saved's backup is missing in a fresh checkout, the validated owned wrapper
provides the original connection without relying on expression object names.

The runtime arrow mesh supplies stable UV0 coordinates in the original 2.4x
chevron template, sets ArrowMaskEnabled=1, and updates ArrowCycle from server
time. ArrowTravelDistance is 90 on native caps and 0 on flat replacement maps.
Floor material instances keep ArrowMaskEnabled=0 and retain their vertex alpha.
"""
import json
import sys
from pathlib import Path

import unreal as u

ASSET = '/Game/Gameplay/Delivery/M_DeliveryZoneGuide'
PREFIX = 'MC Delivery: shader animated arrows | '
DEPTH_PREFIX = 'MC Delivery: selective authored-mouth depth | '
BACKUP = Path(u.Paths.project_saved_dir()) / 'DeliveryGuideArrowAnimationPatch.json'
MODE = 'restore' if '--restore' in sys.argv else 'apply'
edit = u.MaterialEditingLibrary
lib = u.EditorAssetLibrary
material = lib.load_asset(ASSET)
assert isinstance(material, u.Material), 'The owned delivery guide material must exist'
assert material.get_editor_property('blend_mode') == u.BlendMode.BLEND_TRANSLUCENT
if '-run=pythonscript' not in u.SystemLibrary.get_command_line().lower():
    level_editor = u.get_editor_subsystem(u.LevelEditorSubsystem)
    assert not level_editor or not level_editor.is_in_play_in_editor(), 'Stop PIE before authoring'

nodes = list(edit.get_material_expressions(material))
emissive_before = edit.get_material_property_input_node(material, u.MaterialProperty.MP_EMISSIVE_COLOR)
emissive_pin = edit.get_material_property_input_node_output_name(material, u.MaterialProperty.MP_EMISSIVE_COLOR)
depth_test_before = bool(material.get_editor_property('disable_depth_test'))


def tagged(expression):
    return expression.get_editor_property('desc').startswith(PREFIX)


def upstream(expression):
    pending = [expression]
    seen = set()
    while pending:
        current = pending.pop()
        if not current or current.get_path_name() in seen:
            continue
        seen.add(current.get_path_name())
        pending.extend(edit.get_inputs_for_material_expression(material, current))
    return seen


guards = [n for n in nodes if n.get_editor_property('desc') == DEPTH_PREFIX + 'visibility guard']
assert len(guards) == 1, 'The saved selective authored-mouth depth guard must remain in place'
guard = guards[0]
guard_code_before = guard.get_editor_property('code')
current_opacity = edit.get_material_property_input_node(material, u.MaterialProperty.MP_OPACITY)
wrappers = [n for n in nodes if n.get_editor_property('desc') == PREFIX + 'preserve guarded guide opacity']
assert len(wrappers) <= 1, 'Duplicate owned arrow-opacity wrappers'

if BACKUP.exists():
    state = json.loads(BACKUP.read_text(encoding='utf-8'))
    assert state['asset'] == ASSET and state['version'] == 1, 'Unexpected arrow-patch backup ownership/version'
else:
    if wrappers:
        wrapper = wrappers[0]
        assert isinstance(wrapper, u.MaterialExpressionMultiply), 'Unexpected owned arrow-opacity wrapper type'
        assert current_opacity == wrapper, 'Opacity was changed outside this patch; inspect it before recovery'
        names = edit.get_material_expression_input_names(wrapper)
        inputs = edit.get_inputs_for_material_expression(material, wrapper)
        assert len(names) == len(inputs), 'Cannot identify owned arrow-wrapper inputs'
        connections = dict(zip(names, inputs))
        captured = connections.get('A')
        mask = connections.get('B')
        assert captured and isinstance(captured, u.MaterialExpressionMultiply) and not tagged(captured), 'Owned wrapper A must be the original scalar guarded opacity'
        assert mask and isinstance(mask, u.MaterialExpressionCustom) and mask.get_editor_property('desc') == PREFIX + 'rounded chevron mask', 'Owned wrapper B must be its arrow mask'
        assert guard.get_path_name() in upstream(captured), 'Recovered original opacity lost its selective depth guard'
        # The captured scalar Multiply has one unnamed output, so its original
        # pin is unambiguous even when the ignored Saved backup is absent.
        state = dict(version=1, asset=ASSET, opacity_node=captured.get_path_name(), opacity_pin='')
        u.log('MC_DELIVERY_ARROW_ANIMATION_BACKUP_RECOVERED ' + ASSET)
    else:
        assert MODE == 'apply', 'A patched material or original-state backup is required to restore'
        assert not any(tagged(n) for n in nodes), 'Incomplete arrow patch nodes exist without their original-state backup'
        assert current_opacity and guard.get_path_name() in upstream(current_opacity), 'Preserve the existing guarded opacity output'
        state = dict(version=1, asset=ASSET, opacity_node=current_opacity.get_path_name(),
                     opacity_pin=edit.get_material_property_input_node_output_name(material, u.MaterialProperty.MP_OPACITY))
    BACKUP.parent.mkdir(parents=True, exist_ok=True)
    BACKUP.write_text(json.dumps(state, indent=2), encoding='utf-8')

original = next((n for n in nodes if n.get_path_name() == state['opacity_node']), None)
assert original and not tagged(original), 'The preserved opacity node must still belong to this material'
assert guard.get_path_name() in upstream(original), 'The preserved opacity output must still include its depth guard'

assert current_opacity == original or (wrappers and current_opacity == wrappers[0]), 'Opacity was changed outside this patch; inspect it before applying'
if wrappers:
    assert original in edit.get_inputs_for_material_expression(material, wrappers[0]), 'Owned arrow wrapper lost the captured opacity connection'

if MODE == 'restore':
    assert edit.connect_material_property(original, state['opacity_pin'] or '', u.MaterialProperty.MP_OPACITY)
    for expression in nodes:
        if tagged(expression):
            edit.delete_material_expression(material, expression)
else:
    for name in ('ArrowMaskEnabled', 'ArrowCycle', 'ArrowTravelDistance'):
        collisions = [n for n in nodes if isinstance(n, u.MaterialExpressionScalarParameter)
                      and str(n.get_editor_property('parameter_name')) == name and not tagged(n)]
        assert not collisions, 'An unrelated material parameter already owns ' + name

    def node(cls, name, x, y, **properties):
        matches = [n for n in nodes if n.get_editor_property('desc') == PREFIX + name]
        assert len(matches) <= 1, 'Duplicate owned arrow node: ' + name
        result = matches[0] if matches else None
        if result is None:
            result = edit.create_material_expression(material, cls, x, y)
            result.set_editor_property('desc', PREFIX + name)
            nodes.append(result)
        assert isinstance(result, cls), 'Unexpected expression type: ' + name
        for key, value in properties.items():
            result.set_editor_property(key, value)
        return result

    uv = node(u.MaterialExpressionTextureCoordinate, 'stable chevron coordinates', 0, 1120, coordinate_index=0)
    enabled = node(u.MaterialExpressionScalarParameter, 'arrow-only mask switch', 0, 1260,
                   parameter_name='ArrowMaskEnabled', default_value=0.0, group='Delivery | Arrows')
    cycle = node(u.MaterialExpressionScalarParameter, 'synchronized cycle', 0, 1400,
                 parameter_name='ArrowCycle', default_value=0.0, group='Delivery | Arrows')
    travel = node(u.MaterialExpressionScalarParameter, 'native-cap travel distance', 0, 1540,
                  parameter_name='ArrowTravelDistance', default_value=90.0, group='Delivery | Arrows')
    mask = node(u.MaterialExpressionCustom, 'rounded chevron mask', 400, 1260,
                description=PREFIX + 'rounded chevron mask', output_type=u.CustomMaterialOutputType.CMOT_FLOAT1)
    sources = {'UV': (uv, ''), 'Cycle': (cycle, ''), 'ArrowMaskEnabled': (enabled, ''), 'ArrowTravelDistance': (travel, '')}
    inputs = []
    for name in sources:
        entry = u.CustomInput()
        entry.set_editor_property('input_name', name)
        inputs.append(entry)
    mask.set_editor_property('inputs', inputs)
    for name, (source, pin) in sources.items():
        assert edit.connect_material_expressions(source, pin, mask, name)
    mask.set_editor_property('code', r'''
// UV0 is measured from a fixed row centre in the original 2.4x template.
float2 p = UV;
p.x -= (Cycle - 0.5) * ArrowTravelDistance / 2.4;
float alpha = 0.0;
[unroll]
for (int pair = 0; pair < 2; ++pair)
{
    // abs(y) mirrors the two arms. Their rounded endpoints follow the old
    // (-22,-33), (17,0), (-22,33) shape; pair spacing is 96 / 2.4.
    float2 q = float2(p.x + 40.0 * pair, abs(p.y));
    float2 v = float2(-39.0, 33.0);
    float2 w = q - float2(17.0, 0.0);
    float d = length(w - v * saturate(dot(w, v) / 2610.0));
    float aa = max(fwidth(d), 0.08);
    float core = 1.0 - smoothstep(7.0 - aa, 7.0 + aa, d);
    float halo = 0.32 * saturate((12.0 - d) / 5.0);
    alpha = max(alpha, lerp(halo, 0.96, core));
}
// The floor's default 0 bypass preserves its existing vertex opacity exactly.
return lerp(1.0, alpha, saturate(ArrowMaskEnabled));
''')
    opacity = node(u.MaterialExpressionMultiply, 'preserve guarded guide opacity', 900, 180)
    assert edit.connect_material_expressions(original, state['opacity_pin'] or '', opacity, 'A')
    assert edit.connect_material_expressions(mask, '', opacity, 'B')
    assert edit.connect_material_property(opacity, '', u.MaterialProperty.MP_OPACITY)

assert edit.get_material_property_input_node(material, u.MaterialProperty.MP_EMISSIVE_COLOR) == emissive_before
assert edit.get_material_property_input_node_output_name(material, u.MaterialProperty.MP_EMISSIVE_COLOR) == emissive_pin
assert bool(material.get_editor_property('disable_depth_test')) == depth_test_before
assert guard.get_editor_property('code') == guard_code_before
errors = edit.recompile_material(material)
assert not errors, 'Delivery arrow shader compilation failed: ' + str(errors)
assert lib.save_loaded_asset(material, only_if_is_dirty=False), 'Could not save the owned delivery guide material'
u.log('MC_DELIVERY_ARROW_ANIMATION_' + MODE.upper() + ' ' + ASSET)
