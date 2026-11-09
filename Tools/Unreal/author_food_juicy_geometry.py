"""Author two original decorative food wedges in the existing idle UE Editor.

Run AFTER M_FoodJuicy_Pulp exists, through Tools/Unreal/remote_python.py.
Only the two owned meshes are changed/saved; no level, gameplay or donor edits.
UV0.x < .25 identifies ONE short outer peel face (two triangles); all other
faces use UV0.x >= .34 for pulp. OBJ vertex colors are deliberately not used.

APIs verified against UE5.8 headers and author_food_auto_mouth_resources.py:
AssetImportTask/FbxImportUI; StaticMeshEditorSubsystem section, collision and
Nanite utilities; StaticMeshExporterOBJ. Geometry/UV readback is exported to E:.
"""
import datetime
import hashlib
import json
import math
import os
from collections import Counter
from pathlib import Path
import unreal as u

OWNER = 'FoodJuicyVFX_20261109'
ROOT = '/Game/Gameplay/Food/VFX/Juicy'
MATERIAL = ROOT + '/M_FoodJuicy_Pulp'
SOURCE = Path('E:/DEVGAME/MessControl/ArtSource/FoodJuicyVFX_20261109')
OUT = Path('E:/CodexWork/MessControl/Validation/FoodJuicyVFX_20261109')
TAG = 'MC.DraftOwner'
UV_CONTRACT = dict(peel_u=[.02, .22], pulp_u=[.34, .98], v=[.02, .98],
                   peel_mask='UV0.x < 0.25', vertex_colors='not used')

# CCW XY outlines, centimetres. A short chamfer edge is the sole peel face.
# Sloping, inset upper profiles form angular cut wedges instead of round beads.
SPECS = [
    dict(name='SM_FoodJuicy_Chunk',
         outline=[[-6.5, -4.5], [6.2, -3.9], [4.5, 4.4], [-4.5, 3.8], [-6.6, 1.9]],
         upper_z=[2.8, 3.6, .8, 1.4, 2.0], lower_z=-2.8,
         upper_inset=.94, upper_shift=[.15, -.10], peel_edge=3, triangles=16),
    dict(name='SM_FoodJuicy_Crumb',
         outline=[[-5.7, -3.9], [5.6, -2.8], [-2.0, 4.2], [-4.4, 3.0]],
         upper_z=[2.7, 1.6, .65, 1.5], lower_z=-2.5,
         upper_inset=.90, upper_shift=[.20, -.05], peel_edge=2, triangles=12),
]


def digest(path):
    # Git normalizes these text masters. Use the same logical bytes on a fresh
    # checkout and after Windows OBJ authoring rather than hashing CRLF/LF.
    return hashlib.sha256(path.read_text(encoding='utf8').encode('utf8')).hexdigest()


def preserve_or_write(path, text):
    if path.exists():
        assert path.read_text(encoding='utf8') == text, 'Preserve existing source: ' + str(path)
    else:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding='utf8')


def cross(a, b):
    return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])


def sub(a, b):
    return tuple(a[i]-b[i] for i in range(3))


def make_geometry(spec):
    outline = spec['outline']
    n = len(outline)
    vertices = [(x, y, spec['lower_z']) for x, y in outline]
    vertices += [(x*spec['upper_inset']+spec['upper_shift'][0],
                  y*spec['upper_inset']+spec['upper_shift'][1], spec['upper_z'][i])
                 for i, (x, y) in enumerate(outline)]
    low = [min(v[i] for v in vertices) for i in range(3)]
    high = [max(v[i] for v in vertices) for i in range(3)]
    polygons = [(list(reversed(range(n))), False, 'bottom'),
                (list(range(n, 2*n)), False, 'cut pulp')]
    polygons += [([i, (i+1) % n, n+(i+1) % n, n+i], i == spec['peel_edge'], 'side')
                 for i in range(n)]
    uv, triangles, normals, peel_triangles = [], [], [], 0
    for indices, peel, label in polygons:
        start = len(uv)
        u0, u1 = UV_CONTRACT['peel_u' if peel else 'pulp_u']
        for j, index in enumerate(indices):
            if label == 'side':
                x, y = [(0, 0), (1, 0), (1, 1), (0, 1)][j]
            else:
                x = (vertices[index][0]-low[0])/(high[0]-low[0])
                y = (vertices[index][1]-low[1])/(high[1]-low[1])
            uv.append((u0+(u1-u0)*x, .02+.96*y))
        for j in range(1, len(indices)-1):
            corners = [indices[0], indices[j], indices[j+1]]
            direction = cross(sub(vertices[corners[1]], vertices[corners[0]]),
                              sub(vertices[corners[2]], vertices[corners[0]]))
            length = math.sqrt(sum(x*x for x in direction))
            assert length > 1.e-5, 'Degenerate original triangle'
            normals.append(tuple(x/length for x in direction))
            triangles.append(dict(vertices=corners, uv=[start, start+j, start+j+1],
                                  normal=len(normals)-1, peel=peel))
            peel_triangles += int(peel)
    assert len(triangles) == spec['triangles'] and peel_triangles == 2
    edges = Counter(tuple(sorted((f['vertices'][i], f['vertices'][(i+1) % 3])))
                    for f in triangles for i in range(3))
    assert set(edges.values()) == {2}, 'Original source must be a closed solid'
    signed_volume = sum(sum(vertices[f['vertices'][0]][k]*
                            cross(vertices[f['vertices'][1]], vertices[f['vertices'][2]])[k]
                            for k in range(3)) for f in triangles)/6.
    assert signed_volume > 0, 'Original winding must point outward'
    edge = spec['peel_edge']
    a, b = outline[edge], outline[(edge+1) % n]
    peel_width = math.hypot(b[0]-a[0], b[1]-a[1])
    dimensions = [high[i]-low[i] for i in range(3)]
    assert 10 <= max(dimensions) <= 14
    assert .20 <= peel_width/max(dimensions) <= .25, 'Peel face must remain narrow'
    return dict(vertices=vertices, uv=uv, triangles=triangles, normals=normals,
                dimensions_cm=dimensions, volume_cm3=signed_volume,
                peel_face_width_cm=peel_width,
                peel_face_width_to_max_span=peel_width/max(dimensions))


def obj_text(name, geometry):
    lines = ['# Original MessControl angular food wedge, centimetres',
             '# One narrow peel face: UV0.x < .25; remaining surfaces are pulp.',
             'o ' + name, 's off']
    lines += ['v %.9f %.9f %.9f' % v for v in geometry['vertices']]
    lines += ['vt %.9f %.9f' % uv for uv in geometry['uv']]
    lines += ['vn %.9f %.9f %.9f' % normal for normal in geometry['normals']]
    for face in geometry['triangles']:
        lines.append('f ' + ' '.join('%d/%d/%d' % (v+1, t+1, face['normal']+1)
                                    for v, t in zip(face['vertices'], face['uv'])))
    return '\n'.join(lines) + '\n'


def exported_uv_metrics(path, expected_triangles):
    uv, faces = [], []
    for line in path.read_text(encoding='utf8', errors='replace').splitlines():
        words = line.split()
        if not words:
            continue
        if words[0] == 'vt':
            uv.append(tuple(float(x) for x in words[1:3]))
        elif words[0] == 'f':
            face = [int(corner.split('/')[1])-1 for corner in words[1:]]
            for i in range(1, len(face)-1):
                faces.append([face[0], face[i], face[i+1]])
    assert len(faces) == expected_triangles and uv, 'Imported triangle/UV count changed'
    peel = 0
    for face in faces:
        values = [uv[i][0] for i in face]
        if max(values) < .25:
            peel += 1
        else:
            assert min(values) >= .3399, 'UVs bridge peel/pulp regions'
    assert peel == 2, 'Imported UV0 must identify exactly one peel quad'
    return dict(triangles=len(faces), peel_triangles=peel, uv_corners=len(uv),
                uv_range=[[min(v[i] for v in uv), max(v[i] for v in uv)] for i in (0, 1)])


def main():
    assert SOURCE.drive.upper() == OUT.drive.upper() == 'E:'
    temp = Path('E:/CodexTemp/FoodJuicyVFX_20261109/Geometry')
    temp.mkdir(parents=True, exist_ok=True)
    assert Path(os.environ.get('TEMP','E:/CodexTemp')).drive.upper() == 'E:', 'Editor TEMP must stay on E:'
    lib = u.EditorAssetLibrary
    assert not u.EditorLevelLibrary.get_pie_worlds(True), 'Preserve active PIE'
    assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages(), 'Preserve unsaved map'
    dirty = list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    assert all(p.get_path_name().startswith(ROOT+'/') for p in dirty), 'Preserve unrelated content edits'
    material = u.load_asset(MATERIAL)
    assert isinstance(material, u.MaterialInterface), 'Root must author the shared pulp material first'
    paths = [ROOT+'/'+s['name'] for s in SPECS]
    for path in paths:
        if lib.does_asset_exist(path):
            asset = u.load_asset(path)
            assert lib.get_metadata_tag(asset, TAG) == OWNER, 'Preserve unowned mesh: '+path
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
    run = OUT/'GeometryAuthoring'/stamp
    run.mkdir(parents=True, exist_ok=False)
    mesh_edit = u.get_editor_subsystem(u.StaticMeshEditorSubsystem)
    tools = u.AssetToolsHelpers.get_asset_tools()
    report = dict(owner=OWNER, material=MATERIAL, uv_contract=UV_CONTRACT, meshes=[], passed=False)
    try:
        for spec, path in zip(SPECS, paths):
            geometry = make_geometry(spec)
            source = SOURCE/(spec['name']+'.obj')
            preserve_or_write(source, obj_text(spec['name'], geometry))
            source_recipe = dict(owner=OWNER, spec=spec, geometry=geometry,
                                 uv_contract=UV_CONTRACT, source_sha256=digest(source),
                                 source_hash_encoding='UTF-8 with normalized LF')
            preserve_or_write(SOURCE/(spec['name']+'.json'),
                              json.dumps(source_recipe, indent=2)+'\n')
            created = not lib.does_asset_exist(path)
            if created:
                task = u.AssetImportTask()
                task.set_editor_properties(dict(filename=str(source), destination_path=ROOT,
                    destination_name=spec['name'], automated=True, replace_existing=False, save=False))
                options = u.FbxImportUI()
                options.set_editor_properties(dict(import_mesh=True, import_materials=False,
                    import_textures=False, import_as_skeletal=False,
                    mesh_type_to_import=u.FBXImportType.FBXIT_STATIC_MESH))
                options.static_mesh_import_data.set_editor_properties(dict(
                    auto_generate_collision=False, generate_lightmap_u_vs=False,
                    build_nanite=False, distance_field_resolution_scale=0.,
                    normal_import_method=u.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS,
                    convert_scene=False, convert_scene_unit=False))
                task.set_editor_property('options', options)
                tools.import_asset_tasks([task])
                mesh = u.load_asset(path)
                assert isinstance(mesh, u.StaticMesh), list(task.imported_object_paths)
                lib.set_metadata_tag(mesh, TAG, OWNER)
                lib.set_metadata_tag(mesh, 'MC.UV0Contract', 'U<.25 peel; U>=.34 pulp; one peel quad')
                mesh.set_material(0, material)
                mesh_edit.remove_collisions(mesh)
                nanite = mesh_edit.get_nanite_settings(mesh)
                nanite.set_editor_property('enabled', False)
                mesh_edit.set_nanite_settings(mesh, nanite, True)
                for section in range(mesh.get_num_sections(0)):
                    mesh_edit.enable_section_cast_shadow(mesh, False, 0, section)
                    mesh_edit.enable_section_collision(mesh, False, 0, section)
                    mesh_edit.enable_section_affect_distance_field_lighting(mesh, False, 0, section)
                assert lib.save_loaded_asset(mesh, only_if_is_dirty=False)
            else:
                mesh = u.load_asset(path)  # Idempotent readback; no reimport/overwrite.
            assert mesh.get_num_triangles(0) == spec['triangles']
            assert mesh.get_num_sections(0) == mesh_edit.get_number_materials(mesh) == 1
            assert mesh_edit.get_num_uv_channels(mesh, 0) >= 1
            assert mesh.get_material(0) == material
            assert not mesh_edit.get_nanite_settings(mesh).get_editor_property('enabled')
            assert mesh_edit.get_simple_collision_count(mesh) == mesh_edit.get_convex_collision_count(mesh) == 0
            assert not mesh_edit.is_section_cast_shadow_enabled(mesh, 0, 0)
            assert not mesh_edit.is_section_collision_enabled(mesh, 0, 0)
            exported = run/(spec['name']+'.Imported.obj')
            export = u.AssetExportTask()
            export.set_editor_properties(dict(object=mesh, filename=str(exported), automated=True,
                prompt=False, replace_identical=False, exporter=u.StaticMeshExporterOBJ()))
            assert u.Exporter.run_asset_export_task(export) and exported.exists()
            uv_metrics = exported_uv_metrics(exported, spec['triangles'])
            report['meshes'].append(dict(asset=path, created=created, triangles=spec['triangles'],
                source=str(source), source_sha256=digest(source), source_recipe=str(SOURCE/(spec['name']+'.json')),
                source_dimensions_cm=geometry['dimensions_cm'],
                peel_face_width_to_max_span=geometry['peel_face_width_to_max_span'],
                imported_uv0=uv_metrics, imported_obj=str(exported),
                nanite=False, section_shadows=False, section_collision=False, simple_collision=0,
                material_slots=1, resource_readiness='Niagara mesh renderer must retain this mesh and shared material in its scoped profile closure.'))
        remaining = {p.get_path_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
        assert remaining.issubset({p.get_path_name() for p in dirty}), 'Unexpected dirty packages after owned saves'
        assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages()
        report['passed'] = True
    finally:
        (run/'Report.json').write_text(json.dumps(report, indent=2), encoding='utf8')
    u.log('MC_FOOD_JUICY_GEOMETRY '+str(run/'Report.json'))


if __name__ == '__main__':
    main()
