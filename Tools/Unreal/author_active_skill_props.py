"""Import the validated Buster props and editable lit materials, scoped to two owned folders.

Prepared offline. Run only after the current VFX author hands off the Editor.
Use ``python -B Tools/Unreal/author_active_skill_props.py --check-only`` for the
source-only plan check, which never imports Unreal. Normal execution requires
idle MessControl Editor, preserves other dirty packages and never saves a map.
No source export, Blender scene, character, animation or Niagara asset is edited.
"""
from __future__ import annotations

import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import sys
import uuid

PROJECT = Path('E:/DEVGAME/MessControl')
SOURCE = Path('E:/CodexWork/MessControl/BusterActivesAudit_20261029/Exports')
OUTPUT = Path(globals().get('MC_ACTIVE_PROPS_OUTPUT',
    'E:/CodexWork/MessControl/ActiveSkills_20261029/Props')).resolve()
PROPS = '/Game/Gameplay/ActiveSkills/Props'
MATERIALS = '/Game/Gameplay/ActiveSkills/Materials'
OWNER = 'MessControl.ActiveSkillProps.v1'
IDS = ('sponge', 'hook', 'grenade', 'air', 'rocket', 'driver')
IMPORT_UNIFORM_SCALE = 1.0
LOCAL_VERTEX_UNIT_SCALE = 100.0
UNIT_EVIDENCE = ('UE5.8 FFbxImporter::ComputeTotalMatrix uses identity for local vertices when absolute transform and pivot bake are false. '
    'Live attempts01/02 kept Sponge X=1.56248349cm with ImportUniformScale1 or100. BuildScale3D100 also left source/cached bounds tiny. '
    'UnitProbe04 verified a cloned StaticMeshDescription with only local vertex positions multiplied100, then normal editor build at BuildScale3D1: '
    'Sponge156.248352x80.133644x45.164749cm, correct top pivot,116triangles,60vertices and byte-identical UV digest. '
    'Use that persistent local-geometry unit conversion; source FBX and actor/component scale remain unchanged.')
MASTER_CONFIG = {
    'M_BusterPlastic': dict(tint=(.02, .48, .52, 1), roughness=.28, metallic=0., specular=.55),
    'M_BusterMetal': dict(tint=(.87, .86, .77, 1), roughness=.22, metallic=.45, specular=.55),
    'M_BusterRubber': dict(tint=(.025, .031, .033, 1), roughness=.65, metallic=0., specular=.35),
}
INSTANCE_CONFIG = {
    'MI_Buster_Sponge': ('M_BusterPlastic', (.93, .73, .28, 1), .48),
    'MI_Buster_Hook': ('M_BusterMetal', (.02, .48, .52, 1), .22),
    'MI_Buster_Grenade': ('M_BusterPlastic', (.95, .30, .08, 1), .28),
    'MI_Buster_Air': ('M_BusterPlastic', (.02, .48, .52, 1), .28),
    'MI_Buster_Rocket': ('M_BusterPlastic', (.30, .80, .64, 1), .28),
    'MI_Buster_Driver_Chassis': ('M_BusterPlastic', (.02, .48, .52, 1), .28),
    'MI_Buster_Driver_Rim': ('M_BusterPlastic', (.30, .80, .64, 1), .28),
    'MI_Buster_Driver_Rollers': ('M_BusterMetal', (.87, .86, .77, 1), .22),
    'MI_Buster_Driver_Rubber': ('M_BusterRubber', (.025, .031, .033, 1), .65),
}


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def own_path(path):
    package = str(path).split('.')[0]
    return any(package.startswith(root + '/') for root in (PROPS, MATERIALS))


def source_hashes():
    return {str(p.relative_to(SOURCE)): digest(p) for p in SOURCE.rglob('*')
            if p.is_file() and p.suffix.lower() in ('.blend', '.fbx', '.glb', '.json')}


def load_plan():
    require(OUTPUT.drive.upper() == 'E:' and SOURCE.drive.upper() == 'E:', 'Task outputs/source must be on E:')
    result = []
    for slug in IDS:
        manifest = json.loads((SOURCE / slug / 'manifest.json').read_text(encoding='utf-8'))
        require(manifest['id'] == slug and manifest['total_meshes'] == len(manifest['meshes']), 'Bad manifest: ' + slug)
        fbx = SOURCE / slug / (slug + '.fbx')
        require(fbx.is_file(), 'Missing validated FBX: ' + str(fbx))
        meshes = []
        for item in manifest['meshes']:
            require(item['name'].startswith('SM_Buster_'), 'Unexpected source mesh name')
            require(all(abs(v) < 1.e-6 for v in item['rotation']) and all(abs(v - 1) < 1.e-6 for v in item['scale']),
                    'Native assembly expects normalized child rotations/scales: ' + item['name'])
            bounds = item['world_bounds_canonical_m']
            # FFbxDataConverter::ConvertPos maps source RH (X,Y,Z) to
            # Unreal LH (X,-Y,Z); apply that exact basis to comparison data.
            basis = (1., -1., 1.)
            location = [v * 100 * sign for v,sign in zip(item['child_location_m'],basis)]
            local_center = [((lo + hi) * .5 - t) * 100 * sign for lo, hi, t, sign in
                            zip(bounds['min'], bounds['max'], item['child_location_m'], basis)]
            world_a=[v*100*sign for v,sign in zip(bounds['min'],basis)]
            world_b=[v*100*sign for v,sign in zip(bounds['max'],basis)]
            if slug == 'driver':
                role = ('Rubber' if 'WheelsPair' in item['name'] else
                        'Rollers' if 'Roller' in item['name'] else
                        'Rim' if item['name'].endswith('_Rim') else 'Chassis')
                material = 'MI_Buster_Driver_' + role
            else:
                material = 'MI_Buster_' + slug.capitalize()
            meshes.append(dict(name=item['name'], destination=PROPS + '/' + item['name'],
                triangles=item['triangles'], uv_channels=len(item['uv_names']),
                dimensions_cm=[v * 100 for v in item['dimensions_canonical_m']],
                local_center_cm=local_center, child_location_cm=location,
                world_min_cm=[min(a,b) for a,b in zip(world_a,world_b)], world_max_cm=[max(a,b) for a,b in zip(world_a,world_b)],
                material=MATERIALS + '/' + material))
        result.append(dict(id=slug, fbx=str(fbx), sha256=digest(fbx), meshes=meshes))
    require(sum(len(x['meshes']) for x in result) == 11, 'Expected exactly eleven validated meshes')
    require(sum(m['triangles'] for x in result for m in x['meshes']) == 9424, 'Unexpected triangle total')
    return result


def offline_check():
    plan = load_plan()
    # Independent export metadata catches a hand-copied native part offset or
    # seat guide before any Editor import. This does not certify runtime fit.
    driver = next(item for item in plan if item['id'] == 'driver')['meshes']
    native = (PROJECT / 'Source/MessControl/Private/MCDriverSkillAction.cpp').read_text(encoding='utf-8')
    centers = re.search(r'const FVector Centers\[\]=\{(.*?)\};', native, re.S)
    require(centers is not None, 'Native Driver centers require review')
    values = [[float(v.strip().removesuffix('f')) for v in match.split(',')]
              for match in re.findall(r'FVector\(([^()]+)\)', centers.group(1))]
    require(len(values) == 5 and all(abs(a - b) < .001 for actual, expected in zip(values, driver[1:])
                                   for a, b in zip(actual, expected['child_location_cm'])),
            'Native Driver child transforms differ from the validated export')
    chassis = re.search(r'PropTransform=FTransform\(FVector\(([^()]+)\)\)', native)
    require(chassis is not None and all(abs(float(a.strip().removesuffix('f')) - b) < .001
            for a, b in zip(chassis.group(1).split(','), driver[0]['child_location_cm'])), 'Native chassis pivot differs')
    OUTPUT.mkdir(parents=True, exist_ok=True)
    payload = dict(status='offline_prepared_not_executed_in_Unreal', source=str(SOURCE), plan=plan,
                   masters=MASTER_CONFIG, instances=INSTANCE_CONFIG,
                   import_policy=dict(factory='FbxFactory (legacy)', convert_scene=True, convert_scene_unit=True,
                       force_front_x_axis=False, import_uniform_scale=IMPORT_UNIFORM_SCALE, combine_meshes=False,
                       transform_vertex_to_absolute=False, bake_pivot_in_vertex=False,
                       import_materials=False, import_textures=False, auto_generate_collision=False,
                       local_mesh_description_position_scale=LOCAL_VERTEX_UNIT_SCALE, final_build_scale3d=[1,1,1]),
                   measured_unit_contract=UNIT_EVIDENCE, source_to_unreal_basis='(X,-Y,Z); FFbxDataConverter::ConvertPos FbxUtilsImport.cpp:63-69',
                   validation='Source manifest/name/count/triangle/unit/normalized hierarchy and native Driver child offsets match. No UE import, shader or visual acceptance.')
    (OUTPUT / 'OfflinePlan.json').write_text(json.dumps(payload, ensure_ascii=False, indent=2), encoding='utf-8')
    print('MC_ACTIVE_PROPS_OFFLINE_PLAN ' + str(OUTPUT / 'OfflinePlan.json'))
    return payload


def main():
    plan = load_plan()
    import unreal as u
    require(Path(u.Paths.get_project_file_path()).resolve() == (PROJECT / 'MessControl.uproject').resolve(), 'Wrong Editor project')
    require(not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor(), 'Stop Play before importing props')
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%d_%H%M%SZ') + '_' + uuid.uuid4().hex[:8]
    out = OUTPUT / ('Run_' + stamp)
    out.mkdir(parents=True, exist_ok=False)
    task_temp = out / 'Temp';task_temp.mkdir()
    previous_temp = {key: os.environ.get(key) for key in ('TEMP', 'TMP')}
    lib, edit = u.EditorAssetLibrary, u.MaterialEditingLibrary
    tools = u.AssetToolsHelpers.get_asset_tools()
    changed, created = {}, []
    report = dict(passed=False, source=str(SOURCE), plan=plan, imports=[], materials=[], import_uniform_scale=IMPORT_UNIFORM_SCALE,
                  measured_unit_contract=UNIT_EVIDENCE, source_to_unreal_basis='(X,-Y,Z); FFbxDataConverter::ConvertPos FbxUtilsImport.cpp:63-69',
                  hierarchy='Static meshes retain local pivots. Native Driver assembly uses the recorded child translations; no level actors are authored.',
                  limitations=['No gameplay, animation fit, runtime hierarchy, visual acceptance, clean first-launch PSO or network certification.'])
    dirty = lambda: {p.get_name() for p in list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages()) +
                    list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())}
    dirty_before = dirty()
    editor_world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
    map_before = editor_world.get_path_name() if editor_world else None
    report.update(editor_pid=os.getpid(), editor_map_before=map_before, dirty_before=sorted(dirty_before), staged_imports=[])
    require(not any(own_path(p) for p in dirty_before), 'Preserve unsaved prop/material edits before running')
    before_hashes = source_hashes()
    report['source_sha256_before'] = before_hashes
    cvar = 'Interchange.FeatureFlags.Import.FBX'
    previous_fbx = u.SystemLibrary.get_console_variable_int_value(cvar)
    os.environ['TEMP'] = os.environ['TMP'] = str(task_temp)

    def mark(asset):
        require(own_path(asset.get_path_name()), 'Refusing to mutate an unrelated asset: ' + asset.get_path_name())
        changed[asset.get_path_name()] = asset
        lib.set_metadata_tag(asset, 'MCActiveProps.Owner', OWNER)

    def existing(path, cls):
        require(own_path(path), 'Outside owned package roots: ' + path)
        if not lib.does_asset_exist(path):
            return None
        asset = lib.load_asset(path)
        require(isinstance(asset, cls), 'Existing asset has the wrong type: ' + path)
        require(lib.get_metadata_tag(asset, 'MCActiveProps.Owner') == OWNER,
                'Existing artist asset is preserved; expected prior author ownership: ' + path)
        return asset

    def create(name, root, cls, factory):
        asset = tools.create_asset(name, root, cls, factory)
        require(asset is not None, 'Asset creation failed: ' + root + '/' + name)
        created.append(asset.get_path_name());mark(asset)
        return asset

    def backup(path):
        package = str(path).split('.')[0]
        file = PROJECT / 'Content' / (package.removeprefix('/Game/') + '.uasset')
        if file.is_file():
            dest = out / 'BeforePackages' / file.relative_to(PROJECT)
            dest.parent.mkdir(parents=True, exist_ok=True);shutil.copy2(file, dest)

    def material_master(name, config):
        path = MATERIALS + '/' + name;mat = existing(path, u.Material)
        if mat is not None:
            # Repeat runs preserve manual graph edits, but only if the agreed
            # editable input contract and lit surface still exist.
            for prop, kind, parameter in ((u.MaterialProperty.MP_BASE_COLOR, u.MaterialExpressionVectorParameter, 'Tint'),
                    (u.MaterialProperty.MP_ROUGHNESS, u.MaterialExpressionScalarParameter, 'Roughness')):
                node = edit.get_material_property_input_node(mat, prop)
                require(isinstance(node, kind) and str(node.get_editor_property('parameter_name')) == parameter,
                        'Preserved custom graph needs manual review: ' + path)
            require(mat.get_editor_property('shading_model') == u.MaterialShadingModel.MSM_DEFAULT_LIT, 'Expected a lit prop material: ' + path)
            return mat
        mat = create(name, MATERIALS, u.Material, u.MaterialFactoryNew())
        mat.set_editor_properties(dict(blend_mode=u.BlendMode.BLEND_OPAQUE, shading_model=u.MaterialShadingModel.MSM_DEFAULT_LIT))
        for index, (parameter, value, prop) in enumerate((
                ('Tint', config['tint'], u.MaterialProperty.MP_BASE_COLOR),
                ('Roughness', config['roughness'], u.MaterialProperty.MP_ROUGHNESS),
                ('Metallic', config['metallic'], u.MaterialProperty.MP_METALLIC),
                ('Specular', config['specular'], u.MaterialProperty.MP_SPECULAR))):
            vector = parameter == 'Tint'
            node = edit.create_material_expression(mat, u.MaterialExpressionVectorParameter if vector else u.MaterialExpressionScalarParameter, -350, index * 180)
            node.set_editor_property('parameter_name', parameter)
            node.set_editor_property('default_value', u.LinearColor(*value) if vector else value)
            require(edit.connect_material_property(node, '', prop), 'Material connection failed: ' + parameter)
        edit.recompile_material(mat)
        report['materials'].append(dict(path=path, kind='editable DefaultLit master', config=config))
        return mat

    def validate_mesh(mesh, expected):
        require(mesh.get_num_triangles(0) == expected['triangles'], 'Triangle mismatch: ' + expected['name'])
        bounds = mesh.get_bounds()
        size = [float(v * 2) for v in bounds.box_extent.to_tuple()]
        center = [float(v) for v in bounds.origin.to_tuple()]
        for actual, wanted in zip(size, expected['dimensions_cm']):
            require(abs(actual - wanted) <= max(.25, abs(wanted) * .003),
                    'FBX unit/axis/bounds mismatch: ' + expected['name'] + ' ' + str(size))
        for actual, wanted in zip(center, expected['local_center_cm']):
            require(abs(actual - wanted) <= .3, 'Pivot was baked/offset incorrectly: ' + expected['name'] + ' ' + str(center))
        # Reconstruct the exported child hierarchy numerically from actual UE
        # local bounds. Imported roots/empties are deliberately not level actors.
        lo = [c - s * .5 + t for c, s, t in zip(center, size, expected['child_location_cm'])]
        hi = [c + s * .5 + t for c, s, t in zip(center, size, expected['child_location_cm'])]
        require(all(abs(a - b) <= .4 for a, b in zip(lo + hi, expected['world_min_cm'] + expected['world_max_cm'])),
                'Composed hierarchy bounds differ: ' + expected['name'])
        uv = u.get_editor_subsystem(u.StaticMeshEditorSubsystem).get_num_uv_channels(mesh, 0)
        require(uv >= expected['uv_channels'], 'Source UV channels were lost: ' + expected['name'])
        return dict(path=mesh.get_path_name(), dimensions_cm=size, local_center_cm=center,
                    child_location_cm=expected['child_location_cm'], hierarchy_world_min_cm=lo,
                    hierarchy_world_max_cm=hi, triangles=mesh.get_num_triangles(0), uv_channels=uv)

    def description_fingerprint(description, uv_channels):
        uv=[]
        for index in range(description.get_vertex_instance_count()):
            vi=u.VertexInstanceID(index)
            require(description.is_vertex_instance_valid(vi), 'Sparse vertex-instance IDs require review')
            uv.append([list(description.get_vertex_instance_uv(vi, channel).to_tuple()) for channel in range(uv_channels)])
        topology=[]
        for index in range(description.get_triangle_count()):
            triangle=u.TriangleID(index)
            require(description.is_triangle_valid(triangle), 'Sparse triangle IDs require review')
            # UE5.8 GetTriangleVertices appends to three uninitialised entries
            # (MeshDescriptionBase.cpp:756); use its scalar accessors instead.
            topology.append([description.get_vertex_instance_vertex(description.get_triangle_vertex_instance(triangle, corner)).get_editor_property('id_value')
                             for corner in range(3)])
        encoded=lambda value:hashlib.sha256(json.dumps(value,separators=(',',':')).encode('utf-8')).hexdigest()
        return dict(vertices=description.get_vertex_count(), vertex_instances=description.get_vertex_instance_count(),
                    triangles=description.get_triangle_count(), uv_sha256=encoded(uv), topology_sha256=encoded(topology))

    def normalize_local_units(mesh, expected):
        # The FBX local-vertex branch intentionally skips node/global transforms.
        # Change only local positions in a transient clone, preserving every UV,
        # index and imported attribute. No absolute node/pivot bake is performed.
        subsystem=u.get_editor_subsystem(u.StaticMeshEditorSubsystem)
        build=subsystem.get_lod_build_settings(mesh,0)
        require(list(build.get_editor_property('build_scale3d').to_tuple()) == [1.,1.,1.], 'Unexpected incoming mesh build scale')
        raw_size=list((mesh.get_bounds().box_extent*2).to_tuple())
        require(all(abs(a*LOCAL_VERTEX_UNIT_SCALE-b) <= max(.25,abs(b)*.003) for a,b in zip(raw_size,expected['dimensions_cm'])),
                'Incoming local geometry does not follow the measured metre-valued contract: '+expected['name'])
        description=mesh.get_static_mesh_description(0)
        require(description is not None, 'Missing imported source MeshDescription')
        before=description_fingerprint(description,expected['uv_channels'])
        clone=u.SystemLibrary.duplicate_object(description,u.find_object(None,'/Engine/Transient'),'BusterUnits_'+uuid.uuid4().hex)
        require(clone is not None, 'Could not clone local source geometry')
        for index in range(clone.get_vertex_count()):
            vertex=u.VertexID(index)
            require(clone.is_vertex_valid(vertex), 'Sparse vertex IDs require review')
            clone.set_vertex_position(vertex,clone.get_vertex_position(vertex)*LOCAL_VERTEX_UNIT_SCALE)
        require(description_fingerprint(clone,expected['uv_channels']) == before, 'Unit normalization changed UV/topology before build')
        mesh.build_from_static_mesh_descriptions([clone],build_simple_collision=False,fast_build=False)
        require(description_fingerprint(mesh.get_static_mesh_description(0),expected['uv_channels']) == before,
                'Normal editor build changed source UV/topology')
        require(list(subsystem.get_lod_build_settings(mesh,0).get_editor_property('build_scale3d').to_tuple()) == [1.,1.,1.],
                'Final mesh must have identity build scale')
        report.setdefault('unit_normalization',[]).append(dict(mesh=expected['name'],raw_dimensions_cm=raw_size,
            local_vertex_position_multiplier=LOCAL_VERTEX_UNIT_SCALE,final_build_scale3d=[1,1,1],fingerprint=before))

    try:
        for root in (PROPS, MATERIALS):
            lib.make_directory(root)
        for asset_path in [m['destination'] for p in plan for m in p['meshes']] + [MATERIALS + '/' + n for n in (*MASTER_CONFIG, *INSTANCE_CONFIG)]:
            backup(asset_path)
        masters = {name: material_master(name, data) for name, data in MASTER_CONFIG.items()}
        instances = {}
        for name, (parent_name, tint, roughness) in INSTANCE_CONFIG.items():
            path = MATERIALS + '/' + name;instance = existing(path, u.MaterialInstanceConstant)
            if instance is None:
                instance = create(name, MATERIALS, u.MaterialInstanceConstant, u.MaterialInstanceConstantFactoryNew())
                edit.set_material_instance_parent(instance, masters[parent_name])
                edit.set_material_instance_vector_parameter_value(instance, 'Tint', u.LinearColor(*tint))
                edit.set_material_instance_scalar_parameter_value(instance, 'Roughness', roughness)
                edit.update_material_instance(instance)
            else:
                require(instance.get_editor_property('parent') == masters[parent_name], 'Preserved instance has a different parent: ' + path)
            instances[path] = instance
        u.SystemLibrary.execute_console_command(None, cvar + ' 0')
        for item in plan:
            previous = {m['name']: existing(m['destination'], u.StaticMesh) for m in item['meshes']}
            reused = any(previous.values())
            if any(previous.values()):
                require(all(previous.values()), 'Partial prior import is preserved; resolve it before retrying: ' + item['id'])
                for mesh in previous.values():
                    require(lib.get_metadata_tag(mesh, 'MCActiveProps.SourceSHA256') == item['sha256'],
                            'Changed source/artist mesh requires explicit reimport review: ' + mesh.get_path_name())
                mapped = previous
            else:
                stage = PROPS + '/_Import_' + stamp + '/' + item['id']
                lib.make_directory(stage)
                options = u.FbxImportUI()
                options.set_editor_properties(dict(automated_import_should_detect_type=False,
                    mesh_type_to_import=u.FBXImportType.FBXIT_STATIC_MESH, original_import_type=u.FBXImportType.FBXIT_STATIC_MESH,
                    import_mesh=True, import_as_skeletal=False, import_materials=False, import_textures=False, import_animations=False,
                    override_full_name=False))
                options.static_mesh_import_data.set_editor_properties(dict(combine_meshes=False, auto_generate_collision=False,
                    generate_lightmap_u_vs=False, normal_import_method=u.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS,
                    build_nanite=False, convert_scene=True, convert_scene_unit=True, force_front_x_axis=False,
                    transform_vertex_to_absolute=False, bake_pivot_in_vertex=False, import_uniform_scale=IMPORT_UNIFORM_SCALE,
                    import_translation=u.Vector(), import_rotation=u.Rotator(), remove_degenerates=False))
                task = u.AssetImportTask();task.filename=item['fbx'];task.destination_path=stage
                task.destination_name='Buster_' + item['id'];task.automated=True;task.replace_existing=False;task.save=False
                task.factory=u.FbxFactory();task.options=options
                tools.import_asset_tasks([task])
                paths = list(task.imported_object_paths)
                report['staged_imports'].append(dict(source=item['fbx'], paths=[str(p) for p in paths]))
                require(paths and all(str(p).startswith(stage + '/') for p in paths), 'Unexpected import destinations: ' + str(paths))
                imported = [lib.load_asset(p) for p in paths]
                require(all(isinstance(a, u.StaticMesh) for a in imported) and len(imported) == len(item['meshes']),
                        'Legacy FBX must yield only the expected static meshes: ' + str(paths))
                mapped, used = {}, set()
                for expected in item['meshes']:
                    candidates = [a for a in imported if a.get_name().casefold().endswith(expected['name'].casefold())]
                    if len(imported) == len(item['meshes']) == 1:
                        candidates = imported
                    require(len(candidates) == 1 and candidates[0].get_path_name() not in used,
                            'Ambiguous imported node; never rename by guessed order: ' + expected['name'] + ' ' + str(paths))
                    mesh=candidates[0];used.add(mesh.get_path_name());normalize_local_units(mesh,expected);validate_mesh(mesh, expected)
                    mapped[expected['name']] = mesh
                renames = [u.AssetRenameData(mapped[m['name']], PROPS, m['name']) for m in item['meshes']]
                require(tools.rename_assets(renames), 'Owned staging-to-final rename failed: ' + item['id'])
                created.extend(m['destination'] + '.' + m['name'] for m in item['meshes'])
            for expected in item['meshes']:
                mesh=lib.load_asset(expected['destination']);require(isinstance(mesh, u.StaticMesh), 'Final static mesh missing')
                verification=validate_mesh(mesh, expected)
                mark(mesh)
                lib.set_metadata_tag(mesh, 'MCActiveProps.SourceSHA256', item['sha256'])
                lib.set_metadata_tag(mesh, 'MCActiveProps.ChildLocationCM', json.dumps(expected['child_location_cm']))
                lib.set_metadata_tag(mesh, 'MCActiveProps.UnitCorrection', 'LocalMeshDescriptionPositionsx100; BuildScale3D1; actor/componentScale1')
                slots=list(mesh.get_editor_property('static_materials'))
                require(slots, 'Imported mesh has no material slots: ' + expected['name'])
                if reused:
                    require(all(mesh.get_material(slot) == instances[expected['material']] for slot in range(len(slots))),
                            'Preserved artist material assignment needs manual review: ' + expected['name'])
                else:
                    for slot in range(len(slots)):
                        mesh.set_material(slot, instances[expected['material']])
                verification['assigned_material']=expected['material'];report['imports'].append(verification)
        require(not (dirty() - dirty_before - {p for p in dirty() if own_path(p)}), 'An unrelated package became dirty; not saving it')
        for path, asset in changed.items():
            require(own_path(path) and lib.save_loaded_asset(asset, only_if_is_dirty=False), 'Owned save failed: ' + path)
        # Legacy asset renames can leave an owned staging redirector package.
        # Save only that exact new package list, never a global dirty-package set.
        auxiliary = [p for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()
                     if p.get_name() not in dirty_before and p.get_name().startswith(PROPS + '/_Import_' + stamp + '/')]
        if auxiliary:
            require(all(own_path(p.get_name()) for p in auxiliary), 'Unexpected staging package')
            require(u.EditorLoadingAndSavingUtils.save_packages(auxiliary, only_dirty=True), 'Owned staging package save failed')
        report['auxiliary_owned_staging_packages'] = [p.get_name() for p in auxiliary]
        require(dirty() == dirty_before, 'Unexpected dirty packages remain; unrelated work was not saved')
        after_world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
        report['editor_map_after']=after_world.get_path_name() if after_world else None
        require(report['editor_map_after'] == map_before, 'Editor map changed; no viewport/map restoration was attempted')
        require(source_hashes() == before_hashes, 'A validated source export changed')
        report.update(passed=True, saved=list(changed), created=created, source_unchanged=True,
            validation='11 UE mesh bounds/units/pivots/triangles/UV checks; composed manifest hierarchy; editable lit master/instance assignments; scoped saves only.')
    except Exception as error:
        report.update(error=str(error), created=created, dirty_after=sorted(dirty()), saved_attempted=list(changed))
        raise
    finally:
        u.SystemLibrary.execute_console_command(None, cvar + ' ' + str(previous_fbx))
        report['source_sha256_after'] = source_hashes()
        report['source_unchanged'] = report['source_sha256_after'] == before_hashes
        for key, value in previous_temp.items():
            if value is None:os.environ.pop(key, None)
            else:os.environ[key]=value
        (out / 'Authoring.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    u.log('MC_ACTIVE_PROPS_AUTHOR_COMPLETE ' + str(out / 'Authoring.json'))


if __name__ == '__main__':
    if '--check-only' in sys.argv:
        offline_check()
    else:
        main()
