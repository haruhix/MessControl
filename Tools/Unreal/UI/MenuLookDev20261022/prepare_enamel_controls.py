"""Expose clean enamel controls in the two PRIVATE menu enamel shaders only.

No authoring occurs on import. Root calls run('plan') first, then run('apply',
mode='inplace') while PIE is stopped. Inplace preserves the actor construction
MID/Sequencer route. mode='clone' is available for independent experiments, but
does not bind the clone to any actor and is not a replacement animation route.

Select the clean background parent with source_asset=BASE+'M_MenuCleanEnamel'.
No unbound instance is created unless create_instance=True is explicit.
Defaults reproduce the previous shader: Tint=(1,1,1), Roughness=.3225. No
geometry, WPO, papilla, mask, density, sweep, animation or material-output links
change. Other material parents and textures remain untouched.
"""
import hashlib
import json
from datetime import datetime, timezone
from pathlib import Path
import shutil

BASE = '/Game/UI/MenuShowcase/AnatomyV2/Materials/'
SOURCE = BASE + 'M_MenuCoffeeEnamel'
OUT = Path('E:/DEVGAME/MessControl/Artifacts/MenuLookDev_20261022/Authoring')

COLOR_LABEL = 'Warm natural enamel, clean ivory highlights'
ROUGH_LABEL = 'Subtle enamel polish variation'
OLD_COLOR = 'return lerp(float3(.61,.555,.455),float3(.73,.69,.585),N*.55+.25);'
OLD_ROUGH = 'return .295+N*.055;'
NEW_COLOR = 'return saturate(lerp(float3(.61,.555,.455),float3(.73,.69,.585),N*.55+.25)*CleanTint);'
NEW_ROUGH = 'return clamp(CleanRoughness+(N-.5)*.055,.06,.9);'


def _sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _snapshot(material, u):
    edit = u.MaterialEditingLibrary
    nodes = list(edit.get_material_expressions(material))
    customs = {}
    for node in nodes:
        if isinstance(node, u.MaterialExpressionCustom):
            customs[node.get_name()] = {
                'description': str(node.get_editor_property('description')),
                'code': str(node.get_editor_property('code')),
                'inputs': [entry.export_text() for entry in node.get_editor_property('inputs')],
            }
    outputs = {}
    for prop in (u.MaterialProperty.MP_BASE_COLOR, u.MaterialProperty.MP_ROUGHNESS,
                 u.MaterialProperty.MP_SPECULAR, u.MaterialProperty.MP_NORMAL,
                 u.MaterialProperty.MP_WORLD_POSITION_OFFSET):
        node = edit.get_material_property_input_node(material, prop)
        outputs[str(prop)] = node.get_name() if node else None
    parameters = {
        kind: sorted(str(name) for name in fn(material))
        for kind, fn in [('scalar', edit.get_scalar_parameter_names),
                         ('vector', edit.get_vector_parameter_names),
                         ('texture', edit.get_texture_parameter_names)]
    }
    return {'nodes': len(nodes), 'customs': customs, 'outputs': outputs,
            'parameters': parameters,
            'texture_samples': sum(isinstance(node, u.MaterialExpressionTextureSample) for node in nodes)}


def _targets(material, u):
    customs = [node for node in u.MaterialEditingLibrary.get_material_expressions(material)
               if isinstance(node, u.MaterialExpressionCustom)]
    color = [node for node in customs if str(node.get_editor_property('description')) == COLOR_LABEL]
    rough = [node for node in customs if str(node.get_editor_property('description')) == ROUGH_LABEL]
    assert len(color) == len(rough) == 1, 'Private enamel source changed; inspect before applying.'
    assert str(color[0].get_editor_property('code')).strip() in (OLD_COLOR, NEW_COLOR), 'Unexpected enamel color code.'
    assert str(rough[0].get_editor_property('code')).strip() in (OLD_ROUGH, NEW_ROUGH), 'Unexpected enamel roughness code.'
    return color[0], rough[0]


def run(action='plan', *, mode='inplace', source_asset=SOURCE, create_instance=False):
    import unreal as u
    assert action in ('plan', 'apply') and mode in ('inplace', 'clone')
    assert source_asset in (SOURCE, BASE + 'M_MenuCleanEnamel'), 'Only the two private enamel parents are permitted.'
    basename = source_asset.rsplit('/', 1)[1]
    clone_asset = BASE + basename + '_LookV7'
    instance_name = 'MI_' + basename.removeprefix('M_') + '_LookV7'
    instance_asset = BASE + instance_name
    report_prefix = basename + '_EnamelControls'
    lib, edit = u.EditorAssetLibrary, u.MaterialEditingLibrary
    source = lib.load_asset(source_asset)
    assert isinstance(source, u.Material), source_asset
    color, rough = _targets(source, u)
    source_before = _snapshot(source, u)
    OUT.mkdir(parents=True, exist_ok=True)
    report = {'source': source_asset, 'mode': mode, 'action': action,
              'controls': {'CleanEnamelTint': [1., 1., 1., 1.],
                           'CleanEnamelRoughness': .3225},
              'tint_semantics': 'Linear RGB multiplier over original warm enamel only; coffee pigment is preserved.',
              'roughness_semantics': 'Mean clean enamel roughness; retains previous .055 nonperiodic range.',
              'source_before': source_before,
              'geometry_animation_and_wpo_change': False,
              'no_actor_or_sequence_binding_change': True,
              'visual_status': 'pending_native_comparison'}
    if action == 'plan':
        (OUT / (report_prefix + 'Plan.json')).write_text(json.dumps(report, indent=2), encoding='utf8')
        return report

    assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor(), 'Preserve active PIE; root must stop preview before authoring.'
    world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
    assert world.get_path_name().startswith('/Game/UI/MenuShowcase/AnatomyV2/L_MenuAnatomyV2.'), world.get_path_name()
    source_file = Path(u.Paths.project_dir()).resolve() / (source_asset.replace('/Game/', 'Content/', 1) + '.uasset')
    assert source_file.is_file()
    source_hash = _sha(source_file)
    backup = OUT / 'Backup' / datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ')
    backup.mkdir(parents=True, exist_ok=False)
    shutil.copy2(source_file, backup / source_file.name)
    assert _sha(backup / source_file.name) == source_hash
    report['source_package_sha256_before'] = source_hash
    report['backup'] = str(backup / source_file.name)

    if mode == 'clone':
        target = lib.load_asset(clone_asset)
        if target is None:
            target = lib.duplicate_asset(source_asset, clone_asset)
        assert isinstance(target, u.Material), clone_asset
    else:
        target = source
    color, rough = _targets(target, u)
    before = _snapshot(target, u)
    modified_names = {color.get_name(), rough.get_name()}

    def parameter(cls, name, value):
        matches = [node for node in edit.get_material_expressions(target)
                   if isinstance(node, cls) and str(node.get_editor_property('parameter_name')) == name]
        assert len(matches) <= 1, name
        if matches:
            return matches[0]
        node = edit.create_material_expression(target, cls)
        node.set_editor_property('parameter_name', name)
        node.set_editor_property('default_value', value)
        node.set_editor_property('group', 'Menu Look V7 - Clean Enamel')
        return node

    tint = parameter(u.MaterialExpressionVectorParameter, 'CleanEnamelTint', u.LinearColor(1., 1., 1., 1.))
    roughness = parameter(u.MaterialExpressionScalarParameter, 'CleanEnamelRoughness', .3225)

    def append_input(node, name, value, pin):
        inputs = list(node.get_editor_property('inputs'))
        if name not in [str(entry.get_editor_property('input_name')) for entry in inputs]:
            entry = u.CustomInput()
            entry.set_editor_property('input_name', name)
            inputs.append(entry)
            node.set_editor_property('inputs', inputs)
        assert edit.connect_material_expressions(value, pin, node, name)

    append_input(color, 'CleanTint', tint, 'RGB')
    append_input(rough, 'CleanRoughness', roughness, '')
    color.set_editor_property('code', NEW_COLOR)
    rough.set_editor_property('code', NEW_ROUGH)
    after = _snapshot(target, u)
    assert before['outputs'] == after['outputs'], 'Material-output connections changed.'
    assert before['texture_samples'] == after['texture_samples'], 'Texture sample count changed.'
    for name, data in before['customs'].items():
        if name not in modified_names:
            assert after['customs'][name] == data, ('Protected Custom expression changed', name)
    for kind, names in before['parameters'].items():
        assert set(names) <= set(after['parameters'][kind]), ('Existing parameter lost', kind)
    for node in (color, rough):
        old = before['customs'][node.get_name()]['inputs']
        new = after['customs'][node.get_name()]['inputs']
        assert new[:len(old)] == old, 'Existing Custom input connections changed.'
    assert after['nodes'] <= before['nodes'] + 2
    errors = list(edit.recompile_material(target))
    assert not errors, errors
    assert lib.save_loaded_asset(target, only_if_is_dirty=False)

    instance = None
    if create_instance:
        instance = lib.load_asset(instance_asset)
        if instance is None:
            instance = u.AssetToolsHelpers.get_asset_tools().create_asset(
                instance_name, BASE.rstrip('/'),
                u.MaterialInstanceConstant, u.MaterialInstanceConstantFactoryNew())
        assert isinstance(instance, u.MaterialInstanceConstant)
        edit.set_material_instance_parent(instance, target)
        edit.set_material_instance_vector_parameter_value(instance, 'CleanEnamelTint', u.LinearColor(1., 1., 1., 1.))
        edit.set_material_instance_scalar_parameter_value(instance, 'CleanEnamelRoughness', .3225)
        assert lib.save_loaded_asset(instance, only_if_is_dirty=False)
    if mode == 'clone':
        assert _snapshot(source, u) == source_before
        assert _sha(source_file) == source_hash, 'Source package changed during clone preparation.'
    report.update(status='compiled_controls_saved_no_binding_change', material=target.get_path_name(),
                  instance=instance.get_path_name() if instance else None, instance_bound_to_actor=False,
                  source_package_sha256_after=_sha(source_file), after=after, errors=errors)
    (OUT / (report_prefix + 'Prepared.json')).write_text(json.dumps(report, indent=2), encoding='utf8')
    if source_asset == SOURCE:
        # Keep the first root-side wrapper's expected summary working.
        (OUT / 'EnamelControlsPrepared.json').write_text(json.dumps(report, indent=2), encoding='utf8')
    u.log('MENU_ENAMEL_CONTROLS_PREPARED ' + target.get_path_name())
    return report
