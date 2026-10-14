"""Derive storm-only liquid graphs without editing source profiles/materials.

Run in idle MessControl Editor. All writes remain in /Game/Cataclysm/Storm.
Macro constants come from MCCataclysmStormMath.h; copied liquid hierarchy keeps
its coffee/cola identity, micro ripple WPO, optical textures and source overrides.
"""
import hashlib
import json
import re
from pathlib import Path
import unreal as u

ROOT = Path(u.Paths.project_dir()).resolve()
FOLDER = '/Game/Cataclysm/Storm'
OWNER = 'MCCataclysmStormSource'
VERSION = 'MCCataclysmStormGraphVersion'
lib, edit = u.EditorAssetLibrary, u.MaterialEditingLibrary
tools = u.AssetToolsHelpers.get_asset_tools()
HEADER = ROOT / 'Source/MessControl/Private/MCCataclysmStormMath.h'
CONSTANTS = {k: float(v) for k, v in re.findall(r'constexpr float\s+(\w+)\s*=\s*([.\d]+)f;', HEADER.read_text(encoding='utf-8'))}
require_names = ('PrimaryLength', 'SecondaryLength', 'PrimaryAmplitude', 'SecondaryAmplitude',
                 'HarmonicAmplitude', 'PrimarySpeed', 'SecondarySpeed', 'SecondaryCos', 'SecondarySin')
if not all(k in CONSTANTS for k in require_names):
    raise RuntimeError('Storm native constants changed; review the shared wave contract')


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def package(obj):
    return obj.get_path_name().split('.')[0]


def save(obj):
    require(package(obj).startswith(FOLDER + '/'), 'Refusing an unrelated package save')
    require(lib.save_loaded_asset(obj, only_if_is_dirty=False), 'Storm package save failed')


def node(mat, cls, x, y, **values):
    result = edit.create_material_expression(mat, cls, x, y)
    require(result, 'Could not create storm expression')
    if values:
        result.set_editor_properties(values)
    return result


def connect(src, pin, dst, name):
    require(edit.connect_material_expressions(src, pin, dst, name), 'Storm graph link failed: ' + name)


def output(src, prop):
    require(edit.connect_material_property(src, '', prop), 'Storm output link failed: ' + str(prop))


def custom(mat, description, code, inputs, dimension, x, y):
    result = node(mat, u.MaterialExpressionCustom, x, y, description=description, code=code,
        output_type={1: u.CustomMaterialOutputType.CMOT_FLOAT1, 2: u.CustomMaterialOutputType.CMOT_FLOAT2,
                     3: u.CustomMaterialOutputType.CMOT_FLOAT3, 4: u.CustomMaterialOutputType.CMOT_FLOAT4}[dimension])
    entries = []
    for key in inputs:
        entry = u.CustomInput()
        entry.set_editor_property('input_name', key)
        entries.append(entry)
    result.set_editor_property('inputs', entries)
    for name, (source, pin) in inputs.items():
        connect(source, pin, result, name)
    return result


def parameter(mat, name, value, vector=False):
    cls = u.MaterialExpressionVectorParameter if vector else u.MaterialExpressionScalarParameter
    existing = [n for n in edit.get_material_expressions(mat)
                if isinstance(n, cls) and str(n.get_editor_property('parameter_name')) == name]
    require(len(existing) <= 1, 'Duplicate material parameter: ' + name)
    if existing:
        return existing[0]
    result = node(mat, cls, -1900, 1100, parameter_name=name, group='Cataclysm Storm',
                  default_value=u.LinearColor(*value) if vector else value)
    return result


def source_input(mat, prop):
    expression = edit.get_material_property_input_node(mat, prop)
    pin = edit.get_material_property_input_node_output_name(mat, prop) if expression else ''
    return expression, pin


def wave_code():
    c = CONSTANTS
    return f'''float2 q=P.xy-Center.xy;
float2 d=Direction.xy;
float2 e=float2(d.x*{c['SecondaryCos']:.9g}-d.y*{c['SecondarySin']:.9g},d.x*{c['SecondarySin']:.9g}+d.y*{c['SecondaryCos']:.9g});
float a=dot(q,d)*(6.28318530718/{c['PrimaryLength']:.9g})-Time*{c['PrimarySpeed']:.9g}+Phase;
float b=dot(q,e)*(6.28318530718/{c['SecondaryLength']:.9g})-Time*{c['SecondarySpeed']:.9g}+Phase*1.37+1.1;
float h=Strength*({c['PrimaryAmplitude']:.9g}*sin(a)+{c['SecondaryAmplitude']:.9g}*sin(b)+{c['HarmonicAmplitude']:.9g}*sin(2*a+.4));
float2 slope=Strength*(({c['PrimaryAmplitude']:.9g}*cos(a)+2*{c['HarmonicAmplitude']:.9g}*cos(2*a+.4))*(6.28318530718/{c['PrimaryLength']:.9g})*d+{c['SecondaryAmplitude']:.9g}*cos(b)*(6.28318530718/{c['SecondaryLength']:.9g})*e);
float crest=Strength*smoothstep(.50,.91,sin(a)+.13*sin(2*a+.4));
return float4(h,slope,crest);'''


def derive_graph(mat):
    expressions = list(edit.get_material_expressions(mat))
    existing = [n for n in expressions if isinstance(n, u.MaterialExpressionCustom)
                and n.get_editor_property('description') == 'MC Storm: shared macro height and derivative']
    if lib.get_metadata_tag(mat, VERSION) == '1':
        require(len(existing) == 1 and existing[0].get_editor_property('code') == wave_code(),
                'Preserved a changed custom storm graph; native/material constants differ')
        return
    require(not existing, 'Incomplete storm graph needs targeted repair before rerun')
    position = node(mat, u.MaterialExpressionWorldPosition, -2300, 0)
    time = parameter(mat, 'StormTime', 0.)
    phase = parameter(mat, 'StormPhase', 0.)
    strength = parameter(mat, 'StormStrength', 0.)
    direction = parameter(mat, 'StormDirection', (1., 0., 0., 0.), True)
    center = parameter(mat, 'ArenaCenter', (0., 0., 0., 0.), True)
    wave = custom(mat, 'MC Storm: shared macro height and derivative', wave_code(),
        {'P': (position, ''), 'Time': (time, ''), 'Phase': (phase, ''), 'Strength': (strength, ''),
         'Direction': (direction, 'RGBA'), 'Center': (center, 'RGBA')}, 4, -1300, 0)
    wpo, pin = source_input(mat, u.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    require(wpo, 'Source liquid lacks its shared micro-height WPO contract')
    combined = custom(mat, 'MC Storm: preserve micro height plus macro wave', 'return Old+float3(0,0,Wave.x);',
        {'Old': (wpo, pin), 'Wave': (wave, '')}, 3, 1400, 0)
    output(combined, u.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    normal, pin = source_input(mat, u.MaterialProperty.MP_NORMAL)
    require(normal and not mat.get_editor_property('tangent_space_normal'), 'Source liquid must supply world-space surface normals')
    combined_normal = custom(mat, 'MC Storm: analytic combined surface slope',
        'return normalize(float3(Old.xy/max(.18,Old.z)-Wave.yz,1));',
        {'Old': (normal, pin), 'Wave': (wave, '')}, 3, 1400, 220)
    output(combined_normal, u.MaterialProperty.MP_NORMAL)
    broad = [n for n in expressions if isinstance(n, u.MaterialExpressionCustom)
             and n.get_editor_property('description') == 'World flow UV, broad ripples']
    for flow in broad:
        entries = list(flow.get_editor_property('inputs'))
        if 'StormDirection' not in [str(p.get_editor_property('input_name')) for p in entries]:
            entry = u.CustomInput()
            entry.set_editor_property('input_name', 'StormDirection')
            entries.append(entry)
            flow.set_editor_property('inputs', entries)
        connect(direction, 'RGBA', flow, 'StormDirection')
        flow.set_editor_property('code', 'float2 flow=normalize(StormDirection.xy+float2(.000001,0));\nreturn P.xy/TileSize-flow*WaterTime*FlowSpeed/TileSize;')
    foam_uv = custom(mat, 'MC Storm: continuous foam advection',
        'float2 d=normalize(Direction.xy+float2(.000001,0));return P.xy/180.-d*Time*.72;',
        {'P': (position, ''), 'Direction': (direction, 'RGBA'), 'Time': (time, '')}, 2, -900, 500)
    detail = lib.load_asset('/Game/Gameplay/Liquid/Stylized/T_LiquidDetail_M')
    require(isinstance(detail, u.Texture2D), 'Existing optical liquid detail texture is required, read only')
    cells = node(mat, u.MaterialExpressionTextureSample, -650, 500, texture=detail,
                 sampler_type=u.MaterialSamplerType.SAMPLERTYPE_MASKS)
    connect(foam_uv, '', cells, 'UVs')
    mask = custom(mat, 'MC Storm: broken crest foam, never full white bands',
        'return Wave.w*(.10+.90*smoothstep(.28,.74,Cells.g))*smoothstep(.08,.55,Cells.a);',
        {'Wave': (wave, ''), 'Cells': (cells, 'RGBA')}, 1, -300, 500)
    tint = parameter(mat, 'FoamColor', (.72, .49, .24, 1.), True)
    color, pin = source_input(mat, u.MaterialProperty.MP_BASE_COLOR)
    require(color, 'Source liquid color connection is required for identity preservation')
    froth = custom(mat, 'MC Storm: source coffee or cola under aerated crests',
        'return lerp(Old,FoamColor.rgb,min(.82,Foam));',
        {'Old': (color, pin), 'FoamColor': (tint, 'RGBA'), 'Foam': (mask, '')}, 3, 1400, 480)
    output(froth, u.MaterialProperty.MP_BASE_COLOR)
    rough, pin = source_input(mat, u.MaterialProperty.MP_ROUGHNESS)
    if rough:
        rough_froth = custom(mat, 'MC Storm: porous foam roughness', 'return lerp(Old,.56,Foam*.8);',
            {'Old': (rough, pin), 'Foam': (mask, '')}, 1, 1400, 680)
        output(rough_froth, u.MaterialProperty.MP_ROUGHNESS)
    emission, pin = source_input(mat, u.MaterialProperty.MP_EMISSIVE_COLOR)
    if emission:
        froth_emission = custom(mat, 'MC Storm: subdued crest fill', 'return Old*(1-Foam*.6)+FoamColor.rgb*Foam*.18;',
            {'Old': (emission, pin), 'Foam': (mask, ''), 'FoamColor': (tint, 'RGBA')}, 3, 1400, 880)
        output(froth_emission, u.MaterialProperty.MP_EMISSIVE_COLOR)
    errors = list(edit.recompile_material(mat))
    require(not errors, 'Storm graph compile failed: ' + str(errors))
    lib.set_metadata_tag(mat, VERSION, '1')
    lib.set_metadata_tag(mat, 'MCStormMathSHA256', hashlib.sha256(HEADER.read_bytes()).hexdigest())
    save(mat)


def clone(original, name, cls):
    destination = FOLDER + '/' + name
    obj = lib.load_asset(destination) if lib.does_asset_exist(destination) else None
    if obj is None:
        obj = tools.duplicate_asset(name, FOLDER, original)
        require(obj, 'Storm material duplication failed')
        lib.set_metadata_tag(obj, OWNER, original.get_path_name())
    require(isinstance(obj, cls) and lib.get_metadata_tag(obj, OWNER) == original.get_path_name(),
            'Preserved unrelated or artist asset occupying ' + destination)
    return obj


def derive_profile(profile_path, role):
    profile = lib.load_asset(profile_path)
    require(profile, 'Missing saved liquid profile: ' + profile_path)
    source = u.SystemLibrary.load_asset_blocking(profile.get_editor_property('surface_material'))
    require(isinstance(source, u.MaterialInterface), 'Profile has no material: ' + profile_path)
    chain, cursor = [], source
    while isinstance(cursor, u.MaterialInstanceConstant):
        require(cursor not in chain and len(chain) < 8, 'Unexpected material parent chain')
        chain.append(cursor)
        cursor = cursor.get_editor_property('parent')
    require(isinstance(cursor, u.Material), 'No editable source liquid base')
    mat = clone(cursor, 'M_Storm' + role + 'Base', u.Material)
    derive_graph(mat)
    parent = mat
    for index, original in enumerate(reversed(chain)):
        name = 'MI_Storm' + role if original == source else 'MI_Storm' + role + 'Parent' + str(index)
        instance = clone(original, name, u.MaterialInstanceConstant)
        edit.set_material_instance_parent(instance, parent)
        edit.update_material_instance(instance)
        save(instance)
        parent = instance
    if not chain:
        name = 'MI_Storm' + role
        destination = FOLDER + '/' + name
        parent = lib.load_asset(destination) if lib.does_asset_exist(destination) else tools.create_asset(name, FOLDER,
            u.MaterialInstanceConstant, u.MaterialInstanceConstantFactoryNew())
        require(parent, 'Could not create storm preset')
        existing = lib.get_metadata_tag(parent, OWNER)
        require(not existing or existing == source.get_path_name(), 'Preserved unrelated storm preset')
        lib.set_metadata_tag(parent, OWNER, source.get_path_name())
        edit.set_material_instance_parent(parent, mat)
        edit.update_material_instance(parent)
        save(parent)
    return dict(profile=profile_path, source=source.get_path_name(), base=mat.get_path_name(), runtime=parent.get_path_name())


def spray_material():
    path = FOLDER + '/M_StormSpray'
    mat = lib.load_asset(path) if lib.does_asset_exist(path) else None
    if mat:
        require(isinstance(mat, u.Material) and lib.get_metadata_tag(mat, OWNER) == 'procedural_spray:1', 'Preserved custom storm spray')
        return mat
    mat = tools.create_asset('M_StormSpray', FOLDER, u.Material, u.MaterialFactoryNew())
    require(mat, 'Could not create bounded spray material')
    lib.set_metadata_tag(mat, OWNER, 'procedural_spray:1')
    mat.set_editor_properties({'blend_mode': u.BlendMode.BLEND_TRANSLUCENT, 'shading_model': u.MaterialShadingModel.MSM_DEFAULT_LIT,
        'translucency_lighting_mode': u.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING,
        'used_with_instanced_static_meshes': True})
    tint = parameter(mat, 'FoamColor', (.72, .49, .24, 1.), True)
    require(edit.connect_material_property(tint, 'RGB', u.MaterialProperty.MP_BASE_COLOR), 'Spray color link failed')
    opacity = node(mat, u.MaterialExpressionConstant, -250, 150, r=.78)
    output(opacity, u.MaterialProperty.MP_OPACITY)
    rough = node(mat, u.MaterialExpressionConstant, -250, 300, r=.16)
    output(rough, u.MaterialProperty.MP_ROUGHNESS)
    errors = list(edit.recompile_material(mat))
    require(not errors, 'Spray material compile failed: ' + str(errors))
    save(mat)
    return mat


def main():
    require(Path(u.Paths.get_project_file_path()).name == 'MessControl.uproject', 'Wrong project')
    require(not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor(), 'Preserve the existing PIE')
    dirty = [p.get_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
    require(not any(p.startswith(FOLDER + '/') for p in dirty), 'Preserve unsaved storm authoring before running')
    source_files = [p for p in Path(u.Paths.project_content_dir()).rglob('*.uasset')
                    if '/Cataclysm/Storm/' not in p.as_posix() and ('Coffee' in p.name or 'Cola' in p.name or 'Liquid' in p.name)]
    before = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in source_files}
    result = [derive_profile('/Game/Data/DA_CoffeeWater', 'Coffee'), derive_profile('/Game/Data/DA_ColdColaLiquid', 'Cola')]
    spray = spray_material()
    after = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in source_files}
    require(before == after, 'A source liquid package changed')
    report = dict(materials=result, spray=spray.get_path_name(), constants=CONSTANTS,
        native_math_sha256=hashlib.sha256(HEADER.read_bytes()).hexdigest(), source_packages_unchanged=True,
        source_package_hashes=before, saved_folder=FOLDER, dirty_before=dirty,
        dirty_after=[p.get_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()],
        validation='Materials compiled/saved; actual gameplay CPU/GPU height, coverage and cost await solo capture')
    target = ROOT / 'Saved/Checks/CataclysmStorm'
    target.mkdir(parents=True, exist_ok=True)
    (target / 'MaterialReport.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print('CATACLYSM_STORM_SAVED ' + json.dumps(report))


if __name__ == '__main__':
    main()
