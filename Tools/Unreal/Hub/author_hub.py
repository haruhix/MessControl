"""Backed-up, narrowly scoped Hub authoring for the rebuilt MessControl Editor.

Default ``run()`` is a read-only plan. Explicit ``run('apply', placement=...)``
authors only Hub, its selector clone, the destination DT, new localization
rows and, if required, one owned ready-border material. No build, cook, PIE,
network connection, SaveAll or original-menu save.
Run through an owned Editor process and defer quitting until normal ticks resume.
"""
from __future__ import annotations

from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import sys
import traceback
import uuid

HERE = Path(__file__).resolve().parent
HUB = '/Game/Maps/Hub'
SELECTOR_SOURCE = '/Game/UI/MainMenu3D/L_MainMenu3D_RefFinish_v08'
SELECTOR = '/Game/Maps/L_HubSelection'
DESTINATIONS = '/Game/Gameplay/Hub/DT_HubDestinations'
TEXTS = '/Game/Gameplay/UI/Data/DT_UI_Localization'
READY_MATERIAL = '/Game/Gameplay/Hub/M_HubReady'
ENGINE_MARKER_MATERIAL = '/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial'
MATERIAL_OWNER_KEY = 'MC_HubAuthoring'
MATERIAL_OWNER_VALUE = 'ReadyBorder_v1'
MODE = '/Script/MessControl.MCHubGameMode'
ZONE = '/Script/MessControl.MCHubReadyZone'
ROW = '/Script/MessControl.MCHubDestinationRow'
OWNED_TAG = 'MCHubOwned'
DEFAULT_OUTPUT = 'E:/CodexWork/MessControlHub_20261019/Authoring'


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def on_e(path):
    value = Path(path).resolve()
    require(value.drive.upper() == 'E:', 'Working/output files must stay on E: ' + str(value))
    return value


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def write_json(path, value):
    path = on_e(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + '.tmp')
    temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding='utf-8')
    os.replace(temporary, path)


def checkpoint(report, phase):
    """Persist each completed phase even if a later engine operation crashes."""
    report.setdefault('completed_phases', []).append(phase)
    report['last_completed_phase'] = phase
    report['checkpoint_utc'] = datetime.now(timezone.utc).isoformat()
    write_json(Path(report['output']) / 'Report.json', report)


def vec(value):
    return [round(float(value.x), 4), round(float(value.y), 4), round(float(value.z), 4)]


def dirty_packages(u):
    return sorted({package.get_path_name() for package in
                   list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages()) +
                   list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())})


def read_rows(u, table):
    return json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(table))


def package_file(root, package, suffix):
    require(package.startswith('/Game/') and '..' not in package, 'Invalid package path')
    return (root / 'Content' / package[len('/Game/'):]).with_suffix(suffix)


def backup_package(root, package, output, report):
    files = []
    for suffix in ('.umap', '.uasset', '.uexp', '.ubulk'):
        source = package_file(root, package, suffix)
        if not source.is_file():
            continue
        target = output / 'Backups' / source.relative_to(root)
        target.parent.mkdir(parents=True, exist_ok=True)
        require(not target.exists(), 'Refusing to overwrite a backup')
        shutil.copy2(source, target)
        require(digest(source) == digest(target), 'Backup hash mismatch: ' + str(source))
        files.append({'source': str(source), 'backup': str(target), 'sha256': digest(source)})
    report['backups'].extend(files)
    return files


def actor_record(actor):
    rotation = actor.get_actor_rotation()
    return {'name': actor.get_name(), 'label': actor.get_actor_label(),
            'class': actor.get_class().get_path_name(), 'location': vec(actor.get_actor_location()),
            'rotation': [round(float(rotation.pitch), 4), round(float(rotation.yaw), 4), round(float(rotation.roll), 4)],
            'scale': vec(actor.get_actor_scale3d()),
            'tags': [str(tag) for tag in actor.tags]}


def sample_ground(u, world, actors, ground, x, y, diagnostics=None):
    center, extent = ground.get_actor_bounds(False)
    top = max(center.z + extent.z + 1200., 2000.)
    bottom = center.z - extent.z - 1200.
    ignored = [actor for actor in actors if actor != ground]
    start, end = u.Vector(x, y, top), u.Vector(x, y, bottom)
    hit = u.SystemLibrary.line_trace_single(world, start, end,
            u.TraceTypeQuery.TRACE_TYPE_QUERY1, True, ignored, u.DrawDebugTrace.NONE, True)
    evidence = {'world': world.get_path_name(), 'ground': ground.get_path_name(),
                'start': vec(start), 'end': vec(end), 'complex': True,
                'ignored_count': len(ignored), 'hit_is_none': hit is None}
    if not hit:
        evidence['accepted'] = False
        if diagnostics is not None:
            diagnostics.append(evidence)
        return None
    # Use the explicit Blueprint break contract. FHitResult exposes a native
    # break function, so its current Python tuple can also use this order.
    parts = u.get_default_object(u.GameplayStatics).call_method('BreakHitResult', args=(hit,))
    accepted = bool(parts[0] and parts[9] == ground and parts[7].z >= .75)
    evidence.update(blocking_hit=bool(parts[0]), hit_actor=parts[9].get_path_name() if parts[9] else None,
                    hit_component=parts[10].get_path_name() if parts[10] else None,
                    impact_point=vec(parts[5]), impact_normal=vec(parts[7]),
                    actor_matches=parts[9] == ground, accepted=accepted)
    if diagnostics is not None:
        diagnostics.append(evidence)
    if not accepted:
        return None
    return {'point': vec(parts[5]), 'normal': vec(parts[7]), 'actor': ground.get_actor_label()}


def has_clearance(u, world, point, ignored):
    """Reject authored spawn/ready points occupied by scene collision."""
    center = u.Vector(*point) + u.Vector(0, 0, 100.)
    hit = u.SystemLibrary.capsule_trace_single(world, center, center + u.Vector(0, 0, .1),
            38., 85., u.TraceTypeQuery.TRACE_TYPE_QUERY1, False, ignored, u.DrawDebugTrace.NONE, True)
    if not hit:
        return True
    parts = u.get_default_object(u.GameplayStatics).call_method('BreakHitResult', args=(hit,))
    return not parts[0]


def placement_plan(u, world, actors, placement, diagnostics=None):
    """Require an inspected ground actor and explicit reference-aligned XY fractions.

    ``fraction=[0.0..1.0,0.0..1.0]`` is measured within actual world XY bounds.
    Fractions must be selected from the inspection/preview, not scenery bounds.
    Each center/corner must hit this ground mesh; a missed corner rejects authoring.
    """
    require(isinstance(placement, dict), 'Select placement from inspected Hub geometry first')
    key = placement.get('ground_actor')
    matches = [actor for actor in actors if actor.get_actor_label() == key or actor.get_name() == key]
    require(len(matches) == 1, 'Select exactly one inspected ground actor: ' + str(key))
    ground = matches[0]
    center, bounds = ground.get_actor_bounds(False)
    fraction = placement.get('fraction')
    extent = placement.get('extent', [240., 240., 140.])
    require(isinstance(fraction, list) and len(fraction) == 2 and all(math.isfinite(v) and 0 < v < 1 for v in fraction),
            'Placement fraction must be two finite normalized XY values inside the ground bounds')
    require(len(extent) == 3 and all(math.isfinite(v) and v >= 80 for v in extent), 'Unsafe ready box extent')
    x = center.x + bounds.x * (2 * fraction[0] - 1)
    y = center.y + bounds.y * (2 * fraction[1] - 1)
    samples = []
    for dx, dy in [(0, 0), (-extent[0], -extent[1]), (-extent[0], extent[1]),
                   (extent[0], -extent[1]), (extent[0], extent[1])]:
        sample = sample_ground(u, world, actors, ground, x + dx, y + dy, diagnostics)
        require(sample is not None, 'Ready-zone footprint has no walkable ground at ' + str([x + dx, y + dy]))
        require(has_clearance(u, world, sample['point'], [ground]),
                'Ready-zone footprint intersects existing scene collision at ' + str(sample['point']))
        samples.append(sample)
    heights = [sample['point'][2] for sample in samples]
    require(max(heights) - min(heights) < 50, 'Ready-zone footprint is too steep')
    # Native ReadyVolume is the root. Its origin is the box center; marker and
    # optional future mouth mesh are positioned relative to this editable actor.
    return {'ground_actor': ground.get_actor_label(), 'ground_bounds_center': vec(center),
            'ground_bounds_extent': vec(bounds), 'fraction': fraction, 'extent': extent,
            'location': [x, y, max(heights) + extent[2] + 3.], 'surface_samples': samples}


def tag_owned(u, actor, label):
    actor.set_actor_label(label)
    tags = list(actor.tags)
    if OWNED_TAG not in [str(tag) for tag in tags]:
        tags.append(u.Name(OWNED_TAG))
        actor.tags = tags


def ready_material(u, report):
    """Verify the exact runtime MID contract before using a border material.

    Prefer the engine material only if its loaded asset actually exposes both
    names used by AMCHubReadyZone. Otherwise create one small owned opaque lit
    material. Existing designer graphs and material instances are never rebuilt.
    """
    lib, edit = u.EditorAssetLibrary, u.MaterialEditingLibrary
    engine = u.load_asset(ENGINE_MARKER_MATERIAL)
    vectors = [str(name) for name in edit.get_vector_parameter_names(engine)] if engine else []
    scalars = [str(name) for name in edit.get_scalar_parameter_names(engine)] if engine else []
    report['engine_marker_material_probe'] = {'path': ENGINE_MARKER_MATERIAL,
                                              'vector_parameters': vectors, 'scalar_parameters': scalars}
    if 'Color' in vectors and 'Roughness' in scalars:
        report['ready_material'] = {'path': engine.get_path_name(), 'created': False,
                                    'engine_mid_parameters_verified': True}
        return engine
    material = u.load_asset(READY_MATERIAL)
    created = material is None
    if created:
        folder, name = READY_MATERIAL.rsplit('/', 1)
        material = u.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, u.Material, u.MaterialFactoryNew())
        require(isinstance(material, u.Material), 'Could not create owned ready-border material')
        lib.set_metadata_tag(material, MATERIAL_OWNER_KEY, MATERIAL_OWNER_VALUE)
        material.set_editor_property('blend_mode', u.BlendMode.BLEND_OPAQUE)
        material.set_editor_property('shading_model', u.MaterialShadingModel.MSM_DEFAULT_LIT)
        color = edit.create_material_expression(material, u.MaterialExpressionVectorParameter, -420, -100)
        color.set_editor_property('parameter_name', 'Color')
        color.set_editor_property('default_value', u.LinearColor(.08, .75, .65, 1.))
        roughness = edit.create_material_expression(material, u.MaterialExpressionScalarParameter, -420, 100)
        roughness.set_editor_property('parameter_name', 'Roughness')
        roughness.set_editor_property('default_value', .75)
        require(edit.connect_material_property(color, 'RGB', u.MaterialProperty.MP_BASE_COLOR), 'Ready Color connection failed')
        require(edit.connect_material_property(roughness, '', u.MaterialProperty.MP_ROUGHNESS), 'Ready Roughness connection failed')
        errors = edit.recompile_material(material)
        require(not errors, 'Ready-border material compile failed: ' + str(errors))
        report['created_assets'].append(READY_MATERIAL)
    require(isinstance(material, u.Material), 'Ready-border material path has an unexpected asset type')
    require(lib.get_metadata_tag(material, MATERIAL_OWNER_KEY) == MATERIAL_OWNER_VALUE,
            'Ready-border target path belongs to an unrecognized material; preserve it for review')
    actual_vectors = [str(name) for name in edit.get_vector_parameter_names(material)]
    actual_scalars = [str(name) for name in edit.get_scalar_parameter_names(material)]
    require('Color' in actual_vectors and 'Roughness' in actual_scalars, 'Owned ready-border MID contract is invalid')
    if created:
        require(lib.save_loaded_asset(material, only_if_is_dirty=False), 'Could not save ready-border material')
        report['saved_assets'].append(READY_MATERIAL)
    report['ready_material'] = {'path': material.get_path_name(), 'created': created,
                                'engine_mid_parameters_verified': False,
                                'vector_parameters': actual_vectors, 'scalar_parameters': actual_scalars}
    return material


def assign_ready_material(u, zone, material, report):
    """Migrate only blank/default/owned border slots, preserving designer choices."""
    changes = []
    known = {ENGINE_MARKER_MATERIAL, READY_MATERIAL + '.M_HubReady'}
    for border in zone.get_editor_property('border_markers'):
        before = border.get_material(0)
        previous = before.get_path_name() if before else None
        if before == material:
            continue
        if before is not None and previous not in known:
            changes.append({'component': border.get_name(), 'preserved_designer_material': previous})
            continue
        border.modify()
        border.set_material(0, material)
        changes.append({'component': border.get_name(), 'before': previous, 'after': material.get_path_name()})
    report['ready_material_assignments'] = changes
    return any('after' in change for change in changes)


def author_hub(u, mode, zone_class, placement, material, report):
    level = u.get_editor_subsystem(u.LevelEditorSubsystem)
    editor = u.get_editor_subsystem(u.EditorActorSubsystem)
    subsystem = u.get_editor_subsystem(u.UnrealEditorSubsystem)
    world = subsystem.get_editor_world()
    # An owned wrapper may preload the map and allow registration/physics to
    # settle over editor ticks. Do not reload that already prepared world here.
    if not world or world.get_path_name().split('.', 1)[0] != HUB:
        require(level.load_level(HUB), 'Could not open existing Hub')
        world = subsystem.get_editor_world()
    actors = list(editor.get_all_level_actors())
    existing = [actor for actor in actors if isinstance(actor, u.MCHubReadyZone)]
    require(len(existing) <= 1, 'More than one ready zone exists; review before authoring')
    report['hub_original_actors'] = [actor_record(actor) for actor in actors]
    settings = world.get_world_settings()
    changed = settings.get_editor_property('default_game_mode') != mode
    if changed:
        settings.modify()
        settings.set_editor_property('default_game_mode', mode)
    if existing:
        zone = existing[0]
        report['ready_zone_preserved'] = actor_record(zone)
    else:
        target = placement_plan(u, world, actors, placement, report['ground_trace_diagnostics'])
        zone = editor.spawn_actor_from_class(zone_class, u.Vector(*target['location']))
        require(zone is not None, 'Could not create ready zone')
        tag_owned(u, zone, 'Hub_ReadyZone')
        zone.set_zone_extent(u.Vector(*target['extent']))
        report['ready_zone_placement'] = target
        report['ready_zone_created'] = actor_record(zone)
        changed = True
    changed = assign_ready_material(u, zone, material, report) or changed
    # Safe starts are only added if the designer authored none. They are sampled
    # on the same inspected ground and never replace scenery or existing starts.
    starts = [actor for actor in actors if isinstance(actor, u.PlayerStart)]
    report['existing_player_starts'] = [actor_record(actor) for actor in starts]
    if not starts:
        require(placement is not None, 'A ground selection is required to add missing PlayerStarts')
        target = placement_plan(u, world, actors, placement, report['ground_trace_diagnostics'])
        ground = next(actor for actor in actors if actor.get_actor_label() == target['ground_actor'])
        center, unused = ground.get_actor_bounds(False)
        spawn_rows, selected = [], []
        candidates = [(dx, dy) for dy in (-480., -160., 160., 480.) for dx in (-480., -160., 160., 480.)]
        candidates.sort(key=lambda value: value[0] ** 2 + value[1] ** 2)
        for dx, dy in candidates:
            sample = sample_ground(u, world, actors, ground, center.x + dx, center.y + dy,
                                   report['ground_trace_diagnostics'])
            if sample is None or not has_clearance(u, world, sample['point'], [ground, zone]):
                continue
            if any(math.dist(sample['point'][:2], previous[:2]) < 250 for previous in selected):
                continue
            selected.append(sample['point'])
            if len(selected) == 4:
                break
        require(len(selected) == 4, 'No four clear central spawn points; inspect Hub before saving')
        for index, point in enumerate(selected):
            location = u.Vector(*point) + u.Vector(0, 0, 125.)
            start = editor.spawn_actor_from_class(u.PlayerStart, location)
            require(start is not None, 'Could not create missing PlayerStart')
            tag_owned(u, start, 'Hub_PlayerStart_' + str(index + 1))
            spawn_rows.append(actor_record(start))
        report['created_player_starts'] = spawn_rows
        changed = True
    # Original actors are checked before saving, including their transforms/tags.
    current = {actor.get_name(): actor_record(actor) for actor in editor.get_all_level_actors()}
    for original in report['hub_original_actors']:
        require(current.get(original['name']) == original, 'Unrelated Hub actor changed: ' + original['label'])
    if changed:
        require(level.save_current_level(), 'Could not save Hub')
        report['saved_assets'].append(HUB)


def author_selector(u, mode, report):
    lib = u.EditorAssetLibrary
    created = not lib.does_asset_exist(SELECTOR)
    if created:
        world = lib.duplicate_asset(SELECTOR_SOURCE, SELECTOR)
        require(isinstance(world, u.World), 'Could not duplicate selector map')
        report['created_assets'].append(SELECTOR)
    else:
        world = u.load_asset(SELECTOR)
    require(isinstance(world, u.World), 'Selector target is not a World')
    # DuplicateAsset creates an in-memory standalone World, not a saved map.
    # Edit and save that explicit World before any map loading. Opening it in
    # this same process can retain an old standalone World during map cleanup;
    # the next owned fresh process performs map-open/runtime validation instead.
    settings = world.get_world_settings()
    current_mode = settings.get_editor_property('default_game_mode')
    require(created or current_mode == mode, 'Existing selector path is not owned by Hub authoring')
    if current_mode != mode:
        settings.modify()
        settings.set_editor_property('default_game_mode', mode)
    if created or current_mode != mode:
        require(u.EditorLoadingAndSavingUtils.save_map(world, SELECTOR), 'Could not save selector clone World')
        report['saved_assets'].append(SELECTOR)
        checkpoint(report, 'selector_clone_saved')
    report['selector_game_mode'] = settings.get_editor_property('default_game_mode').get_path_name()
    try:
        report['selector_actor_count'] = len(u.GameplayStatics.get_all_actors_of_class(world, u.Actor))
    except Exception as exc:
        report['selector_actor_count_read_error'] = str(exc)
    report['selector_fresh_process_validation_pending'] = True
    # Retain no Python World/settings references beyond this function. Do not
    # force collection while an editor-map operation is on the Python stack.
    del settings
    del world


def author_destinations(u, struct, rows, report):
    table = u.load_asset(DESTINATIONS)
    if table is None:
        factory = u.DataTableFactory()
        factory.set_editor_property('struct', struct)
        folder, name = DESTINATIONS.rsplit('/', 1)
        table = u.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, u.DataTable, factory)
        report['created_assets'].append(DESTINATIONS)
    require(isinstance(table, u.DataTable), 'Destination asset is not a DataTable')
    require(u.DataTableFunctionLibrary.get_data_table_row_struct(table) == struct, 'Destination row struct mismatch')
    before = read_rows(u, table)
    indexed = {row['Name'].casefold(): row for row in before}
    merged = before + [row for row in rows if row['Name'].casefold() not in indexed]
    if merged != before:
        table.modify()
        require(u.DataTableFunctionLibrary.fill_data_table_from_json_string(table, json.dumps(merged, ensure_ascii=False)),
                'Destination table import failed')
        after = read_rows(u, table)
        require(len(after) == len(merged), 'Destination row count changed during import')
        for row in before:
            require(next((value for value in after if value['Name'] == row['Name']), None) == row,
                    'Existing designer destination changed: ' + row['Name'])
        require(u.EditorAssetLibrary.save_loaded_asset(table, only_if_is_dirty=False), 'Could not save destination table')
        report['saved_assets'].append(DESTINATIONS)
    report['destinations'] = read_rows(u, table)


def append_localization(u, rows, report):
    sys.path.insert(0, str(HERE.parent / 'UI'))
    from localization_catalog import index_rows, migrate_localization_row
    table = u.load_asset(TEXTS)
    require(isinstance(table, u.DataTable), 'Shared localization table missing')
    before = read_rows(u, table)
    indexed = index_rows(before, 'existing localization')
    index_rows(rows, 'Hub localization')
    additions = [migrate_localization_row(row) for row in rows if row['Name'].casefold() not in indexed]
    if additions:
        table.modify()
        require(u.DataTableFunctionLibrary.fill_data_table_from_json_string(table, json.dumps(before + additions, ensure_ascii=False)),
                'Hub localization append failed')
        after = read_rows(u, table)
        actual = index_rows(after, 'appended localization')
        for row in before:
            require(actual[row['Name'].casefold()] == row, 'Existing localization changed: ' + row['Name'])
        require(u.EditorAssetLibrary.save_loaded_asset(table, only_if_is_dirty=False), 'Could not save Hub localization')
        report['saved_assets'].append(TEXTS)
    report['localization_added'] = [row['Name'] for row in additions]


def run(action='plan', *, placement=None, output_dir=DEFAULT_OUTPUT, restore_original=False):
    import unreal as u
    require(action in ('plan', 'apply'), 'Unknown action')
    root = on_e(u.Paths.project_dir())
    require((root / 'MessControl.uproject').is_file(), 'Expected MessControl project')
    level = u.get_editor_subsystem(u.LevelEditorSubsystem)
    require(not level.is_in_play_in_editor(), 'Stop PIE before authoring')
    require(not dirty_packages(u), 'Preserve unsaved packages before authoring: ' + str(dirty_packages(u)))
    original = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world().get_path_name().split('.', 1)[0]
    output = on_e(output_dir) / (datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ') + '_' + uuid.uuid4().hex[:8])
    output.mkdir(parents=True, exist_ok=False)
    report = {'status': 'running', 'action': action, 'output': str(output), 'original_map': original,
              'backups': [], 'saved_assets': [], 'created_assets': [], 'placement': placement,
              'ground_trace_diagnostics': [],
              'build_started': False, 'pie_started': False, 'network_test_started': False}
    source_file = package_file(root, SELECTOR_SOURCE, '.umap')
    source_hash = digest(source_file)
    try:
        destinations = json.loads((HERE / 'destinations.json').read_text(encoding='utf-8'))
        texts = json.loads((HERE / 'localization.json').read_text(encoding='utf-8'))
        report['destination_seed_rows'] = len(destinations)
        report['localization_seed_rows'] = len(texts)
        if action == 'plan':
            report['native_classes_loaded'] = {path: bool(u.load_class(None, path)) for path in (MODE, ZONE)}
            report['native_row_loaded'] = bool(u.load_object(None, ROW))
            report['planned_packages'] = [HUB, SELECTOR, DESTINATIONS, TEXTS]
            report['optional_owned_material'] = READY_MATERIAL
            report['status'] = 'planned'
            return report
        mode = u.load_class(None, MODE)
        zone = u.load_class(None, ZONE)
        row = u.load_object(None, ROW)
        require(mode and zone and row, 'Load the rebuilt Editor with native Hub classes before applying')
        # Back up every existing target before the first package mutation/save.
        for package in (HUB, SELECTOR, DESTINATIONS, TEXTS, READY_MATERIAL):
            backup_package(root, package, output, report)
        require(any(item['source'].endswith('Content/Maps/Hub.umap') or item['source'].endswith('Content\\Maps\\Hub.umap')
                    for item in report['backups']), 'Existing Hub map must have a verified backup')
        report['selector_source_before_sha256'] = source_hash
        checkpoint(report, 'target_backups_verified')
        material = ready_material(u, report)
        checkpoint(report, 'ready_material_verified')
        author_hub(u, mode, zone, placement, material, report)
        checkpoint(report, 'hub_authored')
        author_selector(u, mode, report)
        checkpoint(report, 'selector_authored')
        author_destinations(u, row, destinations, report)
        checkpoint(report, 'destinations_authored')
        append_localization(u, texts, report)
        checkpoint(report, 'localization_appended')
        require(digest(source_file) == source_hash, 'Original selector map unexpectedly changed')
        require(not dirty_packages(u), 'Authoring left unsaved packages; review before closing: ' + str(dirty_packages(u)))
        current_map = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world().get_path_name().split('.', 1)[0]
        desired_map = original if restore_original else HUB
        if desired_map != current_map:
            # A clone was loaded as an asset in this process. Avoid a map switch
            # that could hit the editor's standalone-world leak assertion.
            report['restore_map_deferred_to_fresh_process'] = desired_map
        report['final_editor_map'] = current_map
        report['selector_source_after_sha256'] = digest(source_file)
        report['dirty_after'] = dirty_packages(u)
        report['status'] = 'passed'
        checkpoint(report, 'authoring_completed')
        return report
    except Exception as exc:
        report.update(status='failed', error=str(exc), traceback=traceback.format_exc(),
                      rollback_requires_editor_review=True)
        raise
    finally:
        write_json(output / 'Report.json', report)
        u.log('MC_HUB_AUTHORING ' + json.dumps({key: report.get(key) for key in ('status', 'output', 'error')}))


if __name__ == '__main__':
    run(**globals().get('MC_HUB_AUTHORING_OPTIONS', {}))
