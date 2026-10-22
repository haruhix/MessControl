"""Import and stage the approved, posed menu sketch without gameplay changes.

Run only in root's already owned Editor, with PIE stopped and no dirty packages.
Default invocation writes a read-only plan.  Apply is deliberately two phases:

    runpy.run_path(SCRIPT, init_globals={
        'MC_MENU_SHOWCASE_APPLY': 'menu_showcase_static_v1',
        'MC_MENU_SHOWCASE_PHASE': 'import_only',
        'MC_MENU_SHOWCASE_APPROVED_FBX_SHA256': '<reviewed Stage05 FBX hash>',
        'MC_MENU_SHOWCASE_OUT': 'E:/CodexWork/.../StageImport01',
    })

Review ImportReport.json and the imported bounds, then use phase='assemble',
the same source hash, and MC_MENU_SHOWCASE_COORDINATE_BASIS equal to the reported
'identity' or 'flip_y'.  Assembly never guesses handedness.  Variant A owns only
/Game/Maps/L_MainMenu; optional variant B owns a separate preview map and its
camera has auto-activation disabled.  No Blueprint/C++ Tick, PIE, build, cook,
package, global render CVars, save-all, or gameplay asset modifications occur.
"""
from __future__ import annotations

from datetime import datetime, timezone
import hashlib
import json
import math
from pathlib import Path
import re
import shutil
import traceback

import unreal as u


ROOT = Path('E:/DEVGAME/MessControl')
REVISION = 'menu_showcase_static_v1'
OWNER_KEY = 'MC_MenuShowcaseStage'
OWNER_TAG = 'MC_MenuShowcase_OwnedV1'
HANDOFF_DEFAULT = ROOT / 'ArtSource/UI/MenuShowcase_20261021/Blender/UnrealHandoff/Stage05.json'
MAIN_MAP = '/Game/Maps/L_MainMenu'
PREVIEW_MAP = '/Game/UI/MenuShowcase/Preview/L_MenuShowcase_B'
MODE_PATH = '/Script/MessControl.MCMainMenuGameMode'
CONTROLLER_PATH = '/Script/MessControl.MCMainMenuPlayerController'
MATERIALS = {
    'Material_010': '/Game/Gameplay/CharacterCurrent/Materials/MI_Character',
    'Material_003': '/Game/Gameplay/CharacterCurrent/Materials/MI_Bag',
    'Material_006': '/Game/Gameplay/CharacterCurrent/Tools/Materials/MI_Brush',
    'Material_Tongue': '/Game/Gameplay/Arena/MI_TonguePain',
    'Body': '/Game/Gameplay/CharacterCurrent/Materials/MI_Character',
    'Bag': '/Game/Gameplay/CharacterCurrent/Materials/MI_Bag',
    'Brush': '/Game/Gameplay/CharacterCurrent/Tools/Materials/MI_Brush',
    'Tongue': '/Game/Gameplay/Arena/MI_TonguePain',
}
TOOTH_ASSET = '/Game/Gameplay/Arena/SM_ArenaTooth_05'
SHELL_ASSET = '/Game/Art/Meshes/SM_MouthShell'
PALATE_MATERIAL = '/Game/Art/Materials/MI_MouthPalate'


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def e_path(value):
    result = Path(value).resolve()
    require(result.drive.upper() == 'E:', 'Task paths must be on E: ' + str(result))
    return result


def package_name(value):
    return str(value).split('.', 1)[0]


def package_file(package, extension='.uasset'):
    require(package.startswith('/Game/'), 'Only /Game packages are authored')
    return ROOT / 'Content' / (package.removeprefix('/Game/') + extension)


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def path_name(value):
    return value.get_path_name() if value else None


def enum(enum_type, member):
    normalized = re.sub(r'[^a-z0-9]', '', member.lower())
    matches = [name for name in dir(enum_type)
               if re.sub(r'[^a-z0-9]', '', name.lower()) == normalized]
    require(len(matches) == 1, 'Cannot resolve enum ' + str(enum_type) + '.' + member)
    return getattr(enum_type, matches[0])


def vec(value):
    require(len(value) == 3 and all(math.isfinite(float(x)) for x in value), 'Invalid vector')
    return u.Vector(*[float(x) for x in value])


def xyz(value):
    return [float(value.x), float(value.y), float(value.z)]


def transform_point(value, basis):
    sign = -1.0 if basis == 'flip_y' else 1.0
    return [float(value[0]), sign * float(value[1]), float(value[2])]


def dirty_packages():
    return sorted(p.get_path_name() for p in
                  list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages()) +
                  list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages()))


def require_idle_clean():
    levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
    require(not levels.is_in_play_in_editor(), 'Stop PIE before menu stage authoring')
    dirty = dirty_packages()
    require(not dirty, 'Preserve all unsaved packages: ' + ', '.join(dirty))


def own_asset(asset):
    return u.EditorAssetLibrary.get_metadata_tag(asset, OWNER_KEY) == REVISION


def mark_asset(asset, source_hash, role=None):
    u.EditorAssetLibrary.set_metadata_tag(asset, OWNER_KEY, REVISION)
    u.EditorAssetLibrary.set_metadata_tag(asset, 'MC_MenuShowcaseSourceSHA256', source_hash)
    if role:
        u.EditorAssetLibrary.set_metadata_tag(asset, 'MC_MenuShowcaseRole', role)


def backup_file(source, out, report):
    source = source.resolve()
    if not source.exists():
        return
    require(source.is_relative_to(ROOT.resolve()), 'Backup source must stay in this project')
    destination = out / 'BeforeFiles' / source.relative_to(ROOT.resolve())
    require(not destination.exists(), 'Do not overwrite a previous backup: ' + str(destination))
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)
    require(sha256(source) == sha256(destination), 'Backup SHA mismatch: ' + str(source))
    report['backups'].append({'source': str(source), 'backup': str(destination), 'sha256': sha256(source)})


def backup_package(package, out, report, is_map=False):
    extension = '.umap' if is_map else '.uasset'
    primary = package_file(package, extension)
    for suffix in (extension, '.uexp', '.ubulk'):
        backup_file(primary.with_suffix(suffix), out, report)


def load_handoff(path):
    require(path.is_file(), 'Posed source handoff not ready: ' + str(path))
    data = json.loads(path.read_text(encoding='utf-8-sig'))
    require(data.get('roles') and data.get('camera'), 'Handoff requires roles and camera')
    roles = [entry['role'] for entry in data['roles']]
    require(len(roles) == len(set(roles)), 'Duplicate source role')
    require({'Hero', 'Brush'} <= set(roles), 'Handoff must contain the actual Hero and Brush')
    require(not set(roles) - {'Hero', 'Brush', 'Tongue'}, 'Unexpected additional character/geometry role')
    for entry in data['roles']:
        require(entry.get('baked_world_vertices_cm') is True, 'All role vertices must be baked world cm')
        require(entry.get('actor_location_cm') == [0, 0, 0], 'Posed role actor location must be identity')
        require(entry.get('actor_rotation_deg') == [0, 0, 0], 'Posed role actor rotation must be identity')
        require(entry.get('actor_scale') == [1, 1, 1], 'Posed role actor scale must be identity')
        require(len(entry.get('bounds_cm', [])) == 2, 'Missing expected bounds for ' + entry['role'])
    fbx = e_path(data['fbx'])
    require(fbx.is_file(), 'FBX missing: ' + str(fbx))
    require(sha256(fbx) == str(data['fbx_sha256']).lower(), 'Source FBX changed since handoff')
    return data, fbx


def mesh_bounds(mesh):
    bounds = mesh.get_bounds()
    center = xyz(bounds.origin)
    extent = xyz(bounds.box_extent)
    return [[center[i] - extent[i] for i in range(3)],
            [center[i] + extent[i] for i in range(3)]]


def reflected_bounds(bounds, basis):
    if basis == 'identity':
        return bounds
    return [[bounds[0][0], -bounds[1][1], bounds[0][2]],
            [bounds[1][0], -bounds[0][1], bounds[1][2]]]


def infer_basis(role_meshes, handoff, tolerance=1.5):
    actual = {role: mesh_bounds(mesh) for role, mesh in role_meshes.items()}
    errors = {}
    for basis in ('identity', 'flip_y'):
        errors[basis] = max(abs(actual[entry['role']][corner][axis] -
                               reflected_bounds(entry['bounds_cm'], basis)[corner][axis])
                            for entry in handoff['roles'] for corner in range(2) for axis in range(3))
    matches = [basis for basis, error in errors.items() if error <= tolerance]
    require(len(matches) == 1,
            'Import axes/scale not uniquely verified; do not stage. max errors cm=' + str(errors))
    return {'basis': matches[0], 'actual_bounds_cm': actual, 'max_bounds_error_cm': errors,
            'tolerance_cm': tolerance, 'status': 'Bounds verify a candidate basis; root must review orientation before assemble'}


def scope_assets(import_base):
    return [u.load_asset(path) for path in
            u.EditorAssetLibrary.list_assets(import_base, recursive=True, include_folder=False)]


def material_for_slot(name, handoff):
    if name in MATERIALS:
        return MATERIALS[name]
    requested = handoff.get('materials', {}).get(name, '')
    match = re.match(r'(/Game/[^\s\)]+)', str(requested))
    return match.group(1) if match else None


def apply_materials(mesh, entry, handoff, report):
    slots = mesh.get_editor_property('static_materials')
    actual_names = [str(slot.get_editor_property('material_slot_name')) for slot in slots]
    require(len(slots) == len(entry['material_slots']), 'Slot count drift: ' + entry['role'])
    applied = []
    for index, name in enumerate(actual_names):
        # Legacy FBX normally preserves the native slot names.  Index fallback
        # is allowed only when the count matches the explicitly authored list.
        source_name = name if material_for_slot(name, handoff) else entry['material_slots'][index]
        material_path = material_for_slot(source_name, handoff)
        require(material_path, 'Unknown material slot ' + name + ' on ' + entry['role'])
        material = u.load_asset(material_path)
        require(isinstance(material, u.MaterialInterface), 'Missing original material ' + material_path)
        mesh.set_material(index, material)
        require(mesh.get_material(index) == material, 'Material readback failed')
        applied.append({'slot': name, 'source_slot': source_name, 'index': index, 'material': path_name(material)})
    report['restored_materials'][entry['role']] = applied


def get_or_import(handoff, fbx, import_base, out, report):
    lib = u.EditorAssetLibrary
    existing = scope_assets(import_base) if lib.does_directory_exist(import_base) else []
    require(all(asset and own_asset(asset) for asset in existing), 'Preserve unowned assets in ' + import_base)
    source_hash = sha256(fbx)
    cached = {lib.get_metadata_tag(asset, 'MC_MenuShowcaseRole'): asset for asset in existing
              if isinstance(asset, u.StaticMesh) and
              lib.get_metadata_tag(asset, 'MC_MenuShowcaseSourceSHA256') == source_hash}
    expected_roles = {entry['role'] for entry in handoff['roles']}
    if expected_roles <= set(cached):
        result = {role: cached[role] for role in expected_roles}
        for mesh in result.values():
            backup_package(package_name(mesh.get_path_name()), out, report)
        report['import_reused'] = True
    else:
        for asset in existing:
            backup_package(package_name(asset.get_path_name()), out, report)
        task = u.AssetImportTask()
        task.set_editor_properties({
            'filename': str(fbx), 'destination_path': import_base,
            'destination_name': 'MenuShowcasePosed', 'automated': True,
            'replace_existing': bool(existing), 'replace_existing_settings': True,
            'save': False, 'factory': u.FbxFactory(),
        })
        options = u.FbxImportUI()
        options.set_editor_properties({
            'automated_import_should_detect_type': False,
            'mesh_type_to_import': u.FBXImportType.FBXIT_STATIC_MESH,
            'import_mesh': True, 'import_as_skeletal': False,
            'import_materials': False, 'import_textures': False, 'import_animations': False,
        })
        options.static_mesh_import_data.set_editor_properties({
            'combine_meshes': False, 'auto_generate_collision': False,
            'convert_scene': True, 'convert_scene_unit': True, 'force_front_x_axis': False,
            'normal_import_method': u.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS,
            'import_uniform_scale': 1.0,
        })
        task.options = options
        # UE5.8 can route an FBX through Interchange despite FbxFactory.  Scope
        # the legacy flag to this one import and restore the exact process value.
        flag = 'Interchange.FeatureFlags.Import.FBX'
        before_flag = int(u.SystemLibrary.get_console_variable_int_value(flag))
        report['fbx_import_route'] = {'flag': flag, 'before': before_flag, 'temporary': 0}
        try:
            u.SystemLibrary.execute_console_command(None, flag + ' 0')
            require(int(u.SystemLibrary.get_console_variable_int_value(flag)) == 0,
                    'Could not select legacy FBX importer for explicit FbxImportUI options')
            u.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
        finally:
            u.SystemLibrary.execute_console_command(None, flag + ' ' + str(before_flag))
            restored = int(u.SystemLibrary.get_console_variable_int_value(flag))
            report['fbx_import_route']['restored'] = restored
            require(restored == before_flag, 'FBX process flag restoration failed')
        imported = [u.load_asset(path) for path in task.get_editor_property('imported_object_paths')]
        require(imported and all(isinstance(asset, u.StaticMesh) for asset in imported),
                'Expected only posed static meshes from approved FBX')
        require(all(package_name(asset.get_path_name()).startswith(import_base + '/') for asset in imported),
                'Importer wrote outside its owned namespace')
        result = {}
        for entry in handoff['roles']:
            matches = [asset for asset in imported if
                       asset.get_name() == entry['mesh_name'] or asset.get_name().endswith('_' + entry['mesh_name'])]
            require(len(matches) == 1,
                    'Cannot uniquely match FBX node ' + entry['mesh_name'] + ': ' + str([x.get_name() for x in imported]))
            result[entry['role']] = matches[0]
        require(len(imported) == len(result), 'Unexpected extra imported FBX node')
        report['import_reused'] = False
    for entry in handoff['roles']:
        mesh = result[entry['role']]
        apply_materials(mesh, entry, handoff, report)
        mark_asset(mesh, source_hash, entry['role'])
    report['imported_meshes'] = {role: path_name(mesh) for role, mesh in result.items()}
    report['orientation'] = infer_basis(result, handoff)
    return result


def save_owned_meshes(meshes, import_base, report):
    for mesh in meshes.values():
        require(own_asset(mesh) and package_name(mesh.get_path_name()).startswith(import_base + '/'),
                'Refusing to save an unowned mesh')
        require(u.EditorAssetLibrary.save_loaded_asset(mesh, only_if_is_dirty=False),
                'Could not save posed mesh ' + path_name(mesh))
        report['saved_packages'].append(package_name(mesh.get_path_name()))


def tag_actor(actor, role, report):
    label = 'MC_MenuShowcase_' + role
    actor.set_actor_label(label)
    actor.set_editor_property('tags', [u.Name(OWNER_TAG), u.Name(label)])
    actor.set_folder_path('MenuShowcase/' + report['variant'])
    report['actors'].append({'role': role, 'label': label, 'path': path_name(actor)})
    return actor


def spawn_static(mesh, role, location, rotation, scale, actors, report):
    actor = tag_actor(actors.spawn_actor_from_class(u.StaticMeshActor, vec(location), rotation), role, report)
    component = actor.get_component_by_class(u.StaticMeshComponent)
    component.set_static_mesh(mesh)
    component.set_mobility(u.ComponentMobility.STATIC)
    component.set_collision_enabled(u.CollisionEnabled.NO_COLLISION)
    component.set_editor_property('cast_shadow', True)
    actor.set_actor_scale3d(vec(scale))
    actor.set_actor_tick_enabled(False)
    return actor


def source_rotation(entry, basis):
    if 'rotation_unreal_pyr_deg' in entry:
        values = entry['rotation_unreal_pyr_deg']
        return u.Rotator(pitch=float(values[0]), yaw=float(values[1]), roll=float(values[2]))
    values = entry.get('rotation_xyz_deg', [0, 0, 0])
    require(max(abs(float(value)) for value in values) < 1e-5,
            'Nonzero DCC Euler anatomy rotation needs explicit Unreal conversion; do not guess: ' + entry['name'])
    return u.Rotator(0, 0, 0)


def normalized(value):
    length = math.sqrt(sum(float(x)*float(x) for x in value))
    require(length > 1e-8, 'Degenerate contact frame vector')
    return [float(x)/length for x in value]


def contact_frame(handoff, basis):
    anchors = handoff.get('anchors', {})
    require('brush_contact_cm' in anchors and 'brush_outward_normal' in anchors,
            'Coffee look requires the actual brush contact and outward-normal anchors')
    point = anchors.get('tooth_surface_point_cm', anchors['brush_contact_cm'])
    outward = anchors.get('tooth_surface_normal', anchors['brush_outward_normal'])
    if handoff['fbx_sha256'] == '3965748e6d5db8ca8ea84528d6c7fedc8acef0ec8eeafb35b5a5201bc6e56725':
        # BASE566 nearest-surface query from the unchanged Blender v05 source.
        # Brush direction differs by ~74 degrees and is NOT the enamel normal.
        point = [283.66241, -35.13983, 103.99044]
        outward = [-0.3243440, -0.9437472, -0.0643595]
    contact = transform_point(point, basis)
    normal = normalized(transform_point(outward, basis))
    vertical = normalized([-normal[0]*normal[2], -normal[1]*normal[2], 1-normal[2]*normal[2]])
    tangent = normalized([vertical[1]*normal[2]-vertical[2]*normal[1],
                          vertical[2]*normal[0]-vertical[0]*normal[2],
                          vertical[0]*normal[1]-vertical[1]*normal[0]])
    return contact, normal, tangent, vertical


COFFEE_MASK_CODE = r'''
float3 d = P - Contact.xyz;
float2 q = float2(dot(d,TangentU.xyz),dot(d,TangentV.xyz));
float depth = abs(dot(d,Outward.xyz));
float noise = sin(q.x*.117 + sin(q.y*.083)*1.45)*.071
            + sin(q.y*.193 + q.x*.067)*.049
            + sin(q.x*.347-q.y*.231)*.022;
float radial = length((q+float2(5.,-4.)) / float2(43.,58.));
float edgeDistance = (1.-radial+noise)*43.;
float coverage = smoothstep(-1.2,1.4,edgeDistance);
coverage *= 1.-smoothstep(24.,52.,depth);
float2 s = q-float2(sin(q.y*.16)*.65,-4.);
float capsule = length(float2(s.x,max(abs(s.y)-7.,0.)));
float streak = 1.-smoothstep(8.2,10.0,capsule);
float fine = .91+.09*sin(q.x*.31+sin(q.y*.23));
return saturate(coverage*fine*(1.-streak*.91)*.90);
'''


def author_coffee_material(handoff, basis, variant, out, report):
    """Clone enamel, preserve its graph, and add a thin contact-centered film.

    No opacity, clip/discard, displaced mesh, decal quad or flat disk is used.
    This is a static look-dev state.  A future authored LevelSequence can key
    its exposed parameters without actor Tick or gameplay-material changes.
    """
    edit, lib = u.MaterialEditingLibrary, u.EditorAssetLibrary
    base = '/Game/UI/MenuShowcase/Materials/' + variant
    destination = base + '/M_MenuEnamelCoffee'
    source = u.load_asset('/Game/Art/Materials/M_Enamel')
    require(isinstance(source, u.Material), 'Expected existing enamel master material')
    contact, normal, tangent, vertical = contact_frame(handoff, basis)
    existed = lib.does_asset_exist(destination)
    material = u.load_asset(destination) if existed else None
    if existed:
        require(own_asset(material), 'Preserve unowned menu enamel material')
        backup_package(destination, out, report)
        require(lib.get_metadata_tag(material, 'MC_MenuShowcaseCoffeeGraph') == 'world_mask_v1',
                'Existing own coffee graph needs explicit migration; do not append layers')
    else:
        material = lib.duplicate_asset('/Game/Art/Materials/M_Enamel', destination)
        require(isinstance(material, u.Material), 'Could not clone source enamel')
        mark_asset(material, report['fbx_sha256'])

    def node(kind, name, **properties):
        expression = edit.create_material_expression(material, kind, 900, 0)
        require(expression, 'Could not create menu enamel node ' + name)
        expression.set_editor_property('desc', name)
        for key, value in properties.items():
            expression.set_editor_property(key, value)
        return expression

    def connect(a, b, pin, output=''):
        require(edit.connect_material_expressions(a, output, b, pin), 'Material connection failed: ' + pin)

    def output(a, property_name):
        require(edit.connect_material_property(a, '', property_name), 'Material output connection failed')

    existing_nodes = list(edit.get_material_expressions(material))
    parameters = {str(n.get_editor_property('parameter_name')): n for n in existing_nodes
                  if isinstance(n, (u.MaterialExpressionVectorParameter, u.MaterialExpressionScalarParameter))}

    def parameter(name, value):
        expression = parameters.get(name)
        if expression is None:
            require(not existed, 'Own coffee graph lost parameter ' + name)
            expression = node(u.MaterialExpressionVectorParameter, name, parameter_name=name)
        expression.set_editor_property('default_value', u.LinearColor(*value, 1.0))
        return expression

    contact_node = parameter('MenuBrushContact', contact)
    normal_node = parameter('MenuBrushOutward', normal)
    tangent_node = parameter('MenuCoffeeTangentU', tangent)
    vertical_node = parameter('MenuCoffeeTangentV', vertical)
    tint = parameter('MenuCoffeeTint', [0.105, 0.038, 0.012])
    if existed:
        matches = [n for n in existing_nodes if isinstance(n, u.MaterialExpressionCustom)
                   and n.get_editor_property('description') == 'MC_MenuShowcase_CoffeeMask_v1']
        require(len(matches) == 1, 'Own coffee graph mask missing or duplicated')
        mask = matches[0]
        mask.set_editor_property('code', COFFEE_MASK_CODE)
    else:
        base_color = edit.get_material_property_input_node(material, u.MaterialProperty.MP_BASE_COLOR)
        require(base_color, 'Source enamel has no direct BaseColor graph; root must inspect Substrate/attributes before authoring')
        base_color_output = edit.get_material_property_input_node_output_name(material, u.MaterialProperty.MP_BASE_COLOR)
        base_roughness = edit.get_material_property_input_node(material, u.MaterialProperty.MP_ROUGHNESS)
        roughness_output = edit.get_material_property_input_node_output_name(material, u.MaterialProperty.MP_ROUGHNESS)
        if base_roughness is None:
            base_roughness = node(u.MaterialExpressionConstant, 'MC_MenuOriginalRoughnessFallback', r=0.32)
        world_position = node(u.MaterialExpressionWorldPosition, 'MC_MenuCoffeeWorldPosition')
        mask = node(u.MaterialExpressionCustom, 'MC_MenuCoffeeMask',
                    description='MC_MenuShowcase_CoffeeMask_v1', code=COFFEE_MASK_CODE,
                    output_type=enum(u.CustomMaterialOutputType, 'CMOT_Float1'))
        input_names = ['P', 'Contact', 'Outward', 'TangentU', 'TangentV']
        inputs = []
        for name in input_names:
            item = u.CustomInput()
            item.set_editor_property('input_name', name)
            inputs.append(item)
        mask.set_editor_property('inputs', inputs)
        for expression, name in ((world_position,'P'), (contact_node,'Contact'),
                                 (normal_node,'Outward'), (tangent_node,'TangentU'), (vertical_node,'TangentV')):
            connect(expression, mask, name)
        color_blend = node(u.MaterialExpressionLinearInterpolate, 'MC_MenuThinCoffeeColor')
        connect(base_color, color_blend, 'A', base_color_output)
        connect(tint, color_blend, 'B', 'RGB')
        connect(mask, color_blend, 'Alpha')
        output(color_blend, u.MaterialProperty.MP_BASE_COLOR)
        coffee_roughness = node(u.MaterialExpressionConstant, 'MC_MenuCoffeeSatinRoughness', r=0.42)
        rough_blend = node(u.MaterialExpressionLinearInterpolate, 'MC_MenuThinCoffeeRoughness')
        connect(base_roughness, rough_blend, 'A', roughness_output)
        connect(coffee_roughness, rough_blend, 'B')
        connect(mask, rough_blend, 'Alpha')
        output(rough_blend, u.MaterialProperty.MP_ROUGHNESS)
        lib.set_metadata_tag(material, 'MC_MenuShowcaseCoffeeGraph', 'world_mask_v1')
    mark_asset(material, report['fbx_sha256'])
    compile_errors = [str(value) for value in edit.recompile_material(material)]
    report['coffee_shader_errors'] = compile_errors
    require(not compile_errors, 'Own menu coffee material shader errors: ' + ' | '.join(compile_errors))
    require(lib.save_loaded_asset(material, only_if_is_dirty=False), 'Could not save own menu coffee material')
    report['saved_packages'].append(destination)
    report['coffee_material'] = {
        'source_preserved': source.get_path_name(), 'own_clone': material.get_path_name(),
        'brush_contact_ue_cm': contact, 'outward_ue': normal,
        'tangent_u_ue': tangent, 'tangent_v_ue': vertical,
        'shape': 'Irregular softly feathered thin ellipse on the existing tooth surface, with a clean streak under the bristles',
        'radii_cm': [43,58], 'depth_falloff_cm': [19,43],
        'clean_streak_width_cm': [17,27], 'coffee_roughness': 0.42,
        'no_opacity_or_geometry_changes': True, 'state': 'Static authored look-dev frame, not a claimed runtime cleaning animation',
        'shader_compilation': 'UE recompile_material returned zero errors; rendered look still requires root inspection',
    }
    return material


def repair_stage_look(handoff, basis, variant, target_map, out, report):
    """Repair only presentation materials, exposure and contact shadows.

    Keep the reviewed geometry/camera and gameplay materials unchanged.  The
    separate background re-staging is intentionally not mixed into this pass.
    """
    levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
    world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
    require(package_name(world.get_path_name()) == target_map, 'Open the authored menu map before look repair')
    actors = u.get_editor_subsystem(u.EditorActorSubsystem)
    owned = {a.get_actor_label(): a for a in actors.get_all_level_actors()
             if OWNER_TAG in [str(t) for t in a.tags]}
    require(all('MC_MenuShowcase_'+name in owned for name in
                ('Hero','Tongue','Brush','ExistingArenaTooth_Hero','Camera','WarmKey','CoolFill','SoftRim')),
            'Expected authored stage actors are missing')
    mode = world.get_world_settings().get_editor_property('default_game_mode')
    require(path_name(mode) == MODE_PATH, 'Preserve native main-menu GameMode')
    role_meshes = {role: owned['MC_MenuShowcase_'+role].get_component_by_class(u.StaticMeshComponent).get_editor_property('static_mesh')
                   for role in ('Hero','Brush','Tongue')}
    report['orientation'] = infer_basis(role_meshes, handoff)
    require(report['orientation']['basis'] == basis, 'Explicit root-reviewed basis does not match loaded meshes')
    backup_package(target_map, out, report, is_map=True)
    base = '/Game/UI/MenuShowcase/Materials/' + variant
    lib, edit = u.EditorAssetLibrary, u.MaterialEditingLibrary

    def prepare(name, cls, factory, source=None):
        destination = base+'/'+name
        asset = u.load_asset(destination) if lib.does_asset_exist(destination) else None
        if asset:
            require(own_asset(asset), 'Preserve unowned material '+destination)
            backup_package(destination, out, report)
        else:
            asset = lib.duplicate_asset(source, destination) if source else u.AssetToolsHelpers.get_asset_tools().create_asset(name,base,cls,factory)
        require(isinstance(asset,cls), 'Could not create own presentation material '+destination)
        mark_asset(asset, report['fbx_sha256'])
        return asset

    def save(asset):
        require(own_asset(asset), 'Save only owned look material')
        if isinstance(asset,u.Material):
            errors=[str(e) for e in edit.recompile_material(asset)]
            report.setdefault('look_shader_errors',{})[asset.get_name()]=errors
            require(not errors,'Look material shader errors: '+' | '.join(errors))
        require(lib.save_loaded_asset(asset,only_if_is_dirty=False),'Own look material save failed')
        report['saved_packages'].append(package_name(asset.get_path_name()))

    tongue=prepare('M_MenuTongue',u.Material,u.MaterialFactoryNew())
    tongue.set_editor_property('shading_model',u.MaterialShadingModel.MSM_DEFAULT_LIT)
    tongue.set_editor_property('blend_mode',u.BlendMode.BLEND_OPAQUE)
    existing_nodes=list(edit.get_material_expressions(tongue))
    by_desc={str(n.get_editor_property('desc')):n for n in existing_nodes}
    def node(cls,name,**props):
        n=by_desc.get(name)
        if n is None:
            n=edit.create_material_expression(tongue,cls)
            n.set_editor_property('desc',name)
        require(isinstance(n,cls),'Unexpected own tongue graph node '+name)
        for key,value in props.items():n.set_editor_property(key,value)
        return n
    def connect(a,b,pin,output=''):
        names=[str(value) for value in edit.get_material_expression_input_names(b)]
        if pin=='Input' and 'None' in names:
            pin=''
        elif pin not in names and len(names)==1:
            pin=names[0]
        if pin=='None':
            pin=''
        require(edit.connect_material_expressions(a,output,b,pin),'Tongue material connection failed '+pin)
    def output(a,property_name,channel=''):
        require(edit.connect_material_property(a,channel,property_name),'Tongue material output failed')

    live=u.load_asset('/Game/Gameplay/Arena/MI_TonguePain')
    textures={}
    if isinstance(live,u.MaterialInstanceConstant):
        for name in edit.get_texture_parameter_names(live):
            texture=edit.get_material_instance_texture_parameter_value(live,name)
            if texture:textures[str(name)]=texture
    def texture_pick(names,fallback):
        for name in names:
            if name in textures and not textures[name].get_path_name().startswith('/Engine/'):
                return textures[name]
        result=u.load_asset(fallback)
        require(isinstance(result,u.Texture2D),'No existing tongue texture for '+str(names))
        return result
    color_texture=texture_pick(('Albedo Texture','BaseColorTexture','BaseColor','Albedo'),
       '/Game/UI/MainMenu3D/ReferenceFinish/Textures/DAY03/Interior/T_DAY03_Tongue_BaseColor_v08')
    normal_texture=texture_pick(('Normal Texture','NormalTexture','Normal'),
       '/Game/UI/MainMenu3D/ReferenceFinish/Textures/DAY03/Interior/T_DAY03_Tongue_Normal_v08')
    tex=node(u.MaterialExpressionTextureSample,'MC_MenuTongueColorTexture',texture=color_texture,
             sampler_type=u.MaterialSamplerType.SAMPLERTYPE_COLOR)
    normal_tex=node(u.MaterialExpressionTextureSample,'MC_MenuTongueNormalTexture',texture=normal_texture,
                    sampler_type=u.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    tint=node(u.MaterialExpressionConstant3Vector,'MC_MenuTongueSoftPink',constant=u.LinearColor(.32,.065,.095,1))
    luma=node(u.MaterialExpressionDesaturation,'MC_MenuTongueAlbedoPattern')
    connect(tex,luma,'Input','RGB')
    fraction=node(u.MaterialExpressionConstant,'MC_MenuTongueDesaturate',r=1.0);connect(fraction,luma,'Fraction')
    # Retain delicate source pigmentation; clamp it around the authored pink.
    pattern=node(u.MaterialExpressionLinearInterpolate,'MC_MenuTonguePatternStrength')
    constant=node(u.MaterialExpressionConstant3Vector,'MC_MenuTonguePatternNeutral',constant=u.LinearColor(1,1,1,1))
    strength=node(u.MaterialExpressionConstant,'MC_MenuTonguePatternAmount',r=.22)
    connect(constant,pattern,'A');connect(luma,pattern,'B');connect(strength,pattern,'Alpha')
    color=node(u.MaterialExpressionMultiply,'MC_MenuTongueColor');connect(tint,color,'A');connect(pattern,color,'B')
    output(color,u.MaterialProperty.MP_BASE_COLOR)
    flat=node(u.MaterialExpressionConstant3Vector,'MC_MenuTongueFlatNormal',constant=u.LinearColor(0,0,1,1))
    normal_blend=node(u.MaterialExpressionLinearInterpolate,'MC_MenuTongueNormalStrength')
    normal_strength=node(u.MaterialExpressionConstant,'MC_MenuTongueNormalAmount',r=.32)
    connect(flat,normal_blend,'A');connect(normal_tex,normal_blend,'B','RGB');connect(normal_strength,normal_blend,'Alpha')
    normal=node(u.MaterialExpressionNormalize,'MC_MenuTongueNormal');connect(normal_blend,normal,'VectorInput')
    output(normal,u.MaterialProperty.MP_NORMAL)
    rough=node(u.MaterialExpressionConstant,'MC_MenuTongueRoughness',r=.65);output(rough,u.MaterialProperty.MP_ROUGHNESS)
    spec=node(u.MaterialExpressionConstant,'MC_MenuTongueSpecular',r=.25);output(spec,u.MaterialProperty.MP_SPECULAR)
    zero=node(u.MaterialExpressionConstant3Vector,'MC_MenuTongueNoEmissionOrWPO',constant=u.LinearColor(0,0,0,1))
    output(zero,u.MaterialProperty.MP_EMISSIVE_COLOR);output(zero,u.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    save(tongue)

    bag=prepare('MI_MenuBag',u.MaterialInstanceConstant,u.MaterialInstanceConstantFactoryNew())
    parent=u.load_asset('/Game/Gameplay/CharacterCurrent/Materials/MI_Bag')
    require(parent,'Original Bag material missing')
    edit.set_material_instance_parent(bag,parent)
    edit.set_material_instance_scalar_parameter_value(bag,'Saturate',1.0)
    edit.set_material_instance_vector_parameter_value(bag,'Albedo Color',u.LinearColor(.025,.18,.245,1))
    edit.update_material_instance(bag);save(bag)

    clean=prepare('M_MenuEnamelClean',u.Material,u.MaterialFactoryNew(),'/Game/Art/Materials/M_Enamel')
    coffee=author_coffee_material(handoff,basis,variant,out,report)
    original=u.load_asset('/Game/Art/Materials/M_Enamel')
    original_color=edit.get_material_property_input_node(original,u.MaterialProperty.MP_BASE_COLOR)
    original_rough=edit.get_material_property_input_node(original,u.MaterialProperty.MP_ROUGHNESS)
    color_name=str(original_color.get_editor_property('parameter_name'))
    rough_name=str(original_rough.get_editor_property('parameter_name'))
    for material in (clean,coffee):
        for n in edit.get_material_expressions(material):
            if isinstance(n,u.MaterialExpressionVectorParameter) and str(n.get_editor_property('parameter_name'))==color_name:
                n.set_editor_property('default_value',u.LinearColor(.68,.635,.56,1))
            if isinstance(n,u.MaterialExpressionScalarParameter) and str(n.get_editor_property('parameter_name'))==rough_name:
                n.set_editor_property('default_value',.40)
        save(material)
    with u.ScopedEditorTransaction('Repair menu static look and contact'):
        owned['MC_MenuShowcase_Tongue'].get_component_by_class(u.StaticMeshComponent).set_material(0,tongue)
        owned['MC_MenuShowcase_Hero'].get_component_by_class(u.StaticMeshComponent).set_material(1,bag)
        for label,actor in owned.items():
            if label.startswith('MC_MenuShowcase_ExistingArenaTooth_'):
                actor.get_component_by_class(u.StaticMeshComponent).set_material(0,coffee if label.endswith('_Hero') else clean)
        camera=owned['MC_MenuShowcase_Camera'].get_component_by_class(u.CameraComponent)
        pp=camera.get_editor_property('post_process_settings')
        values={'auto_exposure_bias':-1.1,'bloom_intensity':0.0,'ambient_occlusion_intensity':.85,
                'ambient_occlusion_radius':55.0,'ambient_occlusion_power':1.2,'ambient_occlusion_quality':85.0}
        for key,value in values.items():
            pp.set_editor_property('override_'+key,True);pp.set_editor_property(key,value)
        camera.set_editor_property('post_process_settings',pp)
        for role,intensity in (('WarmKey',66.0),('CoolFill',38.0),('SoftRim',40.0)):
            c=owned['MC_MenuShowcase_'+role].get_component_by_class(u.RectLightComponent)
            c.set_intensity(intensity)
            if role=='WarmKey':
                c.set_source_width(520.0);c.set_source_height(420.0)
                c.set_editor_property('contact_shadow_length',.10)
                c.set_editor_property('contact_shadow_length_in_ws',False)
                c.set_editor_property('shadow_bias',.28)
                c.set_editor_property('shadow_slope_bias',.4)
        require(levels.save_current_level(),'Could not save scoped look-repaired menu map')
        report['saved_packages'].append(target_map)
    report['look_repair']={'tongue_material':path_name(tongue),'tongue_albedo_texture':path_name(color_texture),
        'tongue_normal_texture':path_name(normal_texture),'live_tongue_texture_parameters':{n:path_name(t) for n,t in textures.items()},
        'roughness':.65,'normal_strength':.32,'wpo':False,'emission':False,
        'bag_material':path_name(bag),'bag_overrides':{'Saturate':1.0,'Albedo Color':[.025,.18,.245]},
        'enamel_base_linear_rgb':[.68,.635,.56],'enamel_roughness':.40,
        'exposure_bias':-1.1,'light_intensity_cd':{'WarmKey':66,'CoolFill':38,'SoftRim':40},
        'contact':'No geometry moved. DCC sole gaps L0.83cm/R0.43cm; removed floor WPO and added AO/contact shadows.',
        'visual_status':'Pending root native screenshot review, not a visual PASS',
        'background':'Unchanged in this focused first repair; perspective row/svod depth to be reviewed next'}


def repair_stage_background(handoff,basis,variant,target_map,out,report):
    """One scoped native background pass after the material correction."""
    repair_stage_look(handoff,basis,variant,target_map,out,report)
    es=u.get_editor_subsystem(u.EditorActorSubsystem)
    levels=u.get_editor_subsystem(u.LevelEditorSubsystem)
    lib,edit=u.EditorAssetLibrary,u.MaterialEditingLibrary
    owned={a.get_actor_label():a for a in es.get_all_level_actors() if OWNER_TAG in [str(t) for t in a.tags]}
    tongue_path='/Game/UI/MenuShowcase/Materials/'+variant+'/M_MenuTongue'
    mucosa_path='/Game/UI/MenuShowcase/Materials/'+variant+'/M_MenuMucosa'
    if lib.does_asset_exist(mucosa_path):
        mucosa=u.load_asset(mucosa_path);require(own_asset(mucosa),'Preserve unowned mucosa')
        backup_package(mucosa_path,out,report)
    else:
        mucosa=lib.duplicate_asset(tongue_path,mucosa_path)
    require(isinstance(mucosa,u.Material),'Could not create own matte mucosa')
    mark_asset(mucosa,report['fbx_sha256'])
    for n in edit.get_material_expressions(mucosa):
        desc=str(n.get_editor_property('desc'))
        if desc=='MC_MenuTongueSoftPink':n.set_editor_property('constant',u.LinearColor(.24,.047,.073,1))
        if desc=='MC_MenuTongueRoughness':n.set_editor_property('r',.73)
        if desc=='MC_MenuTongueNormalAmount':n.set_editor_property('r',.16)
    errors=[str(e) for e in edit.recompile_material(mucosa)]
    report.setdefault('look_shader_errors',{})['M_MenuMucosa']=errors
    require(not errors,'Mucosa shader errors: '+' | '.join(errors))
    require(lib.save_loaded_asset(mucosa,only_if_is_dirty=False),'Could not save own mucosa')
    report['saved_packages'].append(mucosa_path)
    for role in ('SculptedPalate','ExistingUvula','BackgroundFill'):
        old=owned.get('MC_MenuShowcase_'+role)
        if old:require(es.destroy_actor(old),'Could not replace own background actor')
    old_shell=owned.get('MC_MenuShowcase_ExistingMouthShell')
    if old_shell:
        old_shell.set_actor_hidden_in_game(True);old_shell.set_is_temporarily_hidden_in_editor(True)
    row_specs=[(0,[154.498316,-225,24]),(1,[74.509276,-375,18]),(2,[-.650659,-510,14])]
    projection=[]
    for index,location in row_specs:
        actor=owned['MC_MenuShowcase_ExistingArenaTooth_Back_'+str(index).zfill(2)]
        actor.set_actor_location(vec(location),False,True)
        projection.append({'actor':actor.get_actor_label(),'location_ue_cm':location,
                           'projected_center_uv':camera_projection(location,handoff,basis)})
    palate_mesh=u.load_asset('/Game/Gameplay/Throat/SM_SoftPalate')
    uvula_mesh=u.load_asset('/Game/Gameplay/Throat/SM_Uvula')
    require(isinstance(palate_mesh,u.StaticMesh) and isinstance(uvula_mesh,u.StaticMesh),'Existing anatomical meshes missing')
    # These assets face along native +X.  Yaw -90 aligns that depth with the
    # authored stage's -Y.  The uvula's large Y extent is its palatal ROOT;
    # its hanging tip is Z-down, so no roll/pitch rotation is applied.
    rotation=u.Rotator(pitch=0.,yaw=-90.,roll=0.)
    palate=spawn_static(palate_mesh,'SculptedPalate',[40,-750,140],rotation,[.50,.35,.35],es,report)
    uvula=spawn_static(uvula_mesh,'ExistingUvula',[0,-630,140],rotation,[.35,.24,.70],es,report)
    for actor in (palate,uvula):
        component=actor.get_component_by_class(u.StaticMeshComponent)
        for index in range(component.get_num_materials()):component.set_material(index,mucosa)
    light=tag_actor(es.spawn_actor_from_class(u.RectLight,vec([30,0,330])),'BackgroundFill',report)
    light.set_actor_rotation(u.MathLibrary.find_look_at_rotation(vec([30,0,330]),vec([0,-750,160])),False)
    comp=light.get_component_by_class(u.RectLightComponent)
    comp.set_mobility(u.ComponentMobility.MOVABLE);comp.set_intensity_units(u.LightUnits.CANDELAS)
    comp.set_intensity(28.0);comp.set_source_width(700.0);comp.set_source_height(450.0)
    comp.set_attenuation_radius(1800.0);comp.set_cast_shadows(False)
    comp.set_light_color(u.LinearColor(.88,.72,.80,1));comp.set_editor_property('specular_scale',.18)
    require(levels.save_current_level(),'Could not save only repaired menu map')
    report['background_repair']={'old_wall_hidden':bool(old_shell),'row':projection,
        'palate':{'source':'/Game/Gameplay/Throat/SM_SoftPalate','location':[40,-750,140],'yaw':-90,'scale':[.50,.35,.35]},
        'uvula':{'source':'/Game/Gameplay/Throat/SM_Uvula','location':[0,-630,140],'yaw':-90,'scale':[.35,.24,.70]},
        'mucosa_material':mucosa_path,'background_light_cd':28.0,
        'no_new_primitives_or_geometry_assets':True,'visual_status':'Pending root native frame03 review'}


def assemble_anatomy(handoff, meshes, basis, actors, report, coffee_material):
    entries = handoff.get('existing_anatomy', [])
    for entry in entries:
        name = entry['name']
        if 'Tongue' in name and 'Tongue' in meshes:
            continue  # The DCC contact-matched menu ground supersedes a mismatched live tongue.
        if 'ArenaTooth' in name:
            source = TOOTH_ASSET
        else:
            source = entry.get('source_asset', '')
            require(source.startswith('/Game/') and ' ' not in source, 'Unresolved anatomy asset: ' + str(source))
        mesh = u.load_asset(source)
        require(isinstance(mesh, u.StaticMesh), 'Missing verified anatomy mesh ' + source)
        location = transform_point(entry['location_cm'], basis)
        scale = entry.get('scale_vs_cm_ue_mesh', [1, 1, 1])
        anatomy_actor = spawn_static(mesh, name.removeprefix('GEO_'), location, source_rotation(entry, basis), scale, actors, report)
        if 'ArenaTooth_Hero' in name:
            anatomy_actor.get_component_by_class(u.StaticMeshComponent).set_material(0, coffee_material)
        report['anatomy'].append({'role': name, 'source_asset': source, 'location_cm': location, 'scale': scale})
    if bool(globals().get('MC_MENU_SHOWCASE_INCLUDE_SHELL', True)):
        shell = u.load_asset(SHELL_ASSET)
        require(isinstance(shell, u.StaticMesh), 'Missing existing mouth shell ' + SHELL_ASSET)
        shell_actor = spawn_static(shell, 'ExistingMouthShell', [0, 0, 0], u.Rotator(0, 0, 0),
                                   [1, 1, 1], actors, report)
        comp = shell_actor.get_component_by_class(u.StaticMeshComponent)
        fallback = u.load_asset(PALATE_MATERIAL)
        require(isinstance(fallback, u.MaterialInterface), 'Missing palate material for shell null slots')
        repaired = []
        for index, slot in enumerate(shell.get_editor_property('static_materials')):
            if slot.get_editor_property('material_interface') is None:
                comp.set_material(index, fallback)
                repaired.append(index)
        report['anatomy'].append({'role': 'ExistingMouthShell', 'source_asset': SHELL_ASSET,
                                  'placement': 'Existing native mouth shell identity, separate menu instance',
                                  'null_material_slot_component_overrides': repaired})


def author_camera(handoff, basis, variant, actors, report):
    source = handoff['camera']
    location = transform_point(source['location_cm'], basis)
    target = transform_point(source['target_cm'], basis)
    rotation = u.MathLibrary.find_look_at_rotation(vec(location), vec(target))
    camera = tag_actor(actors.spawn_actor_from_class(u.CameraActor, vec(location), rotation), 'Camera', report)
    camera.set_editor_property('auto_activate_for_player', enum(u.AutoReceiveInput, 'Player0' if variant == 'A' else 'Disabled'))
    comp = camera.get_component_by_class(u.CameraComponent)
    comp.set_projection_mode(u.CameraProjectionMode.PERSPECTIVE)
    comp.set_field_of_view(float(source['horizontal_fov_deg']))
    comp.set_editor_property('aspect_ratio', 16.0 / 9.0)
    comp.set_editor_property('constrain_aspect_ratio', True)
    settings = u.PostProcessSettings()
    values = {
        'auto_exposure_method': u.AutoExposureMethod.AEM_MANUAL,
        'auto_exposure_apply_physical_camera_exposure': False,
        'auto_exposure_bias': float(globals().get('MC_MENU_SHOWCASE_EXPOSURE_BIAS', -0.6)),
        'bloom_intensity': 0.04, 'bloom_threshold': 2.0,
        'lens_flare_intensity': 0.0, 'vignette_intensity': 0.06,
        'motion_blur_amount': 0.0,
    }
    for name, value in values.items():
        settings.set_editor_property('override_' + name, True)
        settings.set_editor_property(name, value)
    comp.set_editor_property('post_process_settings', settings)
    comp.set_editor_property('post_process_blend_weight', 1.0)
    readback = comp.get_editor_property('post_process_settings')
    require(readback.get_editor_property('auto_exposure_method') == u.AutoExposureMethod.AEM_MANUAL,
            'Manual exposure readback failed')
    require(camera.get_auto_activate_player_index() == (0 if variant == 'A' else -1),
            'Camera auto-activation readback failed')
    report['camera'] = {'actor': path_name(camera), 'location_cm': location, 'target_cm': target,
                        'horizontal_fov_deg': source['horizontal_fov_deg'],
                        'projection': 'Perspective', 'auto_activate_player_index': camera.get_auto_activate_player_index(),
                        'manual_exposure': {name: str(readback.get_editor_property(name)) for name in values},
                        'white_clipping_validation': 'Not claimed by authoring script; root must inspect actual capture before final visual acceptance',
                        'anti_aliasing': 'Project quality settings retained; no global CVar or quality change'}
    return camera


def author_lights(handoff, basis, actors, report):
    # Three broad, restrained studio lights in authored DCC world cm.  Global
    # renderer settings and the gameplay map light rig remain untouched.
    camera = handoff['camera']['location_cm']
    hero = next(entry for entry in handoff['roles'] if entry['role'] == 'Hero')
    low, high = hero['bounds_cm']
    focus = [(low[i] + high[i]) * 0.5 for i in range(3)]
    specs = [
        {'role': 'WarmKey', 'location': [focus[0]-210, camera[1]*0.45, focus[2]+230],
         'intensity_cd': 135.0, 'size_cm': [340, 280], 'color': [1.0, 0.87, 0.75], 'shadows': True, 'specular': 0.40},
        {'role': 'CoolFill', 'location': [focus[0]+290, camera[1]*0.25, focus[2]+100],
         'intensity_cd': 42.0, 'size_cm': [420, 340], 'color': [0.72, 0.87, 1.0], 'shadows': False, 'specular': 0.25},
        {'role': 'SoftRim', 'location': [focus[0]+60, focus[1]+320, focus[2]+240],
         'intensity_cd': 100.0, 'size_cm': [260, 220], 'color': [1.0, 0.81, 0.69], 'shadows': False, 'specular': 0.35},
    ]
    target = transform_point(focus, basis)
    for spec in specs:
        location = transform_point(spec['location'], basis)
        light = tag_actor(actors.spawn_actor_from_class(u.RectLight, vec(location)), spec['role'], report)
        light.set_actor_rotation(u.MathLibrary.find_look_at_rotation(vec(location), vec(target)), False)
        component = light.get_component_by_class(u.RectLightComponent)
        component.set_mobility(u.ComponentMobility.MOVABLE)
        component.set_intensity_units(u.LightUnits.CANDELAS)
        component.set_intensity(spec['intensity_cd'])
        component.set_light_color(u.LinearColor(*spec['color'], 1.0))
        component.set_source_width(float(spec['size_cm'][0]))
        component.set_source_height(float(spec['size_cm'][1]))
        component.set_attenuation_radius(2700.0)
        component.set_cast_shadows(spec['shadows'])
        component.set_editor_property('specular_scale', spec['specular'])
        report['lights'].append({**spec, 'location_ue_cm': location, 'target_ue_cm': target,
                                'actual_intensity': component.get_editor_property('intensity')})


def camera_projection(point, handoff, basis):
    camera = handoff['camera']
    location = transform_point(camera['location_cm'], basis)
    forward = transform_point(camera['forward_xyz'], basis)
    up = transform_point(camera['up_xyz'], basis)
    # Unreal's reflected coordinate basis changes handedness.  Cross order is
    # selected explicitly from that basis, not guessed from object placement.
    right = [forward[1]*up[2]-forward[2]*up[1],
             forward[2]*up[0]-forward[0]*up[2], forward[0]*up[1]-forward[1]*up[0]]
    if basis == 'flip_y':
        right = [-value for value in right]
    delta = [point[i]-location[i] for i in range(3)]
    depth = sum(delta[i]*forward[i] for i in range(3))
    require(depth > 0, 'Hero point is behind the authored camera')
    tan_h = math.tan(math.radians(float(camera['horizontal_fov_deg'])) * 0.5)
    return [0.5 + sum(delta[i]*right[i] for i in range(3))/(2*depth*tan_h),
            0.5 - sum(delta[i]*up[i] for i in range(3))/(2*depth*tan_h/(16/9))]


def assemble(handoff, meshes, basis, variant, target_map, out, report):
    levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
    actors = u.get_editor_subsystem(u.EditorActorSubsystem)
    if u.EditorAssetLibrary.does_asset_exist(target_map):
        require(levels.load_level(target_map), 'Cannot load owned menu target map')
    else:
        require(variant == 'B', 'Main menu map is expected to exist with native GameMode already set')
        require(levels.new_level(target_map), 'Cannot create separate preview map')
    world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
    require(package_name(world.get_path_name()) == target_map, 'Loaded world mismatch')
    present = list(actors.get_all_level_actors())
    foreign = [actor.get_actor_label() for actor in present if OWNER_TAG not in [str(x) for x in actor.tags]]
    require(not foreign, 'Preserve unowned map actors; root review needed: ' + str(foreign))
    mode = u.load_class(None, MODE_PATH)
    require(mode, 'Native main menu GameMode missing; no build is triggered')
    settings = world.get_world_settings()
    before_mode = settings.get_editor_property('default_game_mode')
    if variant == 'A':
        require(before_mode == mode, 'Preserve existing MainMenu GameMode; unexpected current override')
    else:
        settings.set_editor_property('default_game_mode', mode)
    controller = u.get_default_object(mode).get_editor_property('player_controller_class')
    require(path_name(controller) == CONTROLLER_PATH, 'Native menu PlayerController changed unexpectedly')
    backup_package(target_map, out, report, is_map=True)
    hero_bounds = report['orientation']['actual_bounds_cm']['Hero']
    hero_center = [(hero_bounds[0][i]+hero_bounds[1][i])*0.5 for i in range(3)]
    projection = camera_projection(hero_center, handoff, basis)
    require(projection[0] >= 0.40 and projection[0] <= 0.95,
            'Authored hero is not right of the left 33% UI region: ' + str(projection))
    coffee_material = author_coffee_material(handoff, basis, variant, out, report)
    with u.ScopedEditorTransaction('Stage approved menu static sketch'):
        for actor in present:
            require(actors.destroy_actor(actor), 'Could not remove previous owned stage actor')
        for entry in handoff['roles']:
            spawn_static(meshes[entry['role']], entry['role'], [0,0,0], u.Rotator(0,0,0),
                         [1,1,1], actors, report)
        assemble_anatomy(handoff, meshes, basis, actors, report, coffee_material)
        camera = author_camera(handoff, basis, variant, actors, report)
        author_lights(handoff, basis, actors, report)
        report['projected_hero_center_uv'] = projection
        report['game_mode'] = {'before': path_name(before_mode), 'after': path_name(settings.get_editor_property('default_game_mode')),
                               'player_controller': path_name(controller), 'native_classes_modified': False}
        report['source_contact_anchors'] = handoff.get('anchors', {})
        report['source_coordinate_contract'] = handoff.get('coordinate_contract')
        report['source_limits'] = handoff.get('limits', [])
        require(levels.save_current_level(), 'Could not save only the current owned menu map')
        report['saved_packages'].append(target_map)
    report['current_world'] = path_name(world)
    report['animation'] = 'Posed static first sketch only; no tick/RPC or runtime animation invented'
    report['deferred_visuals'] = ['Root capture, coffee placement/material and white-clipping inspection',
                                  'Optional contact foam; the coffee film and clean streak are authored in the own enamel clone',
                                  'Future reusable runtime animation via actual AnimationSequence/LevelSequence assets']
    if bool(globals().get('MC_MENU_SHOWCASE_PILOT_CAMERA', False)):
        levels.pilot_level_actor(camera)


def main():
    variant = str(globals().get('MC_MENU_SHOWCASE_VARIANT', 'A')).upper()
    require(variant in ('A', 'B'), 'Only named A/B stage variants are supported')
    target_map = MAIN_MAP if variant == 'A' else PREVIEW_MAP
    import_base = '/Game/UI/MenuShowcase/Posed/' + variant
    phase = str(globals().get('MC_MENU_SHOWCASE_PHASE', 'plan'))
    require(phase in ('plan', 'import_only', 'assemble', 'look_repair','background_repair'), 'Unknown authoring phase')
    apply = globals().get('MC_MENU_SHOWCASE_APPLY') == REVISION
    if not apply:
        phase = 'plan'
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ')
    out = e_path(globals().get('MC_MENU_SHOWCASE_OUT',
                 ROOT / ('Saved/MenuShowcaseAuthoring/' + stamp)))
    require(not out.exists() or not any(out.iterdir()), 'Use a fresh output folder for verified backups')
    out.mkdir(parents=True, exist_ok=True)
    report = {'revision': REVISION, 'variant': variant, 'phase': phase,
              'target_map': target_map, 'import_namespace': import_base,
              'status': 'started', 'backups': [], 'actors': [], 'anatomy': [],
              'lights': [], 'restored_materials': {}, 'saved_packages': [],
              'global_render_configuration_modified': False,
              'no_build_cook_package_or_network': True}
    try:
        handoff_path = e_path(globals().get('MC_MENU_SHOWCASE_HANDOFF', HANDOFF_DEFAULT))
        handoff, fbx = load_handoff(handoff_path)
        report.update({'handoff': str(handoff_path), 'handoff_sha256': sha256(handoff_path),
                       'fbx': str(fbx), 'fbx_sha256': sha256(fbx)})
        require_idle_clean()
        report['dirty_before'] = dirty_packages()
        if phase == 'plan':
            report['status'] = 'read_only_plan_ready'
            report['next_step'] = 'Root reviews source, then import_only with exact approved FBX SHA; review basis before assemble'
        else:
            approved = str(globals().get('MC_MENU_SHOWCASE_APPROVED_FBX_SHA256', '')).lower()
            require(approved == sha256(fbx), 'Root must approve this exact FBX SHA256 before import/assembly')
            if phase in ('look_repair','background_repair'):
                basis = str(globals().get('MC_MENU_SHOWCASE_COORDINATE_BASIS', ''))
                require(basis in ('identity','flip_y'),'Root must specify reviewed basis for look repair')
                if phase=='background_repair':
                    repair_stage_background(handoff,basis,variant,target_map,out,report)
                else:
                    repair_stage_look(handoff,basis,variant,target_map,out,report)
                report['status']=phase+'_saved_pending_root_native_visual_review'
            else:
                meshes = get_or_import(handoff, fbx, import_base, out, report)
                save_owned_meshes(meshes, import_base, report)
            if phase == 'assemble':
                basis = str(globals().get('MC_MENU_SHOWCASE_COORDINATE_BASIS', ''))
                require(basis == report['orientation']['basis'], 'Root must explicitly confirm the detected coordinate basis')
                assemble(handoff, meshes, basis, variant, target_map, out, report)
                report['status'] = 'static_stage_saved_pending_root_visual_review'
            elif phase == 'import_only':
                report['status'] = 'import_only_saved_menu_map_untouched'
            report['dirty_after'] = dirty_packages()
            require(not report['dirty_after'], 'Unexpected unsaved package remains after exact scoped saves')
    except Exception as error:
        report['status'] = 'failed_no_save_all_or_gameplay_changes'
        report['error'] = str(error)
        report['traceback'] = traceback.format_exc()
        report['dirty_after_failure'] = dirty_packages()
        (out / 'AuthoringReport.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
        raise
    filename = 'ImportReport.json' if phase == 'import_only' else 'AuthoringReport.json'
    (out / filename).write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    u.log('MC_MENU_SHOWCASE_STAGE ' + json.dumps({'status': report['status'], 'report': str(out / filename)}))
    return report


RESULT = main()
