"""Import only the owned Cataclysm raft, PBR atlas and bounded cue material.

Run in an idle MessControl Editor through remote_python.py. No map, Blueprint,
gameplay profile or imported artist asset is edited. Repeats preserve existing
generated material graphs and skip unchanged source-file imports.
"""
import hashlib
import json
from pathlib import Path

import unreal as u


ROOT = Path(u.Paths.project_dir()).resolve()
SOURCE = ROOT / 'ArtSource/CataclysmRaft'
DEST = '/Game/Cataclysm/Raft'
OWNER_TAG = 'MCCataclysmRaftGenerated'
OWNER_VALUE = 'import_cataclysm_raft.py:1'
SOURCE_TAG = 'MCCataclysmRaftSourceSHA256'
lib = u.EditorAssetLibrary
edit = u.MaterialEditingLibrary
tools = u.AssetToolsHelpers.get_asset_tools()


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def package(obj):
    return obj.get_path_name().split('.')[0]


def source_hash(filename):
    require(filename.is_relative_to(SOURCE.resolve()) and filename.is_file(),
            'Required raft source missing or outside the source folder: ' + str(filename))
    return hashlib.sha256(filename.read_bytes()).hexdigest()


def existing(name, asset_type):
    path = DEST + '/' + name
    if not lib.does_asset_exist(path):
        return None
    obj = lib.load_asset(path)
    require(isinstance(obj, asset_type) and lib.get_metadata_tag(obj, OWNER_TAG) == OWNER_VALUE,
            'Preserved an unrelated/custom asset at ' + path)
    return obj


def mark(obj, sha=None):
    require(package(obj).startswith(DEST + '/'), 'Unexpected asset outside the owned raft folder')
    lib.set_metadata_tag(obj, OWNER_TAG, OWNER_VALUE)
    if sha:
        lib.set_metadata_tag(obj, SOURCE_TAG, sha)


def save(obj):
    require(package(obj).startswith(DEST + '/'), 'Refusing to save an unrelated asset')
    require(lib.save_loaded_asset(obj, only_if_is_dirty=False), 'Could not save ' + package(obj))


def import_file(name, filename, kind, options=None):
    filename = filename.resolve()
    sha = source_hash(filename)
    obj = existing(name, kind)
    if obj and lib.get_metadata_tag(obj, SOURCE_TAG) == sha:
        return obj
    task = u.AssetImportTask()
    task.filename = str(filename)
    task.destination_path = DEST
    task.destination_name = name
    task.automated = True
    task.replace_existing = True
    task.replace_existing_settings = True
    task.save = False
    if options is not None:
        task.factory = u.FbxFactory()
        task.options = options
    tools.import_asset_tasks([task])
    paths = list(task.imported_object_paths)
    require(paths and all(p.split('.')[0] == DEST + '/' + name for p in paths),
            'Import created unexpected objects: ' + str(paths))
    obj = lib.load_asset(DEST + '/' + name)
    require(isinstance(obj, kind), 'No usable ' + kind.__name__ + ' from ' + str(filename))
    mark(obj, sha)
    return obj


def node(mat, cls, x, y, **properties):
    result = edit.create_material_expression(mat, cls, x, y)
    require(result is not None, 'Could not create a material expression')
    if properties:
        result.set_editor_properties(properties)
    return result


def connect(src, output, prop):
    require(edit.connect_material_property(src, output, prop), 'Failed to connect ' + str(prop))


def compile_material(mat):
    errors = list(edit.recompile_material(mat))
    require(not errors, 'Raft material compile errors: ' + str(errors))
    save(mat)


def atlas_material(textures):
    mat = existing('M_RaftAtlas', u.Material)
    if mat is None:
        mat = tools.create_asset('M_RaftAtlas', DEST, u.Material, u.MaterialFactoryNew())
        require(mat, 'Could not create raft material')
        mark(mat)
        mat.set_editor_properties({'blend_mode': u.BlendMode.BLEND_OPAQUE,
                                  'shading_model': u.MaterialShadingModel.MSM_DEFAULT_LIT})
        properties = [('BaseColor', u.MaterialProperty.MP_BASE_COLOR, u.MaterialSamplerType.SAMPLERTYPE_COLOR),
                      ('Roughness', u.MaterialProperty.MP_ROUGHNESS, u.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR),
                      ('Normal', u.MaterialProperty.MP_NORMAL, u.MaterialSamplerType.SAMPLERTYPE_NORMAL)]
        for index, (role, prop, sampler) in enumerate(properties):
            sample = node(mat, u.MaterialExpressionTextureSample, -500, index * 220,
                          texture=textures[role], sampler_type=sampler,
                          desc='Generated raft atlas: ' + role)
            connect(sample, 'R' if role == 'Roughness' else 'RGB', prop)
        specular = node(mat, u.MaterialExpressionConstant, -200, 620, r=.25)
        connect(specular, '', u.MaterialProperty.MP_SPECULAR)
        compile_material(mat)
    for role, prop in [('BaseColor', u.MaterialProperty.MP_BASE_COLOR),
                       ('Roughness', u.MaterialProperty.MP_ROUGHNESS), ('Normal', u.MaterialProperty.MP_NORMAL)]:
        sample = edit.get_material_property_input_node(mat, prop)
        require(isinstance(sample, u.MaterialExpressionTextureSample)
                and sample.get_editor_property('texture') == textures[role],
                'Preserved custom raft material connection: ' + role)
    return mat


def cue_material():
    mat = existing('M_RaftCue', u.Material)
    if mat is None:
        mat = tools.create_asset('M_RaftCue', DEST, u.Material, u.MaterialFactoryNew())
        require(mat, 'Could not create raft cue material')
        mark(mat)
        mat.set_editor_properties({'blend_mode': u.BlendMode.BLEND_TRANSLUCENT,
                                  'shading_model': u.MaterialShadingModel.MSM_UNLIT,
                                  'two_sided': True})
        color = node(mat, u.MaterialExpressionVertexColor, -300, 0,
                     desc='Native bounded outline/wake: vertex RGB and feathered alpha')
        connect(color, '', u.MaterialProperty.MP_EMISSIVE_COLOR)
        connect(color, 'A', u.MaterialProperty.MP_OPACITY)
        compile_material(mat)
    color = edit.get_material_property_input_node(mat, u.MaterialProperty.MP_EMISSIVE_COLOR)
    require(isinstance(color, u.MaterialExpressionVertexColor)
            and edit.get_material_property_input_node(mat, u.MaterialProperty.MP_OPACITY) == color,
            'Preserved custom raft cue material graph')
    return mat


def main():
    require(Path(u.Paths.get_project_file_path()).name == 'MessControl.uproject', 'Wrong editor project')
    require(not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor(), 'Finish the owned PIE first')
    dirty_before = [p.get_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
    require(not any(p.startswith(DEST + '/') for p in dirty_before),
            'Preserved unsaved changes in the raft folder')
    manifest = json.loads((SOURCE / 'manifest.json').read_text(encoding='utf-8'))
    textures = {}
    for role in ('BaseColor', 'Roughness', 'Normal'):
        tex = import_file('T_CataclysmRaft_' + role, SOURCE / 'Textures' / ('T_CataclysmRaft_' + role + '.png'), u.Texture2D)
        tex.set_editor_property('srgb', role == 'BaseColor')
        tex.set_editor_property('compression_settings', u.TextureCompressionSettings.TC_NORMALMAP
                                if role == 'Normal' else u.TextureCompressionSettings.TC_DEFAULT)
        if role == 'Normal':
            # Raw source is OpenGL +Y. Set True once, never invert the source pixels on repeat.
            tex.set_editor_property('flip_green_channel', True)
        save(tex)
        textures[role] = tex
    wood = atlas_material(textures)
    cue = cue_material()
    options = u.FbxImportUI()
    options.set_editor_properties({'automated_import_should_detect_type': False,
        'mesh_type_to_import': u.FBXImportType.FBXIT_STATIC_MESH,
        'original_import_type': u.FBXImportType.FBXIT_STATIC_MESH,
        'import_mesh': True, 'import_as_skeletal': False, 'import_materials': False,
        'import_textures': False, 'import_animations': False})
    settings = {'combine_meshes': True, 'auto_generate_collision': False,
        'one_convex_hull_per_ucx': True, 'generate_lightmap_u_vs': False,
        'normal_import_method': u.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS,
        'build_nanite': False, 'convert_scene_unit': True, 'import_uniform_scale': 1.0}
    options.static_mesh_import_data.set_editor_properties(settings)
    previous = existing('SM_CataclysmRaft', u.StaticMesh)
    if previous:
        previous.get_editor_property('asset_import_data').set_editor_properties(settings)
    cvar = 'Interchange.FeatureFlags.Import.FBX'
    old = u.SystemLibrary.get_console_variable_int_value(cvar)
    try:
        u.SystemLibrary.execute_console_command(None, cvar + ' 0')
        mesh = import_file('SM_CataclysmRaft', SOURCE / 'SM_CataclysmRaft.fbx', u.StaticMesh, options)
    finally:
        u.SystemLibrary.execute_console_command(None, cvar + ' ' + str(old))
    require(len(mesh.get_editor_property('static_materials')) == 1, 'Expected one raft material slot')
    mesh.set_material(0, wood)
    body = mesh.get_editor_property('body_setup')
    body.set_editor_property('collision_trace_flag', u.CollisionTraceFlag.CTF_USE_SIMPLE_AND_COMPLEX)
    hulls = body.get_editor_property('agg_geom')
    hull_count = sum(len(hulls.get_editor_property(p)) for p in ('convex_elems', 'box_elems', 'sphere_elems', 'sphyl_elems'))
    require(hull_count == 3, 'Expected exactly the three imported UCX hulls: ' + str(hull_count))
    require(mesh.get_num_triangles(0) == manifest['triangles'], 'Unexpected render triangles')
    bounds = mesh.get_bounds()
    actual = [float(bounds.box_extent.x * 2), float(bounds.box_extent.y * 2), float(bounds.box_extent.z * 2)]
    expected = [277.7, 195.6068516, 67.0]
    require(all(abs(a - b) < .2 for a, b in zip(actual, expected)), 'FBX units/bounds mismatch: ' + str(actual))
    save(mesh)
    dirty_after = [p.get_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
    report = {'mesh': mesh.get_path_name(), 'materials': [wood.get_path_name(), cue.get_path_name()],
        'textures': {k: v.get_path_name() for k, v in textures.items()},
        'normal_green_flip': textures['Normal'].get_editor_property('flip_green_channel'),
        'triangles': mesh.get_num_triangles(0), 'simple_collision_hulls': hull_count,
        'bounds_cm': actual, 'source_sha256': manifest['source_sha256'],
        'fbx_sha256': source_hash(SOURCE / 'SM_CataclysmRaft.fbx'),
        'saved_folder': DEST, 'dirty_before': dirty_before, 'dirty_after': dirty_after,
        'unrelated_dirty_preserved': set(dirty_before).issubset(dirty_after),
        'gameplay_validation': 'Pending short solo PIE; no network testing'}
    require(report['unrelated_dirty_preserved'], 'Unexpected change to unrelated dirty package state')
    out = ROOT / 'Saved/Checks/CataclysmRaft'
    out.mkdir(parents=True, exist_ok=True)
    (out / 'ImportReport.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print('CATACLYSM_RAFT_SAVED ' + json.dumps(report))


if __name__ == '__main__':
    main()
