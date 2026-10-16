"""Shape-only r06 candidate: continuous low tongue bed, r05 cavity unchanged.

Preserves topology, UV0, equator, ventral surface, X/Y positions and the paired
character-contact patch. Writes only a new sibling candidate; no FBX/Painter
promotion. Source units remain metres, identity objects; native cm uses the
existing FBX node scale 100 exactly once.
"""
import bpy
import bmesh
import hashlib
import json
import math
import sys
import argparse
from pathlib import Path

import numpy as np
from mathutils import Matrix, Vector
from mathutils.bvhtree import BVHTree


BASE = Path('E:/DEVGAME/MessControl/ArtSource/UI/MainMenu3D/20261015_refB_v1')
PARENT = BASE/'Assembly/UnrealHandoff/v08_ReferenceFinish/Interior/UV_Organic_r05_Candidate01'
OUT = PARENT.parent/'Organic_r06_LowTongue'
SOURCE = PARENT/'MC_DAY03_InteriorSource.blend'
OUTER = BASE/'Assembly/UnrealHandoff/v08b_SeamUV/Candidates/Final96_GumMinStretch_LocalFangRepair/MC_DAY03_UnrealMorphSource.blend'
ANCHOR = np.array([-5.757009, -2.887758, -2.076843])


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def coords(ob):
    array = np.empty(len(ob.data.vertices)*3, np.float32)
    ob.data.vertices.foreach_get('co', array)
    return array.reshape(-1, 3)


def uvcoords(ob):
    array = np.empty(len(ob.data.uv_layers['UV0'].data)*2, np.float32)
    ob.data.uv_layers['UV0'].data.foreach_get('uv', array)
    return array.reshape(-1, 2)


def geometry_digest(ob):
    me = ob.data
    faceverts = np.array([loop.vertex_index for loop in me.loops], np.int32)
    counts = np.array([p.loop_total for p in me.polygons], np.int32)
    return {'co': hashlib.sha256(coords(ob).tobytes()).hexdigest(),
            'topology': hashlib.sha256(faceverts.tobytes()+counts.tobytes()).hexdigest(),
            'uv': hashlib.sha256(uvcoords(ob).tobytes()).hexdigest(),
            'matrix': [list(row) for row in ob.matrix_world]}


def contact(co, faces, x=ANCHOR[0], y=ANCHOR[1]):
    tree = BVHTree.FromPolygons(co.tolist(), faces, all_triangles=False)
    hit, normal, index, distance = tree.ray_cast(Vector((x, y, 5)), Vector((0, 0, -1)))
    assert hit is not None
    return float(hit.z)


def audit(ob):
    bm = bmesh.new()
    bm.from_mesh(ob.data)
    bm.normal_update()
    non_manifold = sum(not e.is_manifold for e in bm.edges)
    volume = bm.calc_volume(signed=True)
    components = 0
    seen = set()
    for start in bm.verts:
        if start in seen:
            continue
        components += 1
        stack = [start]
        while stack:
            v = stack.pop()
            if v in seen:
                continue
            seen.add(v)
            stack.extend(e.other_vert(v) for e in v.link_edges if e.other_vert(v) not in seen)
    bm.free()
    co = coords(ob)
    ob.data.calc_loop_triangles()
    tri = np.array([t.vertices[:] for t in ob.data.loop_triangles], np.int32)
    areas = np.linalg.norm(np.cross(co[tri[:, 1]]-co[tri[:, 0]], co[tri[:, 2]]-co[tri[:, 0]]), axis=1)*.5
    result = {'vertices': len(co), 'triangles': len(tri), 'components': components,
              'non_manifold_edges': non_manifold, 'signed_volume_source': float(volume),
              'degenerate_triangles': int(np.sum(areas < 1e-12)),
              'bounds_cm': [(co.min(0)*100).tolist(), (co.max(0)*100).tolist()]}
    assert components == 1 and non_manifold == 0 and volume > 0 and not result['degenerate_triangles'], result
    return result


def set_clay(ob, rgb):
    mat = bpy.data.materials.new('Review_'+ob.name)
    mat.use_nodes = True
    mat.use_backface_culling = True
    bs = mat.node_tree.nodes.get('Principled BSDF')
    bs.inputs['Base Color'].default_value = (*rgb, 1)
    bs.inputs['Roughness'].default_value = .45
    ob.data.materials.clear()
    ob.data.materials.append(mat)
    return mat


def make_camera(scene):
    data = bpy.data.cameras.new('REVIEW_Camera')
    cam = bpy.data.objects.new('REVIEW_Camera', data)
    scene.collection.objects.link(cam)
    scene.camera = cam
    data.type = 'PERSP'
    data.sensor_width = 36.
    data.sensor_fit = 'HORIZONTAL'
    data.lens = 36/(2*math.tan(math.radians(39.97)/2))
    return cam


def aim(cam, location, target):
    cam.location = location
    cam.rotation_euler = (Vector(target)-cam.location).to_track_quat('-Z', 'Y').to_euler()


def render(scene, name):
    scene.render.filepath = str(OUT/(name+'.png'))
    bpy.ops.render.render(write_still=True)


def main():
    global OUT
    ap = argparse.ArgumentParser()
    ap.add_argument('--candidate', default='Organic_r06_LowTongue')
    ap.add_argument('--bed-height', type=float, default=1.43)
    ap.add_argument('--restore-fullness', type=float, default=0.)
    opt = ap.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
    assert not Path(opt.candidate).is_absolute() and '..' not in Path(opt.candidate).parts
    OUT = PARENT.parent/opt.candidate
    assert bpy.app.background
    print('BLENDER_VERSION', bpy.app.version_string)
    assert not OUT.exists(), 'New candidate directory required; refusing to overwrite evidence'
    OUT.mkdir(parents=True)
    before_hashes = {p.name: sha(p) for p in [SOURCE, PARENT/'SM_DAY03_Interior.fbx', PARENT/'SM_DAY03_Tongue.fbx', PARENT/'texture_mesh_packet.npz', PARENT/'MC_DAY03_Interior_Painter_Open.obj']}
    bpy.ops.wm.open_mainfile(filepath=str(SOURCE))
    cavity = bpy.data.objects['SM_DAY03_Interior']
    tongue = bpy.data.objects['SM_DAY03_Tongue']
    assert len(bpy.data.objects) == 2
    assert len(tongue.data.vertices) == 9090
    before_cavity = geometry_digest(cavity)
    before_tongue = geometry_digest(tongue)
    old = coords(tongue).copy()
    co = old.astype(np.float64)
    faces = [tuple(p.vertices) for p in tongue.data.polygons]
    # r05 latitude topology: ventral pole; 71 rings * 128; dorsal pole.
    # This mask has exact equatorial boundary; no coordinate threshold guesses.
    upper = np.zeros(len(co), bool)
    sp = np.zeros(len(co), np.float64)
    for il in range(37, 72):
        start = 1+(il-1)*128
        upper[start:start+128] = True
        sp[start:start+128] = math.sin(-math.pi/2+math.pi*il/72)
    upper[-1] = True
    sp[-1] = 1.
    x = co[:, 0]
    # The old ~1.95m bilateral lift produced two peaks separated by a ~1.2m
    # valley. Replace only dorsal Z with a smooth, broad bed. f(s) is monotone,
    # continuous with a nonzero equatorial derivative and a shallow top slope.
    f = 1.8*sp-.8*sp*sp
    co[upper, 2] = (-3.43+opt.bed_height*f-.055*np.exp(-(x/.75)**2)*sp)[upper]
    # Contact lift is broad and symmetric and only ~0.1m, instead of retaining
    # the rejected high bilateral lobes. A zero-valued equator keeps the join.
    patch = np.exp(-((np.abs(x)-abs(ANCHOR[0]))/1.10)**2-((co[:, 1]-ANCHOR[1])/1.15)**2)*f
    contact_iterations = []
    for iteration in range(4):
        hit = contact(co, faces)
        delta = ANCHOR[2]-hit
        # Normalise to the interpolated hit location to converge without a
        # discontinuity at an arbitrary vertex or touching the underside.
        co[upper, 2] += delta*patch[upper]/max(float(np.exp(-((ANCHOR[1]-ANCHOR[1])/1.15)**2)*(1.8*.699-.8*.699**2)), .1)
        contact_iterations.append({'iteration': iteration, 'ray_before_z': hit, 'target_delta': float(delta)})
    continuous_bed = co.copy()
    if opt.restore_fullness:
        assert 0 <= opt.restore_fullness <= .3
        # Root's bounded Refine03 direction: recover only part of the removed
        # rear/lateral r05 volume, leaving the continuous raised centre intact.
        rear = 1/(1+np.exp((co[:, 1]+2.30)/.50))
        lateral = 1-np.exp(-(x/2.8)**4)
        anchor_exclusion = 1-np.exp(-((np.abs(x)-abs(ANCHOR[0]))/1.05)**2-((co[:, 1]-ANCHOR[1])/1.15)**2)
        fullness_field = rear*lateral*anchor_exclusion
        restored = opt.restore_fullness*np.maximum(old[:, 2]-co[:, 2], 0)*fullness_field
        co[upper, 2] += restored[upper]
        for iteration in range(3):
            hit = contact(co, faces)
            delta = ANCHOR[2]-hit
            co[upper, 2] += delta*patch[upper]/max(float(1.8*.699-.8*.699**2), .1)
            contact_iterations.append({'fullness_contact_iteration': iteration, 'ray_before_z': hit, 'target_delta': float(delta)})
    tongue.data.vertices.foreach_set('co', np.asarray(co, np.float32).ravel())
    tongue.data.update()
    bpy.context.view_layer.update()
    co = coords(tongue)
    assert np.array_equal(co[:, :2], old[:, :2])
    assert np.array_equal(co[~upper], old[~upper]), 'Ventral/equator must stay bitexact'
    assert geometry_digest(cavity) == before_cavity
    after_tongue = geometry_digest(tongue)
    assert before_tongue['topology'] == after_tongue['topology']
    assert before_tongue['uv'] == after_tongue['uv']
    assert before_tongue['matrix'] == after_tongue['matrix']
    contact_error = abs(contact(co, faces)-ANCHOR[2])
    assert contact_error < 2e-5, contact_error
    # Mirror correspondence is the known ring t -> pi-t mapping, same latitude.
    mirror_errors = []
    for il in range(1, 72):
        start = 1+(il-1)*128
        ring = co[start:start+128]
        mirror = ring[(64-np.arange(128)) % 128].copy()
        mirror[:, 0] *= -1
        mirror_errors.extend(np.linalg.norm(ring-mirror, axis=1).tolist())
    assert max(mirror_errors) < 1e-5
    shape_report = {'status': 'SHAPE_CANDIDATE_ONLY: root visual approval required; no final export',
                    'bed_height_parameter_source': opt.bed_height,
                    'restore_removed_r05_posterolateral_fraction': opt.restore_fullness,
                    'parent_source': str(SOURCE), 'parent_sha256': before_hashes[SOURCE.name],
                    'method': 'Dorsal Z-only smooth low-bed deformation. X/Y, equator/ventral, cavity, topology, UV0 and transforms unchanged.',
                    'units': 'Source metres; identity objects; native centimetres via existing FBX node100 once',
                    'character_contact_anchor_source': ANCHOR.tolist(), 'contact_error_source': contact_error,
                    'contact_error_cm': contact_error*100, 'contact_iterations': contact_iterations,
                    'dorsal_z_delta_cm_min_max': [float((co[upper, 2]-old[upper, 2]).min()*100), float((co[upper, 2]-old[upper, 2]).max()*100)],
                    'dorsal_peak_z_cm_before_after': [float(old[upper, 2].max()*100), float(co[upper, 2].max()*100)],
                    'mirror_max_source': max(mirror_errors),
                    'cavity_before': before_cavity, 'cavity_after': geometry_digest(cavity),
                    'tongue_before': before_tongue, 'tongue_after': after_tongue,
                    'uv_reuse': 'UV0 byteexact. BC/roughness/painted dorsal papillae placement can be reused. Re-bake tongue normals/AO/thickness/curvature after approval; no reuse claim for changed mesh maps.',
                    'cavity': audit(cavity), 'tongue': audit(tongue)}
    tongue['candidate'] = 'Organic_r06_LowTongue'
    tongue['dorsal_shape_only'] = True
    source_out = OUT/'MC_DAY03_InteriorSource.blend'
    bpy.ops.wm.save_as_mainfile(filepath=str(source_out))
    shape_report['candidate_source'] = str(source_out)
    shape_report['candidate_sha256'] = sha(source_out)
    # Save the clean two-object candidate BEFORE adding any review apparatus.
    # Render-only materials are neutral, so macro shape is not hidden by PBR.
    set_clay(cavity, (.16, .065, .055))
    set_clay(tongue, (.36, .125, .100))
    with bpy.data.libraries.load(str(OUTER), link=False) as (src, dst):
        dst.objects = ['SK_DAY03']
    outer = dst.objects[0]
    outer.parent = None
    outer.matrix_world = Matrix.Identity(4)
    bpy.context.scene.collection.objects.link(outer)
    for mod in list(outer.modifiers):
        outer.modifiers.remove(mod)
    for key in outer.data.shape_keys.key_blocks:
        key.value = 0.
    for mat in outer.data.materials:
        bs = mat.node_tree.nodes.get('Principled BSDF')
        for link in list(bs.inputs['Base Color'].links):
            mat.node_tree.links.remove(link)
        bs.inputs['Base Color'].default_value = (.65, .68, .67, 1) if mat.name.startswith('MC_Enamel') else (.21, .30, .32, 1)
        bs.inputs['Roughness'].default_value = .42
        mat.use_backface_culling = True
    cavity.scale.x = .90
    tongue.scale.x = .90
    scene = bpy.context.scene
    world = bpy.data.worlds.new('REVIEW_Organic_r06')
    scene.world = world
    world.use_nodes = True
    world.node_tree.nodes['Background'].inputs[0].default_value = (.025, .028, .032, 1)
    world.node_tree.nodes['Background'].inputs[1].default_value = .12
    for name, location, energy, size, color in [
            ('Key', (-4, 8, 7), 1700, 8, (1, .88, .80)),
            ('Fill', (7, 8, 1), 650, 8, (.78, .84, 1)),
            ('Cavity', (0, 5, 2), 120, 7, (1, .80, .77))]:
        data = bpy.data.lights.new('REVIEW_'+name, 'AREA')
        light = bpy.data.objects.new('REVIEW_'+name, data)
        scene.collection.objects.link(light)
        light.location = location
        light.rotation_euler = (Vector((0, -5, 0))-light.location).to_track_quat('-Z', 'Y').to_euler()
        data.energy = energy
        data.shape = 'DISK'
        data.size = size
        data.color = color
    cam = make_camera(scene)
    scene.render.engine = 'BLENDER_EEVEE'
    scene.eevee.taa_render_samples = 64
    scene.render.resolution_x = 1600
    scene.render.resolution_y = 900
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = 'PNG'
    scene.view_settings.view_transform = 'AgX'
    # Paired identical cameras/materials; geometry is the only variable.
    new = co.copy()
    review_variants = [('r05_before', old)]
    if opt.restore_fullness:
        review_variants.append(('refine02_before', continuous_bed.astype(np.float32)))
    review_variants.append(('r06_after', new))
    for variant, vertices in review_variants:
        tongue.data.vertices.foreach_set('co', vertices.ravel())
        tongue.data.update()
        outer.hide_render = False
        for view, location, target in [
                ('matched_front', (0, 1400/53.76540384, 15/53.76540384), (0, 0, 15/53.76540384)),
                ('matched_oblique', (230/53.76540384, 1380/53.76540384, 70/53.76540384), (0, 0, -10/53.76540384))]:
            aim(cam, location, target)
            render(scene, variant+'_'+view)
        outer.hide_render = True
        aim(cam, (12, 28, 11), (0, -2.7, -1.4))
        render(scene, variant+'_interior_oblique')
        cavity.hide_render = True
        aim(cam, (0, 24, 6), (0, -2.8, -2.8))
        cam.data.type = 'ORTHO'
        cam.data.ortho_scale = 18.
        render(scene, variant+'_tongue_front')
        cam.data.type = 'PERSP'
        cavity.hide_render = False
    tongue.data.vertices.foreach_set('co', new.ravel())
    tongue.data.update()
    # UV checker reuses the exact frozen layout. It exposes stretch caused by
    # the deformation without pretending that the existing bake remains valid.
    tongue_mat = tongue.data.materials[0]
    nodes = tongue_mat.node_tree.nodes
    bs = nodes.get('Principled BSDF')
    tex = nodes.new('ShaderNodeTexCoord')
    checker = nodes.new('ShaderNodeTexChecker')
    checker.inputs['Color1'].default_value = (.035, .12, .30, 1)
    checker.inputs['Color2'].default_value = (.85, .85, .85, 1)
    checker.inputs['Scale'].default_value = 32
    tongue_mat.node_tree.links.new(tex.outputs['UV'], checker.inputs['Vector'])
    tongue_mat.node_tree.links.new(checker.outputs['Color'], bs.inputs['Base Color'])
    outer.hide_render = True
    cavity.hide_render = True
    for view, location, target in [('front', (0, 24, 6), (0, -2.8, -2.8)), ('oblique', (12, 24, 10), (0, -2.8, -2.8)), ('back', (0, -24, -5), (0, -2.8, -3.5))]:
        cam.data.type = 'ORTHO'
        cam.data.ortho_scale = 19.
        aim(cam, location, target)
        render(scene, 'r06_tongue_checker_'+view)
    shape_report['frozen_parent_file_hashes'] = before_hashes
    assert all(sha(PARENT/name) == digest for name, digest in before_hashes.items())
    shape_report['frozen_parents_unchanged'] = True
    shape_report['review_camera'] = {'horizontal_fov': 39.97, 'native_actor_uniform_scale': .5376540384,
                                    'child_relative_x': .90,
                                    'matched_views': 'Native detail camera positions relative to mouth centre, divided by 53.76540384cm per source unit. Outer at native root, interior child X0.9. No card mask in macro shape evidence.'}
    (OUT/'shape_review.json').write_text(json.dumps(shape_report, indent=2))
    print('LOW_TONGUE_R06_SHAPE_PROOF', json.dumps(shape_report))


if __name__ == '__main__':
    main()
