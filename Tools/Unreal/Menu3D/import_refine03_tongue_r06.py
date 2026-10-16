"""Bounded r06 tongue import/bind; run only after the root stops its PIE capture.

Offline preparation: python this_script.py --validate-files
Editor authoring: execute this saved script in the existing Unreal Python/MCP
connection. This file never launches Unreal or calls remote_python itself.

Only one new tongue mesh, five new tongue textures, the five texture parameters
on the three existing row-owned tongue MIs, and the three tongue mesh bindings
may change. The three cavity rows are pre/postcondition guards. Their meshes,
MIs and maps remain unchanged. All relative transforms and scalar/vector/clip
parameters are preserved. No camera, lights, Blueprint graph or startup edits.
"""
import hashlib
import json
from pathlib import Path
import struct
import sys

GEOMETRY_RELEASE_SHA = '535bf9e152190f64ab2663851278278c81df5214f79d1db547e418e28bac0ccb'
PBR_MANIFEST_SHA = 'b50b31c057f00a0234581b994bc14660c21b06b1f9e8b93d2ce49f2a2f865a64'
PACKET_SHA = '53215b04995cfc46b820b622726cf78d692b36f8619f6a964b12cfe097088bbb'
TONGUE_FBX_SHA = '6c98123d3b26b70fdae0924dcc7a6f1992962c83bdc05b57833a8c2c10e47456'
CAVITY_FBX_SHA = '1dcb142957e0efe25aba29204593c7d401347b3b7d693e53b7cf0a0fe1b1d8ce'
OBJ_SHA = '3fd2e321bcbddcedfd56b0dbd9658bdae73940a8ca3eee7f7eb2e551ac050776'
WORK_MAP = '/Game/UI/MainMenu3D/L_MainMenu3D_RefFinish_v08'
BASE = '/Game/UI/MainMenu3D'
MATERIALS = BASE+'/ReferenceFinish/Materials/'
NEW_MESH_NAME = 'SM_DAY03_Tongue_Organic_r06'
NEW_MESH_PATH = BASE+'/Meshes/'+NEW_MESH_NAME
TEXTURE_FOLDER = BASE+'/ReferenceFinish/Textures/DAY03/Interior'
CHANNELS = ('BaseColorTexture','RoughnessTexture','NormalTexture','AOTexture','ThicknessTexture')
HANDOFF_RELATIVE = Path('ArtSource/UI/MainMenu3D/20261015_refB_v1/Assembly/UnrealHandoff/v08_ReferenceFinish/Interior')
NEW_CANDIDATE = 'UV_r06_Refine03_Candidate01'
FROZEN_CANDIDATE = 'UV_Organic_r05_Candidate01'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def texture_asset(parameter):
    return TEXTURE_FOLDER+'/T_DAY03_Tongue_'+parameter.removesuffix('Texture')+'_r06'


def validate_files(root):
    """Filesystem-only input gate; contains no Unreal imports or editor calls."""
    handoff = (root/HANDOFF_RELATIVE/NEW_CANDIDATE).resolve()
    frozen = (root/HANDOFF_RELATIVE/FROZEN_CANDIDATE).resolve()
    release_path = handoff/'release_manifest.json'
    pbr_path = handoff/'Textures/shader_binding.json'
    assert digest(release_path) == GEOMETRY_RELEASE_SHA
    assert digest(pbr_path) == PBR_MANIFEST_SHA
    release = json.loads(release_path.read_text(encoding='utf8'))
    manifest = json.loads(pbr_path.read_text(encoding='utf8'))
    assert release['status'] == 'FROZEN_NEXT_NATIVE_CANDIDATE_TONGUE_PBR_REBAKE_REQUIRED'
    for filename, expected in release['files'].items():
        path = (handoff/filename).resolve()
        assert path.is_relative_to(handoff) and digest(path) == expected, filename
    assert manifest['schema'] == 'messcontrol.menu3d.physical_shader_binding.v1'
    assert manifest['status'] == 'FROZEN_PAINTER_RELEASE_NATIVE_UNREAL_VISUAL_PENDING'
    assert manifest['source_release_manifest_sha256'] == GEOMETRY_RELEASE_SHA
    assert manifest['normal_format'] == 'DirectX' and manifest['texture_resolution'] == 4096
    assert manifest['lighting_baked_into_basecolor'] is False
    assert manifest['mesh_packet_sha256'] == PACKET_SHA == digest(handoff/'texture_mesh_packet.npz')
    assert manifest['static_obj_sha256'] == OBJ_SHA == digest(handoff/'MC_DAY03_Interior_Painter_Open.obj')
    assert manifest['native_fbx_sha256'] == {'SM_DAY03_Interior.fbx':CAVITY_FBX_SHA,'SM_DAY03_Tongue.fbx':TONGUE_FBX_SHA}
    assert digest(handoff/'SM_DAY03_Tongue.fbx') == TONGUE_FBX_SHA
    assert digest(handoff/'SM_DAY03_Interior.fbx') == CAVITY_FBX_SHA == digest(frozen/'SM_DAY03_Interior.fbx')
    assert set(manifest['texture_sets']) == set(manifest['map_metadata']) == {'MC_Interior','MC_Tongue'}
    maps = []
    for set_name in ('MC_Interior','MC_Tongue'):
        assert set(manifest['texture_sets'][set_name]) == set(manifest['map_metadata'][set_name]) == set(CHANNELS)
        allowed_root = frozen if set_name == 'MC_Interior' else handoff
        for parameter in CHANNELS:
            entry = manifest['map_metadata'][set_name][parameter]
            source = Path(entry['path']).resolve()
            assert source == Path(manifest['texture_sets'][set_name][parameter]).resolve()
            assert source.is_relative_to(allowed_root/'Textures') and source.suffix.lower() == '.png'
            payload = source.read_bytes()
            assert hashlib.sha256(payload).hexdigest() == entry['sha256'], source
            assert len(payload) == entry['bytes']
            assert payload[:8] == b'\x89PNG\r\n\x1a\n' and struct.unpack('>II',payload[16:24]) == (4096,4096)
            assert entry['dimensions'] == [4096,4096]
            assert entry['srgb'] == (parameter == 'BaseColorTexture')
            compression = 'TC_NORMALMAP' if parameter == 'NormalTexture' else 'TC_DEFAULT' if parameter == 'BaseColorTexture' else 'TC_MASKS'
            assert entry['compression'] == compression
            assert entry['flip_green_channel'] is (False if parameter == 'NormalTexture' else None)
            maps.append({'set':set_name,'parameter':parameter,'source':source,'sha256':entry['sha256']})
    return {'handoff':handoff,'manifest':manifest,'maps':maps}


def asset_path(asset):
    return asset.get_path_name() if asset else None


def transform_values(transform):
    translation, rotation, scale = transform.translation, transform.rotation, transform.scale3d
    return {'translation':[translation.x,translation.y,translation.z],
            'rotation':[rotation.x,rotation.y,rotation.z,rotation.w],
            'scale':[scale.x,scale.y,scale.z]}


def material_values(u, material):
    lib = u.MaterialEditingLibrary
    result = {'parent':asset_path(material.get_editor_property('parent')),'scalar':{},'vector':{},'texture':{}}
    for name in lib.get_scalar_parameter_names(material):
        result['scalar'][str(name)] = lib.get_material_instance_scalar_parameter_value(material,name)
    for name in lib.get_vector_parameter_names(material):
        value = lib.get_material_instance_vector_parameter_value(material,name)
        result['vector'][str(name)] = [value.r,value.g,value.b,value.a]
    for name in lib.get_texture_parameter_names(material):
        result['texture'][str(name)] = asset_path(lib.get_material_instance_texture_parameter_value(material,name))
    return result


def row_values(actor, component):
    return {'actor':actor.get_path_name(),'actor_transform':transform_values(actor.get_actor_transform()),
            'relative_transform':transform_values(component.get_relative_transform()),
            'mesh':asset_path(component.get_editor_property('static_mesh')),
            'material':asset_path(component.get_material(0)),
            'cast_shadow':component.get_editor_property('cast_shadow')}


def run_in_editor():
    import unreal as u
    root = Path(u.Paths.project_dir()).resolve()
    inputs = validate_files(root)
    levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
    assert not levels.is_in_play_in_editor(), 'Stop the root-owned PIE capture before authoring'
    world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
    assert world.get_path_name().split('.')[0] == WORK_MAP
    assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages(), 'Preserve unsaved map edits'
    report_path = root/'Tools/Unreal/Menu3D/Evidence/r06_refine03_native_binding_report.json'
    assert not report_path.exists(), 'Preserve completed binding evidence'
    assert not u.EditorAssetLibrary.does_asset_exist(NEW_MESH_PATH), 'New candidate asset must not overwrite an existing mesh'
    assert all(not u.EditorAssetLibrary.does_asset_exist(texture_asset(p)) for p in CHANNELS), 'New texture names must not overwrite prior assets'
    rows = []
    for day in (1,2,3):
        for part in ('Interior','Tongue'):
            tag = 'Menu3D_DAY%02d_%s'%(day,part)
            actors = u.GameplayStatics.get_all_actors_with_tag(world,tag)
            assert len(actors) == 1, tag
            actor = actors[0]
            component = actor.get_component_by_class(u.StaticMeshComponent)
            assert component
            expected_mesh = u.load_asset(BASE+'/Meshes/SM_DAY03_'+part+'_Organic_v08')
            assert expected_mesh and component.get_editor_property('static_mesh') == expected_mesh
            suffix = 'v08' if day == 3 else 'v09'
            material = u.load_asset(MATERIALS+'MI_DAY%02d_%s_%s'%(day,part,suffix))
            assert material and component.get_material(0) == material, (tag,'Row-owned MI precondition')
            assert asset_path(material.get_editor_property('parent')) == MATERIALS+'M_Menu'+part+'_v08.M_Menu'+part+'_v08'
            values = material_values(u,material)
            assert set(CHANNELS).issubset(values['texture'])
            row = {'day':day,'part':part,'actor':actor,'component':component,'material':material,
                   'old_mesh':expected_mesh,'before':row_values(actor,component),'material_before':values}
            # Cavity maps are immutable direct r05 links; verify the live bound
            # assets against the same source hashes without importing any copy.
            if part == 'Interior':
                for entry in [m for m in inputs['maps'] if m['set'] == 'MC_Interior']:
                    tex = u.MaterialEditingLibrary.get_material_instance_texture_parameter_value(material,entry['parameter'])
                    assert tex and u.EditorAssetLibrary.get_metadata_tag(tex,'Menu3DSourceSHA') == entry['sha256']
            rows.append(row)
    assert len({r['material'].get_path_name() for r in rows}) == 6, 'Each row must retain its own clipping MI'
    # All six rows, file hashes and destination names pass before any mutation.
    preflight_path = report_path.with_name('r06_refine03_binding_preflight.json')
    assert not preflight_path.exists(), 'Preserve earlier attempt state before retrying'
    preflight_path.write_text(json.dumps({'status':'EDITOR_GATES_PASS_BEFORE_IMPORT',
        'geometry_release_sha256':GEOMETRY_RELEASE_SHA,'PBR_manifest_sha256':PBR_MANIFEST_SHA,
        'rows':[{'day':r['day'],'part':r['part'],'before':r['before'],
                 'material_before':r['material_before']} for r in rows]},indent=2),encoding='utf8')
    assets = u.AssetToolsHelpers.get_asset_tools()
    options = u.FbxImportUI()
    for name,value in [('automated_import_should_detect_type',False),('import_mesh',True),('import_as_skeletal',False),
                       ('import_materials',False),('import_textures',False),('import_animations',False),
                       ('mesh_type_to_import',u.FBXImportType.FBXIT_STATIC_MESH),('original_import_type',u.FBXImportType.FBXIT_STATIC_MESH)]:
        options.set_editor_property(name,value)
    data = options.get_editor_property('static_mesh_import_data')
    for name,value in [('convert_scene',False),('convert_scene_unit',False),('force_front_x_axis',False),
                       ('import_uniform_scale',1.),('transform_vertex_to_absolute',True),('bake_pivot_in_vertex',False),
                       ('combine_meshes',True),('auto_generate_collision',False),('generate_lightmap_u_vs',False),
                       ('normal_import_method',u.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)]:
        data.set_editor_property(name,value)
    task = u.AssetImportTask()
    for name,value in [('filename',str(inputs['handoff']/'SM_DAY03_Tongue.fbx')),('destination_path',BASE+'/Meshes'),
                       ('destination_name',NEW_MESH_NAME),('automated',True),('save',False),('replace_existing',False),
                       ('options',options),('factory',u.FbxFactory())]:
        task.set_editor_property(name,value)
    assets.import_asset_tasks([task])
    mesh = u.load_asset(NEW_MESH_PATH)
    assert mesh and len(mesh.get_editor_property('static_materials')) == 1
    bounds = mesh.get_bounds()
    origin,extent = bounds.origin,bounds.box_extent
    low = [origin.x-extent.x,origin.y-extent.y,origin.z-extent.z]
    high = [origin.x+extent.x,origin.y+extent.y,origin.z+extent.z]
    expected_low,expected_high = [-805.,-505.,-475.],[805.,-65.,-131.3260955810547]
    error = max(abs(a-b) for actual,target in ((low,expected_low),(high,expected_high)) for a,b in zip(actual,target))
    assert error < .1, (low,high,'Unit/axis mismatch; scene bindings are unchanged')
    textures = {}
    for entry in [m for m in inputs['maps'] if m['set'] == 'MC_Tongue']:
        parameter = entry['parameter']
        destination = texture_asset(parameter)
        task = u.AssetImportTask()
        for name,value in [('filename',str(entry['source'])),('destination_path',TEXTURE_FOLDER),
                           ('destination_name',destination.rsplit('/',1)[1]),('automated',True),('save',False),('replace_existing',False)]:
            task.set_editor_property(name,value)
        assets.import_asset_tasks([task])
        tex = u.load_asset(destination)
        assert tex
        normal = parameter == 'NormalTexture'
        tex.set_editor_property('srgb',parameter == 'BaseColorTexture')
        compression = u.TextureCompressionSettings.TC_NORMALMAP if normal else u.TextureCompressionSettings.TC_DEFAULT if parameter == 'BaseColorTexture' else u.TextureCompressionSettings.TC_MASKS
        tex.set_editor_property('compression_settings',compression)
        if normal:
            tex.set_editor_property('flip_green_channel',False)
        wanted = 'TEXTUREGROUPCHARACTERNORMALMAP' if normal else 'TEXTUREGROUPCHARACTER'
        enums = [name for name in dir(u.TextureGroup) if name.replace('_','') == wanted]
        assert len(enums) == 1
        tex.set_editor_property('lod_group',getattr(u.TextureGroup,enums[0]))
        u.EditorAssetLibrary.set_metadata_tag(tex,'Menu3DSourceSHA',entry['sha256'])
        textures[parameter] = tex
    assert len(textures) == 5
    # No material parent, tint, roughness multiplier, scattering or card mask
    # defaults are reset. In 5.8 MI setters may return false after a successful
    # write, so readback establishes success.
    for row in rows:
        if row['part'] != 'Tongue':
            continue
        material = row['material']
        for parameter,tex in textures.items():
            u.MaterialEditingLibrary.set_material_instance_texture_parameter_value(material,parameter,tex)
            assert u.MaterialEditingLibrary.get_material_instance_texture_parameter_value(material,parameter) == tex
        after = material_values(u,material)
        before = row['material_before']
        assert after['parent'] == before['parent'] and after['scalar'] == before['scalar'] and after['vector'] == before['vector']
        assert {k:v for k,v in after['texture'].items() if k not in CHANNELS} == {k:v for k,v in before['texture'].items() if k not in CHANNELS}
    day3_material = next(r['material'] for r in rows if r['day'] == 3 and r['part'] == 'Tongue')
    mesh.set_material(0,day3_material)
    u.EditorAssetLibrary.set_metadata_tag(mesh,'Menu3DSourceSHA',TONGUE_FBX_SHA)
    u.EditorAssetLibrary.set_metadata_tag(mesh,'Menu3DGeometryReleaseSHA',GEOMETRY_RELEASE_SHA)
    bindings = []
    for row in rows:
        component,material = row['component'],row['material']
        if row['part'] == 'Tongue':
            assert component.set_static_mesh(mesh)
            component.set_material(0,material)
        after = row_values(row['actor'],component)
        before = row['before']
        assert all(after[k] == before[k] for k in ('actor','actor_transform','relative_transform','material','cast_shadow'))
        if row['part'] == 'Interior':
            assert after == before and material_values(u,material) == row['material_before']
        else:
            assert after['mesh'] == mesh.get_path_name()
        bindings.append({'day':row['day'],'part':row['part'],'before':before,'after':after,
                         'material_parameters_before':row['material_before'],'material_parameters_after':material_values(u,material)})
    for tex in textures.values():
        assert u.EditorAssetLibrary.save_loaded_asset(tex)
    for row in rows:
        if row['part'] == 'Tongue':
            assert u.EditorAssetLibrary.save_loaded_asset(row['material'])
    assert u.EditorAssetLibrary.save_loaded_asset(mesh)
    assert levels.save_current_level()
    report = {'status':'R06_TONGUE_BOUND_PENDING_ACTUAL_NATIVE_MOTION_LOOK_REVIEW',
              'PBR_manifest_sha256':PBR_MANIFEST_SHA,'geometry_release_sha256':GEOMETRY_RELEASE_SHA,
              'mesh':mesh.get_path_name(),'mesh_bounds_cm':[low,high],'bounds_max_error_cm':error,
              'textures':{parameter:tex.get_path_name() for parameter,tex in textures.items()},'bindings':bindings,
              'unit_policy':'FBX Model100 baked once TransformVertexToAbsolute=True; import_uniform_scale1; no second100',
              'cavity_mesh_maps_materials_unchanged':True,'own_MI_identities_scalar_vector_clip_values_preserved':True,
              'camera_lights_graph_startup_transforms_driver_changed':False}
    report_path.write_text(json.dumps(report,indent=2),encoding='utf8')
    u.log('MC_R06_REFINE03_TONGUE_BOUND '+json.dumps(report))


if __name__ == '__main__':
    if '--validate-files' in sys.argv:
        assert 'unreal' not in sys.modules, 'Offline validation must not load Unreal'
        root = Path(__file__).resolve().parents[3]
        inputs = validate_files(root)
        print(json.dumps({'status':'FILES_ONLY_PASS_NO_UNREAL_CALLS','manifest_sha256':PBR_MANIFEST_SHA,
                          'geometry_release_sha256':GEOMETRY_RELEASE_SHA,'verified_maps':len(inputs['maps']),
                          'expected_created_assets':[NEW_MESH_PATH]+[texture_asset(p) for p in CHANNELS],
                          'expected_existing_MI_texture_updates':[MATERIALS+'MI_DAY%02d_Tongue_%s'%(day,'v08' if day == 3 else 'v09') for day in (1,2,3)],
                          'rows_in_scope':6,'interior_rows_validation_only':3}))
    else:
        run_in_editor()
