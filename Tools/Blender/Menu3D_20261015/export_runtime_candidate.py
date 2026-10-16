"""Internal GLB handoff with explicit endpoint transforms/morphs.

Editable sources retain drivers. The temporary export scene samples endpoints,
then removes mouth drivers and exports real geometry/morphs plus original
character actions. Does not bypass final-art acceptance or overwrite exports.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

import bpy
import numpy as np
from mathutils import Matrix

p = argparse.ArgumentParser()
p.add_argument('--candidate', required=True)
p.add_argument('--out', required=True)
p.add_argument('--look', required=True)
p.add_argument('--closed-width', type=float, default=14.2)
p.add_argument('--closed-height', type=int, default=257)
opt = p.parse_args(sys.argv[sys.argv.index('--')+1:])
if not bpy.app.background:
    raise RuntimeError('Use an isolated background Blender process; never replace an open GUI document')
candidate = Path(opt.candidate).resolve()
out = Path(opt.out).resolve()
ROOT = Path(r'E:\DEVGAME\MessControl\ArtSource\UI\MainMenu3D\20261015_refB_v1')
if not out.is_relative_to((ROOT / 'Assembly/CandidateExports').resolve()):
    raise RuntimeError('Only the task-owned CandidateExports directory is allowed')
if any((out / name).exists() for name in ('DAY03_candidate.glb', 'runtime_candidate.json', 'source_endpoint_world_vertices.npz')):
    raise RuntimeError('Preserve existing candidate evidence; choose a fresh versioned output directory')
out.mkdir(parents=True, exist_ok=True)
before = hashlib.sha256(candidate.read_bytes()).hexdigest()
bpy.ops.wm.open_mainfile(filepath=str(candidate))
scene = bpy.context.scene
root = bpy.data.objects['MC_JawsRoot']
root.animation_data_clear()
mouth = set()
for name in ['MC_Jaws_Family', 'MenuGingiva_Geometry']:
    col = bpy.data.collections.get(name)
    if col:
        mouth.update(o for o in col.all_objects if o)
others = set()
for name in ['MenuInterior_Geometry', 'Menu_ExistingCharacter']:
    col = bpy.data.collections[name]
    col.hide_render = col.hide_viewport = False
    others.update(o for o in col.all_objects if o)
selected = sorted(mouth | others, key=lambda o: o.name)
texture_states = []
for obj in selected:
    obj.hide_render = obj.hide_viewport = False
    obj.hide_set(False)
    obj['mcOriginalObjectName'] = obj.name
    for slot in obj.material_slots if obj.type == 'MESH' else []:
        if slot.material:
            slot.material['mcOriginalMaterialName'] = slot.material.name
materials = {slot.material for obj in selected if obj.type == 'MESH' for slot in obj.material_slots if slot.material}
skin_influence_audit = []
for obj in selected:
    if obj.type != 'MESH':
        continue
    rigs = [modifier.object for modifier in obj.modifiers if modifier.type == 'ARMATURE' and modifier.object]
    if not rigs:
        continue
    bone_names = {bone.name for rig in rigs for bone in rig.data.bones}
    group_indices = {group.index for group in obj.vertex_groups if group.name in bone_names}
    discarded = []
    maximum_count = 0
    for vertex in obj.data.vertices:
        weights = sorted((entry.weight for entry in vertex.groups if entry.group in group_indices and entry.weight > 1e-7), reverse=True)
        maximum_count = max(maximum_count,len(weights))
        if len(weights) > 4:
            discarded.append(sum(weights[4:])/sum(weights))
    skin_influence_audit.append({'mesh':obj.name,'vertices':len(obj.data.vertices),
                                 'vertices_above_four_influences':len(discarded),
                                 'maximum_source_influences':maximum_count,
                                 'maximum_discarded_weight_before_runtime_normalization':max(discarded, default=0.),
                                 'p95_discarded_weight_among_affected':float(np.percentile(discarded,95)) if discarded else 0.,
                                 'policy':'Export every influence; local runtime adapter extends the four-influence Three skin shader.'})
for material in materials:
    if material.get('mc_pose_texture_states'):
        texture_states.extend(json.loads(material['mc_pose_texture_states']))

C = Matrix(((1,0,0,0), (0,0,1,0), (0,-1,0,0), (0,0,0,1)))
Ci = C.inverted()
states = {}
endpoint_points = {}
for label, amount in [('closed', 0.), ('quarter', .25), ('half', .5), ('threequarter', .75), ('open', 1.)]:
    root['OpenAmount'] = amount
    root.update_tag()
    scene.frame_set(42)
    bpy.context.view_layer.update()
    dg = bpy.context.evaluated_depsgraph_get()
    state = {}
    for obj in mouth:
        if obj.constraints:
            raise RuntimeError('Mouth constraint requires an explicit runtime bake: ' + obj.name)
        M = obj.evaluated_get(dg).matrix_basis.copy()
        # All current jaw/wrapper contracts use identity ParentInverse. A
        # nonidentity one needs flattening into its actual glTF local matrix.
        if max(abs(obj.matrix_parent_inverse[i][j] - (1. if i == j else 0.))
               for i in range(4) for j in range(4)) > 1e-5:
            raise RuntimeError('Nonidentity ParentInverse: ' + obj.name)
        gltf = C @ M @ Ci
        position, quaternion, scale = gltf.decompose()
        record = {'position': list(position),
                  'quaternion': [quaternion.x, quaternion.y, quaternion.z, quaternion.w],
                  'scale': list(scale), 'raw_blender_matrix': [list(row) for row in M]}
        if obj.type == 'MESH' and obj.data.shape_keys:
            record['morph_names'] = [k.name for k in obj.data.shape_keys.key_blocks][1:]
            record['weights'] = [float(k.value) for k in obj.data.shape_keys.key_blocks][1:]
        if obj.type == 'MESH':
            evaluated = obj.evaluated_get(dg)
            vertices = np.empty(len(evaluated.data.vertices) * 3, dtype=np.float32)
            evaluated.data.vertices.foreach_get('co', vertices)
            vertices = vertices.reshape(-1, 3)
            matrix = np.asarray(evaluated.matrix_world, dtype=np.float64)
            world = vertices @ matrix[:3, :3].T + matrix[:3, 3]
            endpoint_points[label + '::' + obj.name] = world.astype(np.float32)
        state[obj.name] = record
    states[label] = state

pose = {'mode': 'transforms', 'nodes': [], 'morphs': []}
for name, opened in sorted(states['open'].items()):
    closed = states['closed'][name]
    if np.max(np.abs(np.asarray(opened['raw_blender_matrix']) - np.asarray(closed['raw_blender_matrix']))) > 1e-6:
        pose['nodes'].append({'node': name,
                              'closed': {k: closed[k] for k in ('position', 'quaternion', 'scale')},
                              'open': {k: opened[k] for k in ('position', 'quaternion', 'scale')}})
    if opened.get('morph_names'):
        if opened['morph_names'] != closed['morph_names']:
            raise RuntimeError('Endpoint morph identity changed: ' + name)
        channel = {'node': name, 'names': opened['morph_names'],
                   'closed': closed['weights'], 'open': opened['weights']}
        samples = [{'openness': amount, 'weights': states[label][name]['weights']}
                   for label, amount in [('closed',0.),('quarter',.25),('half',.5),('threequarter',.75),('open',1.)]]
        if any(np.max(np.abs(np.asarray(sample['weights']) -
                            (np.asarray(closed['weights'])*(1-sample['openness']) + np.asarray(opened['weights'])*sample['openness']))) > 1e-6
               for sample in samples):
            channel['samples'] = samples
        pose['morphs'].append(channel)

# Static Open basis plus real morph targets. The browser applies the captured
# endpoint state and original character animation independently.
for obj in mouth:
    obj.animation_data_clear()
    obj.matrix_basis = Matrix(states['open'][obj.name]['raw_blender_matrix'])
    if obj.type == 'MESH' and obj.data.shape_keys:
        keys = obj.data.shape_keys
        keys.animation_data_clear()
        for key, value in zip(list(keys.key_blocks)[1:], states['open'][obj.name]['weights']):
            key.value = value
root['OpenAmount'] = 1.
scene.frame_set(0)
bpy.context.view_layer.update()
for material in materials:
    if not material.use_nodes:
        continue
    nt = material.node_tree
    for node in list(nt.nodes):
        if not node.get('mc_pose_texture_mix'):
            continue
        if node.bl_idname != 'ShaderNodeMixRGB' or len(node.inputs[2].links) != 1:
            raise RuntimeError('Unrecognized pose texture Open branch: ' + material.name)
        source_socket = node.inputs[2].links[0].from_socket
        for link in list(node.outputs[0].links):
            destination_socket = link.to_socket
            nt.links.remove(link)
            nt.links.new(source_socket, destination_socket)
    # Node-driver animation is source authoring only. glTF is static Open
    # material plus explicitly declared registered Closed runtime fields.
    if material.get('mc_pose_texture_states'):
        nt.animation_data_clear()
bpy.ops.object.select_all(action='DESELECT')
for obj in selected:
    obj.select_set(True)
bpy.context.view_layer.objects.active = root
path = out / 'DAY03_candidate.glb'
bpy.ops.export_scene.gltf(filepath=str(path), export_format='GLB', use_selection=True,
    export_apply=False, export_yup=True, export_extras=True,
    export_animations=True, export_animation_mode='BROADCAST',
    export_force_sampling=True, export_frame_range=False,
    export_skins=True, export_all_influences=True, export_morph=True, export_morph_normal=True,
    export_morph_animation=True, export_lights=False, export_cameras=False)

raw = path.read_bytes()
magic, version, total = struct.unpack_from('<III', raw)
length, kind = struct.unpack_from('<II', raw, 12)
if magic != 0x46546C67 or version != 2 or kind != 0x4E4F534A or total != len(raw):
    raise RuntimeError('Invalid exported GLB container')
document = json.loads(raw[20:20+length])
clips = [a.get('name', '') for a in document.get('animations', [])]
hello = [name for name in clips if 'A_Teeth_Hello1' in name]
if len(hello) != 1:
    raise RuntimeError('Existing authored Hello1 clip missing/ambiguous: ' + repr(clips))
expected_clips = [a.name for a in bpy.data.actions if a.name.startswith('A_Teeth_')]
missing = [name for name in expected_clips if not any(name in clip for clip in clips)]
if missing:
    raise RuntimeError('Existing compatible character actions omitted: ' + repr(missing))
node_names = {n.get('name', '') for n in document.get('nodes', [])}
for channel in pose['nodes'] + pose['morphs']:
    if channel['node'] not in node_names:
        raise RuntimeError('Pose node omitted from GLB: ' + channel['node'])
for channel in pose['morphs']:
    node = next(n for n in document['nodes'] if n.get('name') == channel['node'])
    mesh = document['meshes'][node['mesh']]
    if mesh.get('extras', {}).get('targetNames') != channel['names']:
        raise RuntimeError('Exported morph names/order changed: ' + channel['node'])
entry = {
    'id': 'DAY03', 'species': 'Vampire', 'status': 'candidate-for-internal-runtime-check',
    'url': str(path), 'closedAspect': 963/opt.closed_height, 'openAspect': 963/410,
    'look': str(Path(opt.look).resolve()),
    'framing': {'up': [0, 1, 0],
                'closed': {'position': [0, 0, 30], 'target': [0, 0, 0], 'width': opt.closed_width},
                'open': {'position': [0, 0, 30], 'target': [0, 0, 0], 'width': 16}},
    'pose': pose,
    'openingOnly': ['Menu_MouthInterior_Root', 'MC_ExistingCharacter_Placement'],
    'visibilityThresholds': {'Menu_MouthInterior_Root':.05, 'MC_ExistingCharacter_Placement':.75},
    'characterAnimation': {'clip': hello[0], 'timeScale': 1},
    'textureStates': texture_states,
}
manifest = {'status': 'UNAPPROVED_INTERNAL_CANDIDATE_REQUIRES_FRESH_IMPORT_AND_BROWSER_REVIEW',
            'source': str(candidate), 'source_sha256': before,
            'source_unchanged': before == hashlib.sha256(candidate.read_bytes()).hexdigest(),
            'glb': str(path), 'glb_sha256': hashlib.sha256(raw).hexdigest(),
            'clips': clips, 'entry': entry, 'endpoint_states': states,
            'skin_influence_audit':skin_influence_audit,
            'shader_limits': ['glTF has no native Blender subsurface scattering.',
                              'Runtime area shadows/indirect transport differ; actual appearance calibration required.'],
            'final_export_or_menu_manifest_modified': False}
(out / 'runtime_candidate.json').write_text(json.dumps(manifest, indent=2), encoding='utf8')
np.savez_compressed(out / 'source_endpoint_world_vertices.npz', **endpoint_points)
print('INTERNAL_RUNTIME_CANDIDATE_READY', path, 'clips', len(clips), flush=True)
