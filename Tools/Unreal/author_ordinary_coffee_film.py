"""Coffee-only appearance fork; preserve the generic sauce graph and MI overrides.

Default is a read-only plan. Apply in idle, clean MessControl with runpy globals
MC_ORDINARY_COFFEE_APPLY=True. A fresh owned duplicate retains the original
seeded opacity and WPO connections. Only MI_CoffeePuddle is reparented; all ten
exposed override arrays and base-property overrides are copied and verified.
"""
import ast
import datetime
import hashlib
import json
import runpy
import shutil
from pathlib import Path
import unreal as u

ROOT = Path(u.Paths.project_dir()).resolve()
SOURCE = '/Game/Gameplay/Liquid/M_CoffeePuddle'
INSTANCE = '/Game/Gameplay/Liquid/MI_CoffeePuddle'
TARGET = '/Game/Gameplay/Liquid/Coffee/M_CoffeePuddleFilm'
TAG, OWNER, REVISION = 'MCCoffeePuddleFilmAuthor', 'coffee_only_fork_v1', 'ordinary_coffee_film_v1:2'
OUT = ROOT/'Saved/CoffeeStainFilmV3/OrdinaryAuthoring.json'
PROTECTED = (SOURCE, '/Game/Gameplay/Liquid/MI_CurryPuddle',
             '/Game/Gameplay/Liquid/MI_FoodSaucePuddle', '/Game/Gameplay/Hazards/MI_VomitPuddle')
ARRAYS = ('scalar_parameter_values', 'vector_parameter_values', 'double_vector_parameter_values',
          'texture_parameter_values', 'texture_collection_parameter_values',
          'parameter_collection_parameter_values', 'runtime_virtual_texture_parameter_values',
          'sparse_volume_texture_parameter_values', 'font_parameter_values', 'user_scene_texture_overrides')
lib, edit = u.EditorAssetLibrary, u.MaterialEditingLibrary
appearance_source = ROOT/'Tools/Unreal/author_coffee_stain_film_v3.py'
support = runpy.run_path(str(appearance_source), run_name='ordinary_coffee_appearance_support')
Graph, require = support['Graph'], support['require']


def disk_path(path):
    return ROOT/('Content/'+path.removeprefix('/Game/')+'.uasset')


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def reviewed_hlsl(description):
    # Reuse the root-reviewed plain-PBR rev5, without executing its author.
    require(support['REVISION'] == 'coffee_stain_film_v3:5', 'Review the changed shared film revision first')
    for node in ast.walk(ast.parse(appearance_source.read_text(encoding='utf-8-sig'))):
        if isinstance(node, ast.Call) and isinstance(node.func, ast.Attribute) and node.func.attr == 'custom' \
                and len(node.args) >= 2 and isinstance(node.args[0], ast.Constant) and node.args[0].value == description:
            return ast.literal_eval(node.args[1])
    raise RuntimeError('Reviewed appearance block is missing: '+description)


def reviewed_default(name):
    for node in ast.walk(ast.parse(appearance_source.read_text(encoding='utf-8-sig'))):
        if isinstance(node, ast.Call) and isinstance(node.func, ast.Attribute) and node.func.attr in ('vector', 'scalar') \
                and len(node.args) >= 2 and isinstance(node.args[0], ast.Constant) and node.args[0].value == name:
            return ast.literal_eval(node.args[1])
    raise RuntimeError('Reviewed parameter default is missing: '+name)


def palette_calibration(instance):
    result = dict(before={})
    for old_name, new_name, key in (('LiquidColor', 'LiquidColor', 'tint_scale'),
                                    ('EdgeColor', 'CoffeeCrema', 'edge_scale')):
        color = edit.get_material_instance_vector_parameter_value(instance, u.Name(old_name))
        before = [float(color.r), float(color.g), float(color.b)]
        target = reviewed_default(new_name)
        require(all(value > 1.e-6 for value in before), 'Review zero/negative legacy palette before scaling')
        result[key] = [float(value)/prior for value, prior in zip(target, before)]
        result['before'][old_name] = before
    roughness = float(edit.get_material_instance_scalar_parameter_value(instance, u.Name('Roughness')))
    specular = float(edit.get_material_instance_scalar_parameter_value(instance, u.Name('Specular')))
    require(specular > 1.e-6, 'Review zero legacy specular before scaling')
    result.update(roughness_bias=float(reviewed_default('CoffeeRoughness'))-roughness,
                  specular_scale=float(reviewed_default('Specular'))/specular)
    result['before'].update(Roughness=roughness, Specular=specular)
    return result


def overrides(instance):
    raw, exported = {}, {}
    for name in ARRAYS:
        raw[name] = [value.copy() for value in instance.get_editor_property(name)]
        exported[name] = [value.export_text() for value in raw[name]]
    raw['base_property_overrides'] = instance.get_editor_property('base_property_overrides').copy()
    exported['base_property_overrides'] = raw['base_property_overrides'].export_text()
    return raw, exported


def output(material, target):
    node = edit.get_material_property_input_node(material, target)
    pin = edit.get_material_property_input_node_output_name(material, target) if node else ''
    return node, str(pin)


def legacy_fingerprint(material):
    result = []
    for node in edit.get_material_expressions(material):
        entry = dict(name=node.get_name(), cls=node.get_class().get_name())
        for name in ('description', 'code', 'parameter_name', 'default_value'):
            try:
                value = node.get_editor_property(name)
                entry[name] = value.export_text() if hasattr(value, 'export_text') else str(value)
            except Exception:
                pass
        result.append(entry)
    return result


def retain_legacy_graph(material, source, existing_revision):
    legacy = legacy_fingerprint(source)
    source_nodes = {node.get_name(): node for node in edit.get_material_expressions(source)}
    nodes = list(edit.get_material_expressions(material))
    current = {row['name']: row for row in legacy_fingerprint(material)}
    require(all(current.get(row['name']) == row for row in legacy), 'An original seeded graph node changed')
    for target in (u.MaterialProperty.MP_OPACITY_MASK, u.MaterialProperty.MP_WORLD_POSITION_OFFSET):
        old, pin = output(source, target)
        actual, actual_pin = output(material, target)
        require(old is not None and actual is not None and old.get_name() == actual.get_name()
                and pin == actual_pin, 'Original opacity/WPO connection changed')
    require(float(material.get_editor_property('opacity_mask_clip_value'))
            == float(source.get_editor_property('opacity_mask_clip_value')), 'Original opacity clip changed')
    require(material.get_editor_property('shading_model') == u.MaterialShadingModel.MSM_DEFAULT_LIT,
            'Review non-DefaultLit ordinary coffee before authoring')
    extras = [node for node in nodes if node.get_name() not in source_nodes]
    if existing_revision == 'ordinary_coffee_film_v1:1':
        # The first owned fork predates per-node ownership metadata. Accept
        # only the exact classes/names introduced by that author, not extra
        # unowned nodes someone may have saved since the original migration.
        custom = {'Coffee-only irregular pooling', 'Coffee-only espresso pigment and narrow edge',
                  'Coffee-only bounded seeded-film meniscus',
                  'Coffee-only wet roughness; retain original override control',
                  'Coffee-only specular; retain original override control',
                  'Coffee-only restrained warm sheen; retain original override control'}
        scalar = {'CoffeeFilmMeniscusShadingCm', 'CoffeeFilmRoughnessBias',
                  'CoffeeFilmSpecularScale', 'CoffeeFilmSheenScale'}
        special = {'MaterialExpressionWorldPosition', 'MaterialExpressionCameraVectorWS',
                   'MaterialExpressionPixelNormalWS'}
        found_custom, found_scalar, found_special = [], [], []
        for node in extras:
            if isinstance(node, u.MaterialExpressionCustom):
                found_custom.append(str(node.get_editor_property('description')))
            elif isinstance(node, u.MaterialExpressionScalarParameter):
                found_scalar.append(str(node.get_editor_property('parameter_name')))
            else:
                found_special.append(node.get_class().get_name())
        require(len(extras) == 13 and set(found_custom) == custom and len(found_custom) == 6
                and set(found_scalar) == scalar and len(found_scalar) == 4
                and set(found_special) == special and len(found_special) == 3,
                'Preserve unrecognized nodes in the previous owned fork')
    elif existing_revision == REVISION:
        owned = json.loads(lib.get_metadata_tag(material, 'MCCoffeePuddleAppearanceNodes') or '[]')
        require(sorted(node.get_name() for node in extras) == sorted(owned),
                'Preserve unknown or partial nodes in the owned fork')
    else:
        require(not extras and not existing_revision, 'Unexpected existing appearance revision')
    for node in extras:
        edit.delete_material_expression(material, node)
    require(len(list(edit.get_material_expressions(material))) == len(legacy), 'Old appearance nodes remain')
    return legacy, source_nodes


def author(material, source, existing_revision, calibration):
    legacy, source_nodes = retain_legacy_graph(material, source, existing_revision)
    nodes = list(edit.get_material_expressions(material))
    opacity, opacity_pin = output(material, u.MaterialProperty.MP_OPACITY_MASK)
    wpo, wpo_pin = output(material, u.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    require(opacity and wpo, 'Preserve the existing seeded opacity and WPO outputs')
    parameters = {}
    for node in nodes:
        if isinstance(node, (u.MaterialExpressionScalarParameter, u.MaterialExpressionVectorParameter,
                             u.MaterialExpressionTextureObjectParameter)):
            parameters[str(node.get_editor_property('parameter_name'))] = node
    for name in ('WipeMask', 'Seed', 'Finish', 'BrushAge', 'Brush', 'Depth', 'Rim', 'WorldSize',
                 'LiquidColor', 'EdgeColor', 'Roughness', 'Specular', 'WetSheen'):
        require(name in parameters, 'Preserve missing original parameter '+name)
    original_normal, _ = output(source, u.MaterialProperty.MP_NORMAL)
    old_normal = next(node for node in nodes if node.get_name() == original_normal.get_name())
    require(isinstance(old_normal, u.MaterialExpressionCustom), 'Expected the retained legacy coffee normal')
    legacy_normal = str(old_normal.get_editor_property('code'))
    require(legacy_normal.count('CoffeeSurface S;') == 1, 'Unexpected legacy footprint function')
    prefix = legacy_normal.split('CoffeeSurface S;', 1)[0]+'CoffeeSurface S;\n'
    uv = next(n for n in nodes if isinstance(n, u.MaterialExpressionTextureCoordinate))
    clock = next(n for n in nodes if isinstance(n, u.MaterialExpressionTime))
    g = Graph(material)
    g.row = 3200
    pos = g.node(u.MaterialExpressionWorldPosition)
    mottle = g.custom('Coffee-only quiet rev5 grain', reviewed_hlsl(
        'Quiet fine espresso grain; no streak or camouflage field'), dict(P=pos), 4)
    tint = g.custom('Coffee-only calibrated legacy palette', 'return Legacy.rgb*Scale.rgb;',
        {'Legacy': (parameters['LiquidColor'], 'RGB'),
         'Scale': (g.vector('CoffeeFilmTintScale', calibration['tint_scale']), 'RGB')}, 3)
    crema = g.custom('Coffee-only calibrated legacy contact edge', 'return Legacy.rgb*Scale.rgb;',
        {'Legacy': (parameters['EdgeColor'], 'RGB'),
         'Scale': (g.vector('CoffeeFilmEdgeScale', calibration['edge_scale']), 'RGB')}, 3)
    color = g.custom('Coffee-only espresso pigment and narrow edge', reviewed_hlsl(
        'Dark thin espresso film; quiet fine grain and narrow amber contact edge'),
        dict(Tint=tint, Crema=crema, Mottle=mottle, Coverage=(opacity, opacity_pin)), 3)
    g.out(color, u.MaterialProperty.MP_BASE_COLOR)
    normal_code = prefix+'''
float e=1.0/64.0;
float x=S.surface(UV+float2(e,0),Wipe,WipeSampler,Seed,Finish,Age,Brush,Time,Depth,Rim).y-S.surface(UV-float2(e,0),Wipe,WipeSampler,Seed,Finish,Age,Brush,Time,Depth,Rim).y;
float y=S.surface(UV+float2(0,e),Wipe,WipeSampler,Seed,Finish,Age,Brush,Time,Depth,Rim).y-S.surface(UV-float2(0,e),Wipe,WipeSampler,Seed,Finish,Age,Brush,Time,Depth,Rim).y;
float2 meniscus=-float2(x,y)*clamp(FilmDepth,0,1.2)/max(WorldSize*e*2,1);
float2 micro=clamp(Mottle.zw*clamp(MicroStrength,0,.006),-.009,.009)*smoothstep(.40,.85,Coverage);
return normalize(float3(meniscus+micro,1));
'''
    normal = g.custom('Coffee-only bounded seeded-film meniscus', normal_code,
        dict(UV=uv, Wipe=parameters['WipeMask'], Seed=parameters['Seed'], Finish=parameters['Finish'],
             Age=parameters['BrushAge'], Brush=(parameters['Brush'], 'RGBA'), Time=clock,
             Depth=parameters['Depth'], Rim=parameters['Rim'], WorldSize=parameters['WorldSize'],
             FilmDepth=g.scalar('CoffeeFilmMeniscusShadingCm', .35), Mottle=mottle,
             Coverage=(opacity, opacity_pin), MicroStrength=g.scalar('CoffeeMicroNormalStrength', .0025)), 3)
    g.out(normal, u.MaterialProperty.MP_NORMAL)
    roughness_base = g.custom('Coffee-only calibrated legacy roughness', 'return Legacy+Bias;',
        dict(Legacy=parameters['Roughness'], Bias=g.scalar('CoffeeFilmRoughnessBias', calibration['roughness_bias'])))
    roughness = g.custom('Coffee-only plain PBR wet roughness', reviewed_hlsl(
        'Plain PBR gloss; live-comparable roughness and quiet contact edge'),
        dict(Roughness=roughness_base, Mottle=mottle, Coverage=(opacity, opacity_pin)))
    g.out(roughness, u.MaterialProperty.MP_ROUGHNESS)
    specular = g.custom('Coffee-only specular; retain original override control',
        'return saturate(Legacy*Scale);',
        dict(Legacy=parameters['Specular'], Scale=g.scalar('CoffeeFilmSpecularScale', calibration['specular_scale'])))
    g.out(specular, u.MaterialProperty.MP_SPECULAR)
    g.out(g.custom('Coffee-only zero emissive', reviewed_hlsl(
        'No fake glints, pigment floor or self illumination'), {}, 3), u.MaterialProperty.MP_EMISSIVE_COLOR)
    require(output(material, u.MaterialProperty.MP_OPACITY_MASK) == (opacity, opacity_pin), 'Opacity output changed')
    require(output(material, u.MaterialProperty.MP_WORLD_POSITION_OFFSET) == (wpo, wpo_pin), 'Legacy WPO output changed')
    after = {row['name']: row for row in legacy_fingerprint(material)}
    require(all(after.get(row['name']) == row for row in legacy), 'An original graph node changed')
    owned = [node.get_name() for node in edit.get_material_expressions(material)
             if node.get_name() not in source_nodes]
    lib.set_metadata_tag(material, 'MCCoffeePuddleAppearanceNodes', json.dumps(owned))
    return dict(legacy_nodes_unchanged=len(legacy), opacity_node=opacity.get_name(),
        wpo_node=wpo.get_name(), opacity_clip=float(material.get_editor_property('opacity_mask_clip_value')),
        appearance_nodes_added=edit.get_num_material_expressions(material)-len(legacy), owned_appearance_nodes=owned)


def main():
    require(Path(u.Paths.get_project_file_path()).stem == 'MessControl', 'Wrong project')
    require(support['REVISION'] == 'coffee_stain_film_v3:5', 'Review the changed shared film revision first')
    require(not u.EditorLevelLibrary.get_pie_worlds(False), 'Preserve active PIE')
    require(not list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages())
            and not list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages()), 'Preserve unsaved packages')
    source, instance = lib.load_asset(SOURCE), lib.load_asset(INSTANCE)
    require(isinstance(source, u.Material) and isinstance(instance, u.MaterialInstanceConstant), 'Unexpected source types')
    before_parent = instance.get_editor_property('parent')
    existing = lib.load_asset(TARGET) if lib.does_asset_exist(TARGET) else None
    require(before_parent == source or (existing is not None and before_parent == existing), 'Preserve custom MI parent')
    require(not list(edit.get_static_switch_parameter_names(source)), 'Review static-switch contract before migration')
    raw, exported = overrides(instance)
    protected = {path: digest(disk_path(path)) for path in PROTECTED}
    existing_revision = ''
    if existing:
        existing_revision = lib.get_metadata_tag(existing, 'MCCoffeePuddleFilmRevision')
        require(isinstance(existing, u.Material) and lib.get_metadata_tag(existing, TAG) == OWNER
                and existing_revision in ('ordinary_coffee_film_v1:1', REVISION)
                and lib.get_metadata_tag(existing, 'MCCoffeePuddleSourceSHA256') == protected[SOURCE], 'Preserve unowned or partial target')
    wave = lib.load_asset('/Game/Cataclysm/Jelly/M_CataclysmPaintTrail')
    require(lib.get_metadata_tag(wave, 'MCCoffeeSurfaceRevision') == 'coffee_stain_film_v3:5',
            'Apply/review the plain-PBR rev5 wave film before ordinary parity')
    calibration = palette_calibration(instance)
    report = dict(status='plan', revision=REVISION, source=SOURCE, target=TARGET, instance=INSTANCE,
        parent_before=before_parent.get_path_name(), protected_package_sha256=protected,
        instance_before_sha256=digest(disk_path(INSTANCE)), overrides_before=exported,
        existing_revision=existing_revision, target_before_sha256=digest(disk_path(TARGET)) if existing else None,
        calibration=calibration,
        source_author_sha256=digest(__file__), reviewed_appearance_sha256=digest(appearance_source),
        mask_contract='Preserve all legacy nodes and original opacity/WPO output connections; retain seeded/Finish/Brush mask and original clip',
        appearance_contract='Coffee-only root-reviewed rev5 plain PBR pigment/normal/roughness; calibrate original palette/roughness/specular through film scales/bias without changing overrides; retain unused WetSheen override but remove fake emissive',
        shared_appearance_revision='coffee_stain_film_v3:5',
        texture_cost='Original seeded opacity/WPO remain. Pixel appearance uses original opacity plus four seeded-alpha neighbours and one four-hash arithmetic grain; no fake reflection/custom emissive.',
        static_switch_parameters=[], gameplay_visual_review='pending', gpu_cost='not measured')
    OUT.parent.mkdir(parents=True, exist_ok=True)
    if OUT.exists():
        previous = OUT.read_bytes()
        history = OUT.parent/'History'/(hashlib.sha256(previous).hexdigest()+'.json')
        history.parent.mkdir(parents=True, exist_ok=True)
        if not history.exists():
            history.write_bytes(previous)
    if globals().get('MC_ORDINARY_COFFEE_APPLY', False):
        backup = OUT.parent/'OrdinaryBackup'/report['instance_before_sha256']
        backup.mkdir(parents=True, exist_ok=True)
        if not (backup/disk_path(INSTANCE).name).exists():
            shutil.copy2(disk_path(INSTANCE), backup/disk_path(INSTANCE).name)
        report['backup'] = str(backup)
        if existing:
            target_backup = OUT.parent/'OrdinaryBackup'/report['target_before_sha256']/disk_path(TARGET).name
            target_backup.parent.mkdir(parents=True, exist_ok=True)
            if not target_backup.exists():
                shutil.copy2(disk_path(TARGET), target_backup)
            report['target_backup'] = str(target_backup)
        OUT.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
        target = existing
        if target is None:
            require(lib.does_directory_exist('/Game/Gameplay/Liquid/Coffee')
                    or lib.make_directory('/Game/Gameplay/Liquid/Coffee'), 'Could not create owned folder')
            target = lib.duplicate_asset(SOURCE, TARGET)
            require(isinstance(target, u.Material), 'Could not duplicate the generic graph')
            lib.set_metadata_tag(target, TAG, OWNER)
        report['graph'] = author(target, source, existing_revision, calibration)
        errors = edit.recompile_material(target)
        require(not errors, 'Coffee material compile errors: '+str(errors))
        lib.set_metadata_tag(target, 'MCCoffeePuddleFilmRevision', REVISION)
        lib.set_metadata_tag(target, 'MCCoffeePuddleSourceSHA256', protected[SOURCE])
        edit.set_material_instance_parent(instance, target)
        # Deep native struct copies include parameter infos and expression GUIDs.
        for name, values in raw.items():
            instance.set_editor_property(name, values)
        edit.update_material_instance(instance)
        _, after = overrides(instance)
        require(after == exported, 'Material instance override content changed')
        require(lib.save_loaded_asset(target, False), 'Could not save owned coffee film')
        require(lib.save_loaded_asset(instance, False), 'Could not save coffee instance')
        require(all(digest(disk_path(path)) == before for path, before in protected.items()), 'Protected source/sibling bytes changed')
        report.update(status='authored', compiled=True, overrides_after=after, override_arrays_preserved=True,
            parent_after=instance.get_editor_property('parent').get_path_name(),
            instance_after_sha256=digest(disk_path(INSTANCE)), target_sha256=digest(disk_path(TARGET)))
    report['utc'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    OUT.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
