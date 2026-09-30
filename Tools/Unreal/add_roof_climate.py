"""Add the shared cold event to the artist roof and active mouth material.

Run in Unreal after merging the artist arena. The artist material, instance,
textures and meshes stay authored assets; only the level's material slots change.
"""
import hashlib
import json
from pathlib import Path

import unreal as u

lib = u.EditorAssetLibrary
edit = u.MaterialEditingLibrary
actors = u.get_editor_subsystem(u.EditorActorSubsystem)
root = Path(u.Paths.project_dir()).resolve()
folder = '/Game/Gameplay/Cold'
source_instance_paths = {
    '/Game/Art/Materials/Arena/MI_Roof',
    '/Game/Art/Materials/Arena/MI_Roof_0',
}
exit_material_path = '/Game/Art/Materials/Arena/M_Exit'
exit_mesh_path = '/Game/FromBlender4/SK_Exit.SK_Exit'
roof_mesh_path = '/Game/FromBlender3/SM_Plane_001.SM_Plane_001'
color_marker = 'Mouth climate frost color'
rough_marker = 'Mouth climate rough crystalline finish'
hash_tag = 'MC.RoofClimate.SourceSHA256'
source_tag = 'MC.RoofClimate.Source'

assert not u.EditorLevelLibrary.get_pie_worlds(False), 'Stop PIE before authoring'
mpc = lib.load_asset(folder + '/MPC_MouthClimate')
assert isinstance(mpc, u.MaterialParameterCollection), 'Missing shared mouth climate'
assert 'ColdAmount' in [str(name) for name in mpc.get_scalar_parameter_names()]


def package_path(asset):
    return asset.get_path_name().split('.')[0]


def canonical_asset(path):
    # Native load_object follows artist redirectors; EditorAssetLibrary can
    # return the redirector object itself after a package rename.
    result = u.load_asset(path)
    assert isinstance(result, (u.Material, u.MaterialInstanceConstant)), path
    return result


canonical_roof_sources = {
    package_path(canonical_asset(path)): canonical_asset(path)
    for path in source_instance_paths
}
canonical_exit_source = canonical_asset(exit_material_path)
canonical_exit_path = package_path(canonical_exit_source)


def source_hash(asset):
    path = package_path(asset)
    assert path.startswith('/Game/'), 'Expected a saved project material: ' + path
    filename = root / 'Content' / (path[len('/Game/'):] + '.uasset')
    assert filename.is_file(), filename
    return hashlib.sha256(filename.read_bytes()).hexdigest()


def save(asset):
    assert lib.save_loaded_asset(asset, only_if_is_dirty=False), asset.get_path_name()


def clone(source, path, expected_parent=None):
    digest = source_hash(source)
    original_path = package_path(source)

    def matches(candidate):
        return (lib.get_metadata_tag(candidate, hash_tag) == digest
                and lib.get_metadata_tag(candidate, source_tag) == original_path
                and (expected_parent is None
                     or candidate.get_editor_property('parent') == expected_parent))

    if lib.does_asset_exist(path):
        result = lib.load_asset(path)
        if matches(result):
            return result, False
        # Keep old copies and their references intact. A different parent also
        # needs a fresh instance, even when the child artist package is unchanged.
        identity = original_path + ':' + digest
        if expected_parent is not None:
            identity += ':' + package_path(expected_parent)
        version = hashlib.sha256(identity.encode('utf-8')).hexdigest()
        path += '_' + version[:12]
        if lib.does_asset_exist(path):
            result = lib.load_asset(path)
            assert matches(result), 'Unexpected derived material collision: ' + path
            return result, False
    result = lib.duplicate_asset(original_path, path)
    assert result, 'Could not duplicate ' + original_path
    lib.set_metadata_tag(result, hash_tag, digest)
    lib.set_metadata_tag(result, source_tag, original_path)
    return result, True


def node(material, cls, **properties):
    result = edit.create_material_expression(material, cls)
    for name, value in properties.items():
        result.set_editor_property(name, value)
    return result


def custom(material, description, code, inputs, scalar=False):
    result = node(material, u.MaterialExpressionCustom,
                  description=description, code=code,
                  output_type=(u.CustomMaterialOutputType.CMOT_FLOAT1 if scalar
                               else u.CustomMaterialOutputType.CMOT_FLOAT3))
    entries = []
    for name in inputs:
        entry = u.CustomInput()
        entry.set_editor_property('input_name', name)
        entries.append(entry)
    result.set_editor_property('inputs', entries)
    for name, (expression, pin) in inputs.items():
        assert edit.connect_material_expressions(expression, pin, result, name)
    return result


def output(material, prop):
    expression = edit.get_material_property_input_node(material, prop)
    pin = edit.get_material_property_input_node_output_name(material, prop)
    return expression, pin if isinstance(pin, str) else ''


def has_climate(material):
    expressions = edit.get_material_expressions(material)
    markers = {str(n.get_editor_property('description')) for n in expressions
               if isinstance(n, u.MaterialExpressionCustom)}
    shared_input = any(
        isinstance(n, u.MaterialExpressionCollectionParameter)
        and n.get_editor_property('collection') == mpc
        and str(n.get_editor_property('parameter_name')) == 'ColdAmount'
        for n in expressions)
    if not shared_input and color_marker not in markers and rough_marker not in markers:
        return False
    color, _ = output(material, u.MaterialProperty.MP_BASE_COLOR)
    rough, _ = output(material, u.MaterialProperty.MP_ROUGHNESS)
    assert shared_input and color_marker in markers and rough_marker in markers, (
        'Partial roof climate graph needs inspection before authoring')
    assert (isinstance(color, u.MaterialExpressionCustom)
            and color.get_editor_property('description') == color_marker
            and isinstance(rough, u.MaterialExpressionCustom)
            and rough.get_editor_property('description') == rough_marker), (
        'Existing climate controls are not the final roof outputs')
    return True


# Find each authored material before making derived assets. Metadata maps
# old climate assignments back to their current artist source after a merge.
slots = []
for actor in actors.get_all_level_actors():
    for component in actor.get_components_by_class(u.MeshComponent):
        allowed = set()
        if isinstance(component, u.StaticMeshComponent):
            mesh = component.get_editor_property('static_mesh')
            if mesh and mesh.get_path_name() == roof_mesh_path:
                allowed = set(canonical_roof_sources)
        elif (isinstance(component, u.SkeletalMeshComponent)
              and isinstance(actor, u.MCThroat)
              and component.get_name() == 'AuthoredMouth'):
            mesh = component.get_editor_property('skeletal_mesh_asset')
            if mesh and mesh.get_path_name() == exit_mesh_path:
                allowed = {canonical_exit_path}
        if not allowed:
            continue
        for slot in range(component.get_num_materials()):
            current = component.get_material(slot)
            if not current:
                continue
            path = package_path(current)
            if path not in allowed:
                original = lib.get_metadata_tag(current, source_tag)
                if not original:
                    continue
                source = canonical_asset(original)
                path = package_path(source)
            if path not in allowed:
                continue
            source = canonical_asset(path)
            slots.append((actor, component, slot, source))
assert any(package_path(source) in canonical_roof_sources for _, _, _, source in slots), (
    'No merged roof slot uses MI_Roof, MI_Roof_0 or their climate copies')

modified = []
variants = {}


def climate_variant(source_asset):
    # Retain any intermediate artist instances, including static parameters.
    chain = []
    parent = source_asset
    while isinstance(parent, u.MaterialInstance):
        assert parent not in chain, 'Cyclic roof material parent chain'
        chain.append(parent)
        parent = parent.get_editor_property('parent')
    assert isinstance(parent, u.Material), 'Roof has no material parent'
    source_material = parent

    if has_climate(source_material):
        return source_asset, source_material, source_material

    active_exit = package_path(source_asset) == canonical_exit_path
    # M_Exit may itself be an instance of the same artist master as MI_Roof_0.
    # Share that unchanged shader, while preserving each instance's overrides.
    material_name = 'M_ExitClimate' if active_exit and not chain else 'M_RoofClimate'
    climate_material, material_created = clone(source_material, folder + '/' + material_name)
    if not has_climate(climate_material):
        base, base_pin = output(climate_material, u.MaterialProperty.MP_BASE_COLOR)
        rough, rough_pin = output(climate_material, u.MaterialProperty.MP_ROUGHNESS)
        assert base and rough, (
            'Expected explicit artist BaseColor and Roughness outputs; '
            'constant or material-attributes inputs need a separate migration')
        cold = node(climate_material, u.MaterialExpressionCollectionParameter,
                    collection=mpc, parameter_name='ColdAmount')
        position = node(climate_material, u.MaterialExpressionWorldPosition)
        normal = node(climate_material, u.MaterialExpressionVertexNormalWS)
        frost = custom(climate_material, 'Mouth climate frost crystal coverage', r'''
float grains=frac(sin(dot(floor(P*.9),float3(12.9898,78.233,45.164)))*43758.5453);
float wisps=.5+.5*sin(P.x*.021+sin(P.y*.018)*2+P.z*.015);
float coat=saturate(.38+.50*saturate(N.z)+.28*wisps+.12*grains);
return saturate(Cold)*coat;
''', {'P': (position, ''), 'N': (normal, ''), 'Cold': (cold, '')}, scalar=True)
        color = custom(climate_material, color_marker,
                       'return lerp(Base,float3(.65,.82,.93),Frost*.84);',
                       {'Base': (base, base_pin), 'Frost': (frost, '')})
        finish = custom(climate_material, rough_marker,
                        'return lerp(Rough,.58,Frost);',
                        {'Rough': (rough, rough_pin), 'Frost': (frost, '')}, scalar=True)
        assert edit.connect_material_property(color, '', u.MaterialProperty.MP_BASE_COLOR)
        assert edit.connect_material_property(finish, '', u.MaterialProperty.MP_ROUGHNESS)
        assert has_climate(climate_material)
    usage_changed = False
    if active_exit:
        for flag, usage in [('used_with_skeletal_mesh', u.MaterialUsage.MATUSAGE_SKELETAL_MESH),
                            ('used_with_morph_targets', u.MaterialUsage.MATUSAGE_MORPH_TARGETS)]:
            if not climate_material.get_editor_property(flag):
                edit.set_base_material_usage(climate_material, usage)
                usage_changed = True
    if material_created or usage_changed:
        assert not edit.recompile_material(climate_material), 'Roof climate compile failed'
        save(climate_material)
        modified.append(climate_material.get_path_name())

    derived_parent = climate_material
    # Separate instance namespaces keep the author's two roof looks independent.
    namespace = source_asset.get_name() + 'Climate'
    for index in range(len(chain) - 1, -1, -1):
        name = namespace if index == 0 else namespace + 'Parent_' + str(index)
        instance, created = clone(chain[index], folder + '/' + name,
                                  expected_parent=derived_parent)
        if created:
            edit.set_material_instance_parent(instance, derived_parent)
            edit.update_material_instance(instance)
            save(instance)
            modified.append(instance.get_path_name())
        derived_parent = instance
    return derived_parent, source_material, climate_material


for _, _, _, source in slots:
    path = package_path(source)
    if path in variants:
        continue
    instance, material, climate_material = climate_variant(source)
    variants[path] = dict(source=source, instance=instance,
                          material=material, climate_material=climate_material)

assignments = []
for actor, component, slot, source in slots:
    roof_instance = variants[package_path(source)]['instance']
    component.set_material(slot, roof_instance)
    assignments.append(dict(actor=actor.get_actor_label(), slot=slot,
                            source=source.get_path_name(),
                            material=roof_instance.get_path_name()))
assert u.get_editor_subsystem(u.LevelEditorSubsystem).save_current_level()

details = [dict(sourceInstance=v['source'].get_path_name(),
                sourceSHA256=source_hash(v['source']),
                sourceMaterial=v['material'].get_path_name(),
                climateInstance=v['instance'].get_path_name(),
                climateMaterial=v['climate_material'].get_path_name(),
                profile=str(v['climate_material'].get_editor_property('subsurface_profile')))
           for v in variants.values()]
report = dict(variants=details, collection=mpc.get_path_name(), scalar='ColdAmount',
              modified=modified, assignments=assignments,
              unfrozenBehavior='Original BaseColor and Roughness at ColdAmount=0')
if len(details) == 1:
    report.update(details[0])
(root / 'Saved/RoofClimateBuild.json').write_text(
    json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
u.log('MC_ROOF_CLIMATE_READY ' + json.dumps(report, ensure_ascii=False))
