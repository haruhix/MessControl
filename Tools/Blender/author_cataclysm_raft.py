"""Deterministic standalone Cataclysm raft. Never touches a live Blender scene.

Run with Blender --background --factory-startup --disable-autoexec --threads 2.
Build writes source/exports and the first hero; verify imports one delivered file
in a new Blender process and can produce the fixed diagnostic views.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import math
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'ArtSource/CataclysmRaft'
OUT = ROOT / 'Artifacts/CataclysmRaft_20261014'
NAME = 'SM_CataclysmRaft'
DECK_TOP = .375
SEED = 141026


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + '\n', encoding='utf8')


def main():
    import bpy
    import bmesh
    import numpy as np
    from mathutils import Vector
    parser = argparse.ArgumentParser()
    parser.add_argument('--verify', choices=['glb', 'fbx'])
    parser.add_argument('--evidence', action='store_true')
    args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else [])
    SOURCE.mkdir(parents=True, exist_ok=True)
    OUT.mkdir(parents=True, exist_ok=True)
    start = time.monotonic()
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete(use_global=False)
    scene = bpy.context.scene
    scene.unit_settings.system = 'METRIC'
    scene.unit_settings.scale_length = 1
    scene.render.engine = 'BLENDER_EEVEE'
    scene.render.threads_mode = 'FIXED'
    scene.render.threads = 2
    scene.render.image_settings.file_format = 'PNG'
    scene.render.film_transparent = False
    scene.view_settings.view_transform = 'AgX'
    scene.view_settings.look = 'AgX - Medium High Contrast'
    scene.view_settings.exposure = 0
    scene.view_settings.gamma = 1
    scene.world.color = (.055, .055, .055)
    scene.world.use_nodes = True
    scene.world.node_tree.nodes['Background'].inputs[0].default_value = (.055, .075, .095, 1)
    scene.world.node_tree.nodes['Background'].inputs[1].default_value = .45
    parts = []
    rng = np.random.default_rng(SEED)

    def link_mesh(name, vertices, faces):
        mesh = bpy.data.meshes.new(name + '_Mesh')
        mesh.from_pydata(vertices, [], faces)
        mesh.update()
        obj = bpy.data.objects.new(name, mesh)
        scene.collection.objects.link(obj)
        return obj

    def uv_assign(obj, mapper):
        while obj.data.uv_layers:
            obj.data.uv_layers.remove(obj.data.uv_layers[0])
        uv = obj.data.uv_layers.new(name='UVMap')
        obj.data.uv_layers.active_index = 0
        uv.active_render = True
        for face in obj.data.polygons:
            for index in face.loop_indices:
                point = obj.data.vertices[obj.data.loops[index].vertex_index].co
                uv.data[index].uv = mapper(point, face.normal)

    def atlas_texture():
        size = 1024
        color = np.zeros((size, size, 3), dtype=np.float32)
        height = np.zeros((size, size), dtype=np.float32)
        rough = np.ones((size, size), dtype=np.float32) * .80
        for board in range(6):
            low, high = 256 + board * 128, 256 + (board + 1) * 128
            y, x = np.mgrid[0:128, 0:size]
            u, v = x / (size - 1), y / 127
            phase = board * 2.37
            warped_v = v.copy()
            knot_dark = np.zeros_like(v)
            if board in (1, 3, 4):
                ku, kv = {1:(.33,.50), 3:(.67,.40), 4:(.47,.58)}[board]
                du, dv = (u - ku) / .031, (v - kv) / .17
                radius = np.sqrt(du * du + dv * dv)
                # Fibres bend above/below the almond-shaped knot, not through it.
                warped_v += .080 * np.exp(-du * du * .20) * np.tanh(dv * 2) * np.exp(-dv * dv * .20)
                knot_dark = (.22 * np.exp(-radius * radius * 6)
                             + .075 * (.5 + .5 * np.sin(radius * 18)) * np.exp(-radius * radius * .72))
            flow = warped_v * 15 + .62 * np.sin(u * 5 + phase) + .24 * np.sin(u * 13 - phase) + .07 * np.sin(u * 29 + phase)
            grain = -.037 * (.5 + .5 * np.sin(flow * math.tau)) ** 9
            grain += .008 * np.sin(flow * 3.17 * math.tau + u * 7)
            broad = .022 * np.sin(v * 7 + phase) + .011 * np.sin(u * 4.5 - phase)
            base = np.array(((.59,.345,.135),(.63,.39,.170),(.55,.315,.120),
                             (.60,.36,.140),(.56,.325,.115),(.65,.405,.185))[board])
            color[low:high] = np.clip(base + (grain + broad)[:, :, None] * np.array((1, .72, .37))
                                     - knot_dark[:, :, None] * np.array((.85,.64,.31)), .025, .95)
            height[low:high] = grain * .045 - knot_dark * .011
            rough[low:high] = np.clip(.83 - grain * .4 + knot_dark * .12, .75, .95)
        y, x = np.mgrid[0:256, 0:512]
        u, v = x / 511, y / 255
        bark = np.sin((v * 29 + .30 * np.sin(u * 11) + .12 * np.sin(u * 41)) * math.tau)
        color[:256, :512] = np.array((.39, .205, .070)) + bark[:, :, None] * np.array((.032, .022, .012))
        height[:256, :512] = bark * .006
        y, x = np.mgrid[0:256, 0:256]
        u, v = x / 255, y / 255
        rad = np.sqrt(((u - .49) * 1.02) ** 2 + ((v - .48) * .94) ** 2)
        ring = np.sin(rad * 95 + .7 * np.sin(np.arctan2(v - .48, u - .49) * 5))
        color[:256, 512:768] = np.array((.73, .48, .245)) + ring[:, :, None] * np.array((.035, .032, .020))
        height[:256, 512:768] = ring * .0025
        weave = np.sin((u * 11 + v * 18) * math.tau) * .026 + np.sin((u * 5 - v * 18) * math.tau) * .019
        color[:256, 768:] = np.array((.78, .635, .405)) + weave[:, :, None]
        height[:256, 768:] = weave * .09
        rough[:256, 768:] = .90
        dy, dx = np.gradient(height)
        normal = np.stack((-dx * 60, -dy * 60, np.ones_like(height)), axis=-1)
        normal /= np.linalg.norm(normal, axis=-1)[:, :, None]
        textures = {}
        for label, pixels, colorspace in (
            ('BaseColor', color, 'sRGB'), ('Roughness', np.repeat(rough[:, :, None], 3, axis=-1), 'Non-Color'),
            ('Normal', normal * .5 + .5, 'Non-Color')):
            image = bpy.data.images.new('T_CataclysmRaft_' + label, width=size, height=size, alpha=False)
            image.colorspace_settings.name = colorspace
            rgba = np.concatenate((pixels, np.ones((size, size, 1), dtype=np.float32)), axis=-1)
            image.pixels.foreach_set(rgba.ravel())
            image.filepath_raw = str(SOURCE / 'Textures' / (image.name + '.png'))
            image.file_format = 'PNG'
            Path(image.filepath_raw).parent.mkdir(parents=True, exist_ok=True)
            image.save()
            image.pack()
            textures[label] = image
        mat = bpy.data.materials.new('M_CataclysmRaft_Atlas')
        mat.use_nodes = True
        bsdf = mat.node_tree.nodes.get('Principled BSDF')
        bsdf.inputs['Metallic'].default_value = 0
        bsdf.inputs['Roughness'].default_value = .83
        bsdf.inputs['Specular IOR Level'].default_value = .18
        for label, input_name in (('BaseColor', 'Base Color'), ('Roughness', 'Roughness')):
            node = mat.node_tree.nodes.new('ShaderNodeTexImage')
            node.image = textures[label]
            mat.node_tree.links.new(node.outputs['Color'], bsdf.inputs[input_name])
        node = mat.node_tree.nodes.new('ShaderNodeTexImage')
        node.image = textures['Normal']
        normal_node = mat.node_tree.nodes.new('ShaderNodeNormalMap')
        normal_node.inputs['Strength'].default_value = .42
        mat.node_tree.links.new(node.outputs['Color'], normal_node.inputs['Color'])
        mat.node_tree.links.new(normal_node.outputs['Normal'], bsdf.inputs['Normal'])
        return mat

    def wood_box(name, location, dimensions, board, bevel=.025):
        bpy.ops.mesh.primitive_cube_add(size=1, location=location)
        obj = bpy.context.object
        obj.name = name
        obj.scale = dimensions
        bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
        mod = obj.modifiers.new('Rounded_hand_hewn_edges', 'BEVEL')
        mod.width = bevel
        mod.segments = 4
        bpy.ops.object.modifier_apply(modifier=mod.name)
        obj.data.materials.append(material)
        for face in obj.data.polygons:
            face.use_smooth = True
        obj.modifiers.new('Broad_face_normals', 'WEIGHTED_NORMAL')
        dims = Vector(dimensions)
        def mapper(p, n):
            if abs(n.x) > .75:
                return (.505 + (p.y / dims.y + .5) * .240, .008 + (p.z / dims.z + .5) * .234)
            u = .008 + (p.x / dims.x + .5) * .984
            v = p.y / dims.y + .5 if abs(n.z) > .60 else p.z / dims.z + .5
            return (u, .25 + board * .125 + .006 + v * .113)
        uv_assign(obj, mapper)
        obj['bas_role'] = 'deck_plank' if name.startswith('Deck') else 'support_batten'
        parts.append(obj)
        return obj

    def log(name, length, y, offset, phase):
        count, rows = 48, 9
        vertices, faces = [], []
        for row in range(rows):
            t = row / (rows - 1)
            x = (t - .5) * length + offset
            end = .94 if row in (0, rows - 1) else 1
            for i in range(count):
                angle = i * math.tau / count
                radius = .21 * end * (1 + .018 * math.sin(t * 8 + phase) + .012 * math.sin(angle * 3 + phase))
                vertices.append((x, y + math.cos(angle) * radius, math.sin(angle) * radius))
        for row in range(rows - 1):
            for i in range(count):
                a, b = row * count + i, row * count + (i + 1) % count
                faces.append((a, b, b + count, a + count))
        vertices.extend(((offset - length / 2, y, 0), (offset + length / 2, y, 0)))
        for i in range(count):
            faces.append((rows * count, (i + 1) % count, i))
            faces.append((rows * count + 1, (rows - 1) * count + i, (rows - 1) * count + (i + 1) % count))
        obj = link_mesh(name, vertices, faces)
        obj.data.materials.append(material)
        for face in obj.data.polygons:
            face.use_smooth = len(face.vertices) == 4
        def mapper(p, n):
            if abs(n.x) > .75:
                return (.625 + (p.y - y) / .44 * .235, .125 + p.z / .44 * .235)
            angle = math.atan2(p.z, p.y - y) / math.tau % 1
            return (.008 + ((p.x - offset) / length + .5) * .484, .008 + angle * .234)
        uv_assign(obj, mapper)
        obj['bas_role'] = 'log_float'
        parts.append(obj)

    def cord(name, points, radius=.014, closed=False):
        points = [Vector(item) for item in points]
        if closed and (points[-1] - points[0]).length < 1e-8:
            points.pop()
        count, sides = len(points), 8
        vertices, faces = [], []
        for i, point in enumerate(points):
            tangent = (points[(i + 1) % count] - points[(i - 1) % count] if closed
                       else points[min(i + 1, count - 1)] - points[max(i - 1, 0)]).normalized()
            axis = tangent.cross(Vector((0, 0, 1)))
            if axis.length < .1:
                axis = tangent.cross(Vector((0, 1, 0)))
            axis.normalize()
            other = tangent.cross(axis).normalized()
            for j in range(sides):
                a = j * math.tau / sides
                vertices.append(point + radius * (math.cos(a) * axis + math.sin(a) * other))
        for i in range(count if closed else count - 1):
            for j in range(sides):
                a, b = i * sides + j, i * sides + (j + 1) % sides
                c, d = ((i + 1) % count) * sides + (j + 1) % sides, ((i + 1) % count) * sides + j
                faces.append((a, b, c, d))
        if not closed:
            faces.extend((tuple(reversed(range(sides))), tuple((count - 1) * sides + j for j in range(sides))))
        obj = link_mesh(name, vertices, faces)
        obj.data.materials.append(material)
        uv = obj.data.uv_layers.new(name='UVMap')
        for face in obj.data.polygons:
            face.use_smooth = True
            for li in face.loop_indices:
                vi = obj.data.loops[li].vertex_index
                row, column = divmod(vi, sides)
                uv.data[li].uv = (.758 + column / sides * .234, .008 + (row % 12) / 12 * .234)
        obj['bas_role'] = 'rope_binding'
        parts.append(obj)

    def metrics(objects, visual=True):
        result = dict(meshes=[], vertices=0, triangles=0, bounds_min=[1e9]*3, bounds_max=[-1e9]*3,
                      missing_materials=0, invalid_vertices=0, degenerate_faces=0, boundary_edges=0, nonmanifold_edges=0)
        for obj in objects:
            if obj.type != 'MESH':
                continue
            evaluated = obj.evaluated_get(bpy.context.evaluated_depsgraph_get())
            mesh = evaluated.to_mesh()
            mesh.calc_loop_triangles()
            bm = bmesh.new(); bm.from_mesh(mesh)
            item = dict(name=obj.name, vertices=len(mesh.vertices), triangles=len(mesh.loop_triangles), uv_maps=len(mesh.uv_layers),
                        materials=[mat.name for mat in mesh.materials if mat],
                        degenerate_faces=sum(face.calc_area() < 1e-10 for face in bm.faces),
                        boundary_edges=sum(edge.is_boundary for edge in bm.edges),
                        nonmanifold_edges=sum(not edge.is_manifold for edge in bm.edges))
            result['meshes'].append(item)
            for key in ('vertices', 'triangles', 'degenerate_faces', 'boundary_edges', 'nonmanifold_edges'):
                result[key] += item[key]
            result['missing_materials'] += sum(face.material_index >= len(mesh.materials) for face in mesh.polygons)
            for vertex in mesh.vertices:
                point = evaluated.matrix_world @ vertex.co
                for axis in range(3):
                    if not math.isfinite(point[axis]):
                        result['invalid_vertices'] += 1
                    result['bounds_min'][axis] = min(result['bounds_min'][axis], point[axis])
                    result['bounds_max'][axis] = max(result['bounds_max'][axis], point[axis])
            bm.free(); evaluated.to_mesh_clear()
        result['dimensions_m'] = [b-a for a,b in zip(result['bounds_min'], result['bounds_max'])]
        result['material_coverage_status'] = 'checked' if visual else 'not_applicable_nonvisual_collision'
        result['role'] = 'render_geometry' if visual else 'collision_proxy'
        result['passed'] = bool(result['meshes']) and result['invalid_vertices'] == 0 and (not visual or result['missing_materials'] == 0) and result['degenerate_faces'] == 0
        return result

    def aim(obj, target):
        obj.rotation_euler = (Vector(target) - obj.location).to_track_quat('-Z', 'Y').to_euler()

    def studio():
        for name, location, energy, size, color in (
            ('Key_softbox', (1, -3.4, 4.2), 800, 4, (1, .89, .73)),
            ('Fill_softbox', (-3, -1, 2.1), 520, 3, (.76, .85, 1)),
            ('Rim_softbox', (1, 3.2, 3.5), 950, 3, (1, .88, .65)),
            ('Underside_fill', (-1, 1, -2), 160, 3, (.8, .88, 1))):
            data = bpy.data.lights.new(name, 'AREA'); data.energy = energy; data.shape = 'DISK'; data.size = size; data.color = color
            obj = bpy.data.objects.new(name, data); scene.collection.objects.link(obj); obj.location = location; aim(obj, (0, 0, .15))
        data = bpy.data.cameras.new('Raft_Hero_Camera'); data.lens = 55
        camera = bpy.data.objects.new('Raft_Hero_Camera', data); scene.collection.objects.link(camera); scene.camera = camera
        return camera

    def render(camera, path, position, target=(0, 0, .13), size=1024, ortho=None):
        camera.location = position; aim(camera, target)
        camera.data.type = 'ORTHO' if ortho else 'PERSP'
        if ortho: camera.data.ortho_scale = ortho
        scene.render.resolution_x = size; scene.render.resolution_y = size; scene.render.resolution_percentage = 100
        scene.render.filepath = str(path)
        bpy.ops.render.render(write_still=True)

    if args.verify:
        path = SOURCE / (NAME + '.' + args.verify)
        if args.verify == 'glb': bpy.ops.import_scene.gltf(filepath=str(path))
        else: bpy.ops.import_scene.fbx(filepath=str(path))
        imported = [obj for obj in scene.objects if obj.type == 'MESH' and not obj.name.startswith('UCX_')]
        result = metrics(imported)
        write_json(OUT / ('fresh_' + args.verify + '_metrics.json'), result)
        if not result['passed']: raise RuntimeError('Fresh import geometry failed')
        for obj in scene.objects:
            if obj.name.startswith('UCX_'): obj.hide_render = True
        camera = studio()
        render(camera, OUT / ('fresh_' + args.verify + '_hero.png'), (3.8, -4.4, 3.2))
        if args.evidence:
            views = dict(front=((0, -5, .14), 3.2), back=((0, 5, .14), 3.2),
                         left=((-5, 0, .14), 3.2), right=((5, 0, .14), 3.2),
                         top=((0, 0, 5), 3.2), bottom=((0, 0, -5), 3.2))
            for label, (pose, scale) in views.items():
                render(camera, OUT / (label + '.png'), pose, size=512, ortho=scale)
        print(json.dumps(dict(verified=args.verify, triangles=result['triangles'], dimensions_m=result['dimensions_m'], elapsed=time.monotonic()-start)))
        return

    material = atlas_texture()
    for index in range(6):
        wood_box('Deck_Plank_' + str(index+1), ((index % 3-1)*.005, (index-2.5)*(.283333+.012), DECK_TOP-.045),
                 (2.48 + rng.uniform(-.016, .024), .283333, .09), index, bevel=.027)
    for x in (-.81, .81):
        wood_box('Cross_Batten_' + ('Fore' if x > 0 else 'Aft'), (x, 0, .24), (.145, 1.73, .10), 1, bevel=.019)
    log('Float_Log_Port', 2.70, -.64, -.025, .32)
    log('Float_Log_Starboard', 2.76, .64, .022, 1.14)
    for x in (-.81, .81):
        for turn in range(2):
            points = []
            for i in range(96):
                a = i * math.tau / 96
                # Rounded rectangular transverse binding hugs the deck and float bottoms.
                y = .925 * math.copysign(abs(math.cos(a))**.45, math.cos(a))
                z = .065 + .320 * math.copysign(abs(math.sin(a))**.45, math.sin(a))
                points.append((x + (turn-.5)*.037, y, z))
            cord('Deck_Lashing_' + str(len(parts)), points, .015, closed=True)
        for side_sign in (-1, 1):
            y = side_sign*.64
            coil = [(x+.035*math.sin(a*2), y+.234*math.cos(a), .028+.270*math.sin(a))
                    for a in np.linspace(0, math.tau*2, 96)]
            cord('Support_Lashing_' + str(len(parts)), coil, .012)
            knot = [(x+.045*math.sin(t*math.tau*2), side_sign*.93+.022*math.sin(t*math.tau*4),
                     .22+.048*math.cos(t*math.tau*2)) for t in np.linspace(0, 1, 64)]
            cord('Lashing_Knot_' + str(len(parts)), knot, .014)
            tail = [(x+.035*t, side_sign*(.938+.030*t), .22-.16*t+.022*math.sin(t*6)) for t in np.linspace(0,1,24)]
            cord('Rope_Tail_' + str(len(parts)), tail, .012)
    # Normals of procedural tubes/logs are corrected as a whole before export.
    for obj in parts:
        bm = bmesh.new(); bm.from_mesh(obj.data); bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces)); bm.to_mesh(obj.data); bm.free()
    authored = metrics(parts)
    write_json(OUT / 'authored_metrics.json', authored)
    if not authored['passed'] or authored['triangles'] > 45000: raise RuntimeError('Authored asset failed gates')
    bpy.ops.object.select_all(action='DESELECT')
    for obj in parts: obj.select_set(True)
    bpy.context.view_layer.objects.active = parts[0]
    bpy.ops.object.duplicate(linked=False)
    duplicates = list(bpy.context.selected_objects)
    # Freeze each part's own refinement before joining. A plank's Weighted
    # Normal modifier must never be evaluated on the unrelated log end caps.
    for duplicate in duplicates:
        bpy.context.view_layer.objects.active = duplicate
        for modifier in list(duplicate.modifiers):
            bpy.ops.object.modifier_apply(modifier=modifier.name)
    bpy.context.view_layer.objects.active = duplicates[0]
    bpy.ops.object.join()
    export_obj = bpy.context.object; export_obj.name = NAME
    export_obj.data.name = NAME + '_Mesh'
    scene.cursor.location = (0,0,0); bpy.ops.object.origin_set(type='ORIGIN_CURSOR')
    for modifier in list(export_obj.modifiers):
        bpy.ops.object.modifier_apply(modifier=modifier.name)
    triangulate = export_obj.modifiers.new('Portable_export_triangulation', 'TRIANGULATE')
    bpy.ops.object.modifier_apply(modifier=triangulate.name)
    colliders = []
    for index, (location, dimensions) in enumerate((((0,0,DECK_TOP-.045),(2.40,1.70,.09)),
                                                  ((-.025,-.64,0),(2.70,.42,.42)), ((.022,.64,0),(2.76,.42,.42)))):
        bpy.ops.mesh.primitive_cube_add(size=1, location=location)
        obj=bpy.context.object; obj.name=f'UCX_{NAME}_{index:02d}'; obj.scale=dimensions
        bpy.ops.object.transform_apply(location=True, rotation=True, scale=True); obj.hide_render=True; colliders.append(obj)
    bpy.ops.object.select_all(action='DESELECT'); export_obj.select_set(True); bpy.context.view_layer.objects.active=export_obj
    bpy.ops.export_scene.gltf(filepath=str(SOURCE/(NAME+'.glb')), export_format='GLB', use_selection=True,
                              export_apply=True, export_animations=False, export_tangents=True, export_image_format='AUTO')
    for obj in colliders: obj.select_set(True)
    bpy.ops.export_scene.fbx(filepath=str(SOURCE/(NAME+'.fbx')), use_selection=True, object_types={'MESH'},
                             apply_unit_scale=True, apply_scale_options='FBX_SCALE_ALL', bake_space_transform=True,
                             mesh_smooth_type='FACE', use_tspace=True, bake_anim=False, path_mode='COPY', embed_textures=True,
                             axis_forward='-Z', axis_up='Y')
    collision=metrics(colliders, visual=False)
    collision['hulls'] = []
    for obj in colliders:
        bm=bmesh.new(); bm.from_mesh(obj.data); bm.transform(obj.matrix_world); bm.normal_update()
        volume=bm.calc_volume(signed=True)
        convex=all((v.co-face.verts[0].co).dot(face.normal)<=1e-5 for face in bm.faces for v in bm.verts)
        hull=dict(name=obj.name, closed=all(edge.is_manifold for edge in bm.edges), convex=convex,
                  signed_volume_m3=volume, passed=volume>0 and convex and all(edge.is_manifold for edge in bm.edges))
        collision['hulls'].append(hull); bm.free()
    collision['passed'] = collision['passed'] and all(item['passed'] for item in collision['hulls'])
    write_json(OUT/'collision_metrics.json', collision)
    if not collision['passed']: raise RuntimeError('UCX hull preflight failed')
    export_obj.hide_render=True
    for obj in colliders: obj.hide_viewport=True
    camera=studio()
    camera.location=(3.8,-4.4,3.2); aim(camera,(0,0,.13))
    scene.render.resolution_x=1024; scene.render.resolution_y=1024
    for orphan in list(bpy.data.materials):
        if orphan.users == 0: bpy.data.materials.remove(orphan)
    bpy.ops.wm.save_as_mainfile(filepath=str(SOURCE/(NAME+'.blend')))
    render(camera, OUT/'hero.png', (3.8,-4.4,3.2))
    manifest=dict(asset=NAME, blender=bpy.app.version_string, units='meters', origin=[0,0,0],
                  origin_semantics='intended waterline', deck_top_m=DECK_TOP, intended_walkable_rectangle_cm=[240,170],
                  visual_deck_cm=[250,176], capacity='Two-player limit is future gameplay logic; no runtime validation',
                  collision_hulls=[obj.name for obj in colliders], texture_atlas={'resolution':[1024,1024], 'maps':['BaseColor','Roughness','Normal'], 'normal_convention':'OpenGL +Y; Unreal FlipGreenChannel required unless explicitly converted'},
                  triangles=authored['triangles'], exports={ext:str(SOURCE/(NAME+'.'+ext)) for ext in ('blend','glb','fbx')},
                  preview=str(OUT/'hero.png'), engine_import_validation='Deferred; Unreal/editor untouched',
                  provenance='Original deterministic geometry and texture pixels; museum images viewed as construction analogues only',
                  source_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(), elapsed_seconds=time.monotonic()-start)
    write_json(SOURCE/'manifest.json', manifest)
    print(json.dumps(manifest))


if __name__ == '__main__': main()
