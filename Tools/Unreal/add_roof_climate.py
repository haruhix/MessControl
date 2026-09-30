"""Add the shared cold event to the artist roof through a separate material.

Run in Unreal after merging the artist arena. The artist material, instance,
textures and mesh stay authored assets; only the level's roof slot changes.
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
source_instance_path = '/Game/Art/Materials/Arena/MI_Roof'
roof_mesh_path = '/Game/FromBlender3/SM_Plane_001.SM_Plane_001'
color_marker = 'Mouth climate frost color'
rough_marker = 'Mouth climate rough crystalline finish'
hash_tag = 'MC.RoofClimate.SourceSHA256'

assert not u.EditorLevelLibrary.get_pie_worlds(False), 'Stop PIE before authoring'
source_instance = lib.load_asset(source_instance_path)
assert isinstance(source_instance, u.MaterialInstanceConstant), source_instance_path
mpc = lib.load_asset(folder + '/MPC_MouthClimate')
assert isinstance(mpc, u.MaterialParameterCollection), 'Missing shared mouth climate'
assert 'ColdAmount' in [str(name) for name in mpc.get_scalar_parameter_names()]


def package_path(asset):
    return asset.get_path_name().split('.')[0]


def source_hash(asset):
    path = package_path(asset)
    assert path.startswith('/Game/'), 'Expected a saved project material: ' + path
    filename = root / 'Content' / (path[len('/Game/'):] + '.uasset')
    assert filename.is_file(), filename
    return hashlib.sha256(filename.read_bytes()).hexdigest()


def save(asset):
    assert lib.save_loaded_asset(asset, only_if_is_dirty=False), asset.get_path_name()


def clone(source, path):
    digest = source_hash(source)
    if lib.does_asset_exist(path):
        result = lib.load_asset(path)
        assert lib.get_metadata_tag(result, hash_tag) == digest, (
            'Roof climate copy predates its artist source; refresh the derived '
            'copy before rerunning: ' + path)
    else:
        result = lib.duplicate_asset(package_path(source), path)
        assert result, 'Could not duplicate ' + package_path(source)
        lib.set_metadata_tag(result, hash_tag, digest)
        lib.set_metadata_tag(result, 'MC.RoofClimate.Source', package_path(source))
    return result


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


# Retain any intermediate artist instances, including their static parameters.
chain = []
parent = source_instance
while isinstance(parent, u.MaterialInstance):
    assert parent not in chain, 'Cyclic roof material parent chain'
    chain.append(parent)
    parent = parent.get_editor_property('parent')
assert isinstance(parent, u.Material), 'Roof has no material parent'
source_material = parent
modified = []

if has_climate(source_material):
    roof_instance = source_instance
    climate_material = source_material
else:
    climate_material = clone(source_material, folder + '/M_RoofClimate')
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
    assert not edit.recompile_material(climate_material), 'Roof climate compile failed'
    save(climate_material)
    modified.append(climate_material.get_path_name())

    derived_parent = climate_material
    for index in range(len(chain) - 1, -1, -1):
        name = 'MI_RoofClimate' if index == 0 else 'MI_RoofClimateParent_' + str(index)
        instance = clone(chain[index], folder + '/' + name)
        edit.set_material_instance_parent(instance, derived_parent)
        edit.update_material_instance(instance)
        save(instance)
        modified.append(instance.get_path_name())
        derived_parent = instance
    roof_instance = derived_parent

assignments = []
for actor in actors.get_all_level_actors():
    for component in actor.get_components_by_class(u.StaticMeshComponent):
        mesh = component.get_editor_property('static_mesh')
        if not mesh or mesh.get_path_name() != roof_mesh_path:
            continue
        for slot in range(component.get_num_materials()):
            current = component.get_material(slot)
            if current not in (source_instance, roof_instance):
                continue
            component.set_material(slot, roof_instance)
            assignments.append(dict(actor=actor.get_actor_label(), slot=slot,
                                    material=roof_instance.get_path_name()))
assert assignments, 'No merged roof slot uses the artist MI_Roof'
assert u.get_editor_subsystem(u.LevelEditorSubsystem).save_current_level()

report = dict(sourceInstance=source_instance.get_path_name(),
              sourceMaterial=source_material.get_path_name(),
              climateInstance=roof_instance.get_path_name(),
              climateMaterial=climate_material.get_path_name(),
              profile=str(climate_material.get_editor_property('subsurface_profile')),
              collection=mpc.get_path_name(), scalar='ColdAmount',
              modified=modified, assignments=assignments,
              unfrozenBehavior='Original BaseColor and Roughness at ColdAmount=0')
(root / 'Saved/RoofClimateBuild.json').write_text(
    json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
u.log('MC_ROOF_CLIMATE_READY ' + json.dumps(report, ensure_ascii=False))
