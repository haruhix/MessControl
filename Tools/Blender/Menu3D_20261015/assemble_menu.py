"""Reference-B menu asset assembly. Sources are appended, never edited in place.

Run in an isolated Blender process only, with a lead-owned render/bake slot.
"""
import argparse
import fnmatch
import hashlib
import json
import math
from pathlib import Path
import sys

import bpy
from mathutils import Matrix, Vector
from mathutils.bvhtree import BVHTree

PROJECT = Path(r'E:\DEVGAME\MessControl')
ROOT = PROJECT / 'ArtSource/UI/MainMenu3D/20261015_refB_v1'
OUT = ROOT / 'Assembly'
args = sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else []
p = argparse.ArgumentParser()
p.add_argument('--revision', default='look01')
p.add_argument('--render', action='store_true')
p.add_argument('--final-textures', action='store_true')
p.add_argument('--samples', type=int, default=24)
p.add_argument('--edge', type=int, default=960)
p.add_argument('--device', choices=['CPU', 'OPTIX'], default='OPTIX')
p.add_argument('--exports', action='store_true')
p.add_argument('--binding', default='material_binding.json')
p.add_argument('--closed-binding', help='Optional same-UV0 semantic Closed material maps; Open binding stays unchanged')
p.add_argument('--surface-render-mode', choices=['pbr','baked-radiance'], default='pbr', help='Explicit baked-lighting alternative on the same real meshes; never called recovered albedo')
p.add_argument('--hero-only', action='store_true')
p.add_argument('--jaws-file', default='Jaws/MC_Jaws_Family.blend')
p.add_argument('--interior-file', default='Interior/MouthInterior.blend')
p.add_argument('--gums-file', help='Optional separate source-locked MenuGingiva_Geometry collection')
p.add_argument('--legacy-interior-placement', action='store_true')
p.add_argument('--reference-camera', action='store_true')
p.add_argument('--closed-reference-width', type=float, help='Explicit fitted closed camera width, e.g.14.2; no inferred default')
p.add_argument('--closed-reference-height', type=int, default=257, help='Canonical closed source pixel height at width963')
p.add_argument('--open-only', action='store_true')
p.add_argument('--untextured', action='store_true')
p.add_argument('--no-character', action='store_true')
p.add_argument('--interior-only', action='store_true')
p.add_argument('--look-config', help='Optional JSON candidate lighting/color overrides; source files remain unchanged')
opt = p.parse_args(args)
look_config = json.loads(Path(opt.look_config).read_text(encoding='utf8')) if opt.look_config else {}
OUT.mkdir(parents=True, exist_ok=True)
if opt.exports:
    acceptance_path = OUT / 'quality_acceptance.json'
    acceptance = json.loads(acceptance_path.read_text(encoding='utf8')) if acceptance_path.exists() else {}
    if acceptance.get('status') != 'accepted_by_lead_after_reference_review':
        raise RuntimeError('Reference/geometry/look review is unfinished; candidate export is not a final deliverable')
    for relative, expected in acceptance.get('source_sha256', {}).items():
        actual = hashlib.sha256((ROOT/relative).read_bytes()).hexdigest()
        if actual != expected:
            raise RuntimeError(f'Accepted source changed: {relative}')
REVIEW = OUT / 'Review' / opt.revision
REVIEW.mkdir(parents=True, exist_ok=True)
bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
scene.name = 'MC_Menu3D_ReferenceB'
sources = []

def append_collection(path, name):
    sources.append({'file': str(path), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()})
    with bpy.data.libraries.load(str(path), link=False) as (src, dst):
        if name not in src.collections:
            raise RuntimeError(f'Missing collection {name}: {src.collections}')
        dst.collections = [name]
    col = dst.collections[0]
    scene.collection.children.link(col)
    return col

jaws = append_collection(ROOT / opt.jaws_file, 'MC_Jaws_Family')
interior = append_collection(ROOT / opt.interior_file, 'MenuInterior_Geometry')
for col in [jaws, *jaws.children_recursive, interior]:
    col.hide_render = col.hide_viewport = False
for ob in [*jaws.all_objects, *interior.all_objects]:
    ob.hide_render = False
    ob.hide_viewport = False
    ob.hide_set(False)
root = bpy.data.objects['MC_JawsRoot']
# Imported diagnostic actions must not override the assembly's single control
# while the original character is sampled at frame0 for placement.
root.animation_data_clear()
root['OpenAmount'] = 1.0
root.update_tag()
bpy.context.view_layer.update()
pose_bindings = []
if opt.gums_file:
    if any(slot.material and slot.material.name == 'MC_Gum'
           for obj in jaws.all_objects if obj.type == 'MESH' for slot in obj.material_slots):
        raise RuntimeError('Separate gum source cannot be combined with legacy gum geometry')
    gingiva = append_collection(ROOT / opt.gums_file, 'MenuGingiva_Geometry')
    gingiva.hide_render = gingiva.hide_viewport = False
    # Appending a keyed gum collection can append private parent/driver IDs from
    # its diagnostic file. Explicitly bind the copied gums to the current jaws.
    # Otherwise a hidden MC_JawsRoot.001 silently drives a different pose.
    for ob in gingiva.all_objects:
        ids = [ob]
        if ob.type == 'MESH' and ob.data.shape_keys:
            ids.append(ob.data.shape_keys)
        for datablock in ids:
            if not datablock.animation_data:
                continue
            for fc in datablock.animation_data.drivers:
                for variable in fc.driver.variables:
                    for target in variable.targets:
                        if target.id and target.data_path == '["OpenAmount"]':
                            previous = target.id.name
                            target.id = root
                            pose_bindings.append({'owner': datablock.name,
                                                  'channel': fc.data_path,
                                                  'old_control': previous,
                                                  'control': root.name})
    for side in ('Upper', 'Lower'):
        obj = bpy.data.objects['MC_Gingiva_' + side]
        # Canonical gum wrappers are identity; mesh basis/key endpoints contain
        # the geometry. Preserve that contract rather than an imported closed
        # diagnostic parent's currently evaluated world transform.
        if max(abs(obj.matrix_basis[i][j] - (1. if i == j else 0.))
               for i in range(4) for j in range(4)) > 1e-5:
            raise RuntimeError('Nonidentity gum wrapper needs an explicit pose contract: ' + obj.name)
        obj.parent = bpy.data.objects['MC_Jaw_DAY03_' + side]
        obj.matrix_parent_inverse = Matrix.Identity(4)
        obj.matrix_basis = Matrix.Identity(4)
    bpy.context.view_layer.update()
for ob in (interior.all_objects if opt.legacy_interior_placement else []):
    if ob.type != 'MESH':
        continue
    if 'Tongue' in ob.name:
        # The tongue is the floor behind the lower teeth, not a foreground lip.
        # Placement preserves the approved Painter UV surface exactly.
        ob.location.y += .90
        ob.location.z -= .65
    else:
        # The front shell return lies outside the final camera crop, behind gum.
        ob.scale = (1.15,)*3

# Reuse the current mesh/skin/rig, then compatible artist-authored actions only.
character = bpy.data.collections.new('Menu_ExistingCharacter')
scene.collection.children.link(character)
charpath = PROJECT / 'ArtSource/CharacterCurrent/CharacterGameplay.blend'
sources.append({'file': str(charpath), 'sha256': hashlib.sha256(charpath.read_bytes()).hexdigest()})
with bpy.data.libraries.load(str(charpath), link=False) as (src, dst):
    dst.objects = list(src.objects)
for ob in dst.objects:
    if ob:
        character.objects.link(ob)
        ob.hide_render = ob.hide_viewport = False
        ob.hide_set(False)
rig = next(ob for ob in character.objects if ob.type == 'ARMATURE')
mesh = next(ob for ob in character.objects if ob.type == 'MESH')
anchor = bpy.data.objects.new('MC_ExistingCharacter_Placement', None)
character.objects.link(anchor)
rig.parent = anchor
anchor.location = (-5.757, 2.889, 0.)
anchor.scale = (2.0,)*3
anchor.rotation_euler.z = math.radians(165)
# Current source front is -Y; no skeleton axis edits or retargeting.
rig.animation_data_clear()
animpath = PROJECT / 'ArtSource/CharacterAnimation/Teeth3/Teeth3GameplayAnimations.blend'
with bpy.data.libraries.load(str(animpath), link=False) as (src, dst):
    dst.actions = [n for n in src.actions if n.startswith('A_Teeth_')]
actions = [a for a in dst.actions if a]
for a in actions:
    a.use_fake_user = True
hello = next(a for a in actions if a.name == 'A_Teeth_Hello1')
rig.animation_data_create()
rig.animation_data.action = hello
if len(hello.slots):
    rig.animation_data.action_slot = hello.slots[0]
scene.render.fps = 30
scene.frame_set(0)
grounding = {'status': 'not sampled'}
if not opt.no_character:
    bpy.context.view_layer.update()
    dg = bpy.context.evaluated_depsgraph_get()
    foot_groups = {g.index for g in mesh.vertex_groups if g.name.startswith(('foot_', 'toes_'))}
    indices = [v.index for v in mesh.data.vertices if sum(g.weight for g in v.groups if g.group in foot_groups) > .45]
    tongue = next((o for o in interior.all_objects if o.type == 'MESH' and 'Tongue' in o.name), None)
    if not indices or tongue is None:
        raise RuntimeError('Existing character foot groups/tongue unavailable; refusing unverified floating placement')
    evaluated = mesh.evaluated_get(dg)
    evaluated_mesh = evaluated.to_mesh()
    points = [evaluated.matrix_world @ evaluated_mesh.vertices[i].co for i in indices]
    evaluated.to_mesh_clear()
    minimum_z = min(v.z for v in points)
    sole = [v for v in points if v.z <= minimum_z + .035]
    point = sum(sole, Vector()) / len(sole)
    tree = BVHTree.FromObject(tongue, dg)
    inv = tongue.matrix_world.inverted()
    origin = inv @ Vector((point.x, point.y, point.z + 20))
    direction = (inv.to_3x3() @ Vector((0, 0, -1))).normalized()
    hit, normal, face, distance = tree.ray_cast(origin, direction, 100)
    if hit is None:
        raise RuntimeError('Existing posed feet are outside tongue floor')
    contact = tongue.matrix_world @ hit
    anchor.location.z += contact.z - minimum_z + .008
    bpy.context.view_layer.update()
    grounding = {'status': 'lowest posed sole aligned to actual tongue ray hit at frame0',
                 'foot_vertices': len(indices), 'floor_world': list(contact), 'clearance': .008,
                 'anchor': list(anchor.location), 'motion_contact': 'Greeting clip contact still requires motion review'}
character['source_mesh'] = str(charpath)
character['source_actions'] = str(animpath)
character['integration_policy'] = 'Existing character copied unchanged; placement only, existing actions retained. Excluded Worker draft is not loaded.'

def input_value(bsdf, name, value):
    if name in bsdf.inputs:
        bsdf.inputs[name].default_value = value

binding_arg = Path(opt.binding)
if binding_arg.is_absolute():
    binding_path = binding_arg
else:
    # Historical texture-relative paths and explicit art-root-relative paths
    # are both supported. Never silently render a supplied missing binding.
    candidates = [p for p in (ROOT / 'Textures' / binding_arg, ROOT / binding_arg) if p.exists()]
    if len(candidates) > 1 and candidates[0].resolve() != candidates[1].resolve():
        raise RuntimeError('Ambiguous binding; supply an absolute path: ' + opt.binding)
    binding_path = candidates[0] if candidates else ROOT / 'Textures' / binding_arg
if not opt.untextured and opt.binding != 'material_binding.json' and not binding_path.exists():
    raise RuntimeError('Explicit texture binding is missing: ' + str(binding_path))
bindings = json.loads(binding_path.read_text(encoding='utf-8')) if binding_path.exists() and not opt.untextured else {}
if opt.final_textures and not bindings:
    raise RuntimeError('Final Painter texture binding is required but missing')
bindings = bindings.get('materials', bindings)
closed_bindings = {}
if opt.closed_binding:
    closed_path = Path(opt.closed_binding)
    if not closed_path.is_absolute():
        closed_path = ROOT / closed_path
    if not closed_path.is_file():
        raise RuntimeError('Explicit Closed texture binding is missing: ' + str(closed_path))
    closed_bindings = json.loads(closed_path.read_text(encoding='utf8'))
    closed_bindings = closed_bindings.get('materials', closed_bindings)
if opt.surface_render_mode == 'baked-radiance':
    # Dedicated provenance key stays distinct in the files. Only this explicit
    # alternative temporarily binds it to the color socket used for radiance.
    for material_name in ('MC_Enamel','MC_Gum','MC_Interior','MC_Tongue'):
        field = bindings.get(material_name, {}).get('radiance_not_albedo')
        if not field:
            raise RuntimeError('Explicit baked-radiance atlas missing: ' + material_name)
        bindings[material_name] = {**bindings[material_name], 'base_color':field, 'uv_map':'UV0'}
    for material_name, binding in list(closed_bindings.items()):
        if binding.get('radiance_not_albedo'):
            closed_bindings[material_name] = {**binding, 'base_color':binding['radiance_not_albedo'], 'uv_map':'UV0'}

def bind_texture(mat, filename, color, socket, normal=False, normal_strength=1.0):
    if not filename:
        return
    path = Path(filename)
    if not path.is_file():
        raise RuntimeError(f'Missing image {path}')
    im = bpy.data.images.load(str(path), check_existing=True)
    im.colorspace_settings.name = 'sRGB' if color else 'Non-Color'
    nodes, links = mat.node_tree.nodes, mat.node_tree.links
    tex = nodes.new('ShaderNodeTexImage')
    tex.image = im
    tex.label = path.name
    bsdf = next(n for n in nodes if n.type == 'BSDF_PRINCIPLED')
    if normal:
        n = nodes.new('ShaderNodeNormalMap')
        n.inputs['Strength'].default_value = normal_strength
        links.new(tex.outputs['Color'], n.inputs['Color'])
        links.new(n.outputs['Normal'], bsdf.inputs[socket])
    else:
        links.new(tex.outputs['Color'], bsdf.inputs[socket])
    return tex

def scalar_mask(mat, filename, destinations):
    """One documented anatomy mask drives related shader ranges."""
    if not filename:
        return
    im = bpy.data.images.load(str(filename), check_existing=True)
    im.colorspace_settings.name = 'Non-Color'
    nodes, links = mat.node_tree.nodes, mat.node_tree.links
    tex = nodes.new('ShaderNodeTexImage')
    tex.image = im
    tex.label = Path(filename).name
    bsdf = next(n for n in nodes if n.type == 'BSDF_PRINCIPLED')
    for name, low, high in destinations:
        if name not in bsdf.inputs:
            continue
        m = nodes.new('ShaderNodeMapRange')
        m.label = f'{name}: {low} to {high}'
        m.inputs['From Min'].default_value = 0.
        m.inputs['From Max'].default_value = 1.
        m.inputs['To Min'].default_value = low
        m.inputs['To Max'].default_value = high
        m.clamp = True
        links.new(tex.outputs['Color'], m.inputs['Value'])
        links.new(m.outputs['Result'], bsdf.inputs[name])


def bind_portable_specular(mat, filename):
    if not filename:
        return
    im = bpy.data.images.load(str(filename), check_existing=True)
    im.colorspace_settings.name = 'Non-Color'
    nt = mat.node_tree
    tex = nt.nodes.new('ShaderNodeTexImage')
    tex.image = im
    tex.label = Path(filename).name
    multiply = nt.nodes.new('ShaderNodeMath')
    multiply.operation = 'MULTIPLY'
    multiply.inputs[1].default_value = .5
    multiply.label = 'Portable specular: glTF Alpha, Blender .5*Alpha'
    nt.links.new(tex.outputs['Alpha'], multiply.inputs[0])
    bsdf = next(n for n in nt.nodes if n.type == 'BSDF_PRINCIPLED')
    nt.links.new(multiply.outputs[0], bsdf.inputs['Specular IOR Level'])


def bind_pose_textures(mat, opened, closed):
    """Blend registered semantic material fields, never lit render images.

    The source scene retains editable native nodes/drivers. Internal GLB export
    temporarily flattens these tagged Mix nodes to the Open branch and records
    the Closed maps for the corresponding runtime shader adapter.
    """
    if not closed:
        return []
    if opened.get('uv_map') != 'UV0' or closed.get('uv_map') != 'UV0':
        raise RuntimeError('Pose material maps require the same named UV0: ' + mat.name)
    nt = mat.node_tree
    bsdf = next(n for n in nt.nodes if n.type == 'BSDF_PRINCIPLED')
    channels = [('base_color', 'Base Color', True), ('roughness', 'Roughness', False),
                ('coat_weight', 'Coat Weight', False), ('coat_roughness', 'Coat Roughness', False),
                ('specular_ior_level', 'Specular IOR Level', False)]
    descriptors = []
    control = None
    for channel, socket_name, color in channels:
        old, new = opened.get(channel), closed.get(channel)
        if not new or new == old:
            continue
        if not old or not Path(old).is_file() or not Path(new).is_file():
            raise RuntimeError('Missing registered state texture: ' + mat.name + '/' + channel)
        socket = bsdf.inputs[socket_name]
        if not socket.links:
            raise RuntimeError('Open branch must have an explicit map: ' + mat.name + '/' + channel)
        open_source = socket.links[0].from_socket
        if control is None:
            control = nt.nodes.new('ShaderNodeValue')
            control.name = 'MC_MaterialOpenAmount'
            fc = control.outputs[0].driver_add('default_value')
            fc.driver.type = 'SCRIPTED'
            var = fc.driver.variables.new()
            var.name = 'amount'
            var.targets[0].id = root
            var.targets[0].data_path = '["OpenAmount"]'
            fc.driver.expression = 'amount'
        tex = nt.nodes.new('ShaderNodeTexImage')
        tex.image = bpy.data.images.load(str(new), check_existing=True)
        tex.image.colorspace_settings.name = 'sRGB' if color else 'Non-Color'
        tex.label = 'Closed ' + channel
        closed_source = tex.outputs['Color']
        if channel == 'specular_ior_level':
            scale = nt.nodes.new('ShaderNodeMath')
            scale.operation = 'MULTIPLY'
            scale.inputs[1].default_value = .5
            nt.links.new(tex.outputs['Alpha'], scale.inputs[0])
            closed_source = scale.outputs[0]
        mix = nt.nodes.new('ShaderNodeMixRGB')
        mix.label = 'Registered Closed/Open ' + channel
        mix['mc_pose_texture_mix'] = channel
        mix.blend_type = 'MIX'
        nt.links.new(control.outputs[0], mix.inputs[0])
        nt.links.new(closed_source, mix.inputs[1])
        nt.links.new(open_source, mix.inputs[2])
        nt.links.new(mix.outputs[0], socket)
        descriptors.append({'material':mat.name,'channel':channel,'closed':str(new),'open':str(old),'uv_map':'UV0'})
    if descriptors:
        mat['mc_pose_texture_states'] = json.dumps(descriptors)
    return descriptors

look = {
    'MC_Enamel': ((.89, .66, .50, 1), .235, .16, .025),
    'MC_Gum': ((.58, .035, .054, 1), .255, .10, .045),
    'MC_Interior': ((.16, .009, .021, 1), .34, .14, .06),
    'MC_Tongue': ((.56, .065, .095, 1), .29, .30, .10),
}
material_provenance = {}
texture_states = []
for name, (color, rough, coat, sss) in look.items():
    mat = bpy.data.materials.get(name)
    if not mat:
        if opt.final_textures:
            raise RuntimeError(f'Missing final material {name}')
        material_provenance[name] = {'status': 'Geometry candidate does not yet contain this part; not a final asset'}
        continue
    bsdf = next(n for n in mat.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
    for socket_name in ('Base Color', 'Roughness', 'Normal', 'Coat Weight', 'Coat Roughness', 'Subsurface Weight', 'Subsurface Scale', 'Transmission Weight'):
        if socket_name in bsdf.inputs:
            for link in list(bsdf.inputs[socket_name].links):
                mat.node_tree.links.remove(link)
    for key, val in [('Base Color', color), ('Roughness', rough), ('Coat Weight', coat), ('Coat Roughness', .21), ('Subsurface Weight', sss), ('Subsurface Scale', .014), ('Subsurface Radius', (1., .33, .16)), ('IOR', 1.50 if name == 'MC_Enamel' else 1.40)]:
        input_value(bsdf, key, val)
    binding = bindings.get(name, {})
    if binding:
        constants = binding.get('shader_constants', {})
        for key, socket_name in [('ior', 'IOR'), ('coat_roughness', 'Coat Roughness'),
                                  ('subsurface_scale', 'Subsurface Scale'), ('subsurface_radius', 'Subsurface Radius'),
                                  ('subsurface_weight', 'Subsurface Weight'), ('metallic', 'Metallic'),
                                  ('transmission_weight', 'Transmission Weight'),
                                  ('specular_ior_level', 'Specular IOR Level')]:
            if key in constants:
                input_value(bsdf, socket_name, constants[key])
        bind_texture(mat, binding.get('base_color'), True, 'Base Color')
        bind_texture(mat, binding.get('roughness'), False, 'Roughness')
        bind_texture(mat, binding.get('normal_opengl'), False, 'Normal', True, constants.get('normal_strength', 1.0))
        coat_range = constants.get('coat_weight_range', [.03, .27])
        wet_destinations = [('Coat Weight', *coat_range)]
        if 'specular_ior_level_range' in constants:
            wet_destinations.append(('Specular IOR Level', *constants['specular_ior_level_range']))
        if 'coat_roughness' not in constants:
            wet_destinations.append(('Coat Roughness', .24, .10))
        scalar_mask(mat, binding.get('wetness'), wet_destinations)
        sss_range = constants.get('subsurface_weight_range', [.02, .15])
        sss_destinations = [('Subsurface Weight', *sss_range)]
        if 'subsurface_scale' not in constants:
            sss_destinations.append(('Subsurface Scale', .006, .022))
        scalar_mask(mat, binding.get('subsurface'), sss_destinations)
        scalar_mask(mat, binding.get('incisal'), [
            ('Subsurface Weight', *constants.get('incisal_subsurface_weight_range', [.015, .065])),
            ('Subsurface Scale', *constants.get('incisal_subsurface_scale_range', [.008, .025])),
            ('Transmission Weight', *constants.get('incisal_transmission_weight_range', [0., .035]))])
        # Explicit maps replace the unsupported range math for portable exports.
        bind_texture(mat, binding.get('coat_weight'), False, 'Coat Weight')
        bind_texture(mat, binding.get('coat_roughness'), False, 'Coat Roughness')
        bind_portable_specular(mat, binding.get('specular_ior_level'))
        material_provenance[name] = binding
    else:
        material_provenance[name] = {'status': 'Constant lookdev material; Painter final exports pending'}
    texture_states.extend(bind_pose_textures(mat, binding, closed_bindings.get(name, {})))
    for socket_name, value in look_config.get('materials', {}).get(name, {}).items():
        if socket_name in bsdf.inputs:
            for link in list(bsdf.inputs[socket_name].links):
                mat.node_tree.links.remove(link)
            input_value(bsdf, socket_name, value)
    if opt.surface_render_mode == 'baked-radiance':
        if not bsdf.inputs['Base Color'].links:
            raise RuntimeError('Baked radiance requires an explicit registered surface atlas: ' + name)
        nt = mat.node_tree
        output = next(n for n in nt.nodes if n.type == 'OUTPUT_MATERIAL' and n.is_active_output)
        if not output.inputs['Surface'].links:
            raise RuntimeError('Original non-camera material surface is missing: ' + name)
        original_surface = output.inputs['Surface'].links[0].from_socket
        radiance = bsdf.inputs['Base Color'].links[0].from_socket
        path = nt.nodes.new('ShaderNodeLightPath')
        path.label = 'Baked lighting: camera radiance only'
        mix = nt.nodes.new('ShaderNodeMixShader')
        mix.label = 'Real mesh baked-radiance alternative (not albedo)'
        nt.links.new(path.outputs['Is Camera Ray'], mix.inputs[0])
        nt.links.new(original_surface, mix.inputs[1])
        # Blender's RGBA->Shader conversion emits unit radiance. The official
        # glTF exporter recognizes this exact camera/lightpath graph as unlit.
        nt.links.new(radiance, mix.inputs[2])
        nt.links.new(mix.outputs[0], output.inputs['Surface'])
        mat['mc_surface_render_mode'] = 'BAKED_RADIANCE_NOT_ALBEDO'

def area(name, position, target, energy, color, size, size_y=None):
    d = bpy.data.lights.new(name, 'AREA')
    d.energy, d.color, d.size = energy, color, size
    if size_y:
        d.shape = 'RECTANGLE'
        d.size_y = size_y
    else:
        d.shape = 'DISK'
    ob = bpy.data.objects.new(name, d)
    scene.collection.objects.link(ob)
    ob.location = position
    ob.rotation_euler = (Vector(target)-ob.location).to_track_quat('-Z', 'Y').to_euler()
    return ob

area('Menu_Key_Softbox', (-5, -7, 7), (0, .5, 0), 2300, (1., .87, .76), 7, 5)
area('Menu_Fill_Softbox', (5, -6, 2), (0, 1, 0), 1400, (.78, .89, 1.), 6, 7)
area('Menu_Enamel_LongHighlight', (0, -4, 8), (0, .5, .5), 1200, (1., .94, .85), 10, 2)
area('Menu_Coral_InteriorBounce', (-2, 1, 1), (0, 4, -2), 140, (1., .26, .24), 5)
area('Menu_Violet_Edge', (8, 2, 1), (2, 0, -.2), 180, (.46, .48, 1.), 5)
if 'lights' in look_config:
    for obj in list(scene.objects):
        if obj.type == 'LIGHT':
            bpy.data.objects.remove(obj, do_unlink=True)
    for light in look_config['lights']:
        obj = area(light['name'], light['position'], light['target'], light['energy'],
                   light.get('color', [1., 1., 1.]), light['size'], light.get('size_y'))
        if 'specular_factor' in light:
            obj.data.specular_factor = light['specular_factor']
        if 'diffuse_factor' in light:
            obj.data.diffuse_factor = light['diffuse_factor']
        if 'use_shadow' in light:
            obj.data.use_shadow = light['use_shadow']
        if 'roll_degrees' in light:
            obj.rotation_euler.rotate_axis('Z', math.radians(light['roll_degrees']))
        if light.get('receiver_objects') or light.get('receiver_materials'):
            receivers = bpy.data.collections.new(light['name'] + '_Receivers')
            for receiver in scene.objects:
                if receiver.type != 'MESH':
                    continue
                object_match = any(fnmatch.fnmatchcase(receiver.name, pattern)
                                   for pattern in light.get('receiver_objects', []))
                material_match = any(slot.material and slot.material.name in light.get('receiver_materials', [])
                                     for slot in receiver.material_slots)
                if object_match or material_match:
                    receivers.objects.link(receiver)
            if not receivers.objects:
                raise RuntimeError(f"Light has no matching receiver: {light['name']}")
            obj.light_linking.receiver_collection = receivers
        # Cycles 5.1 sync_light does not consume Light.diffuse/specular_factor.
        # Ray visibility is consumed by sync_object. Decompose unequal lobe
        # energies into two coincident area emitters for actual Cycles parity.
        diffuse_factor = light.get('diffuse_factor', 1.)
        specular_factor = light.get('specular_factor', 1.)
        if diffuse_factor != specular_factor:
            obj.data.energy = light['energy'] * diffuse_factor
            obj.visible_glossy = False
            obj.visible_diffuse = diffuse_factor > 0
            if specular_factor > 0:
                reflection = obj.copy()
                reflection.data = obj.data.copy()
                reflection.name = light['name'] + '_CyclesGlossy'
                scene.collection.objects.link(reflection)
                reflection.data.energy = light['energy'] * specular_factor
                reflection.visible_diffuse = False
                reflection.visible_glossy = True
                reflection.visible_transmission = False
                reflection.visible_volume_scatter = False
scene.world = bpy.data.worlds.new('Menu_BurgundyWorld')
scene.world.use_nodes = True
bg = scene.world.node_tree.nodes.get('Background')
bg.inputs[0].default_value = (.018, .004, .009, 1)
bg.inputs[1].default_value = .22
world = look_config.get('world', {})
if 'color' in world:
    bg.inputs[0].default_value = (*world['color'], 1.)
if 'strength' in world:
    bg.inputs[1].default_value = world['strength']
camdata = bpy.data.cameras.new('Menu_Front_16x9')
cam = bpy.data.objects.new(camdata.name, camdata)
scene.collection.objects.link(cam)
cam.location = (0, -30, 2.0)
cam.rotation_euler = (Vector((0, 2, -.15))-cam.location).to_track_quat('-Z', 'Y').to_euler()
camdata.type = 'ORTHO'
camdata.ortho_scale = 17.2
if opt.reference_camera:
    cam.name = camdata.name = 'Menu_CanonicalOpen_Reference'
    cam.location = (0, -30, 0)
    cam.rotation_euler = (Vector((0, 0, 0))-cam.location).to_track_quat('-Z', 'Y').to_euler()
    camdata.ortho_scale = 16.0
scene.camera = cam
scene.render.engine = 'CYCLES'
scene.cycles.samples = opt.samples
scene.cycles.use_denoising = True
scene.cycles.denoiser = 'OPENIMAGEDENOISE'
scene.cycles.denoising_use_gpu = True
scene.render.threads_mode = 'FIXED'
scene.render.threads = 2
scene.cycles.device = 'CPU'
if opt.device == 'OPTIX':
    prefs = bpy.context.preferences.addons['cycles'].preferences
    prefs.compute_device_type = 'OPTIX'
    prefs.get_devices()
    for device in prefs.devices:
        device.use = device.type == 'OPTIX'
    if not any(d.use for d in prefs.devices):
        raise RuntimeError('No verified OPTIX device; explicit CPU fallback required')
    scene.cycles.device = 'GPU'
scene.view_settings.view_transform = 'AgX'
scene.view_settings.look = 'AgX - Medium High Contrast'
scene.view_settings.exposure = -.6
color_management = look_config.get('color_management', {})
if color_management:
    scene.view_settings.view_transform = color_management.get('view_transform', 'AgX')
    scene.view_settings.look = color_management.get('look', 'None')
    scene.view_settings.exposure = color_management.get('exposure', 0.)
if opt.surface_render_mode == 'baked-radiance' and (scene.view_settings.view_transform != 'Standard' or abs(scene.view_settings.exposure) > 1e-8):
    raise RuntimeError('Source-radiance comparison requires Standard / exposure0, with no display transform compensation')
scene.render.resolution_x = opt.edge
scene.render.resolution_y = round(opt.edge * 9/16)
if opt.reference_camera:
    scene.render.resolution_y = round(opt.edge * 410/963)
scene.render.resolution_percentage = 100
scene.render.image_settings.file_format = 'PNG'
scene.render.film_transparent = False

def set_variant(day, amount):
    jaws.hide_render = opt.interior_only
    for col in jaws.children:
        if col.name.startswith('MC_Jaws_DAY'):
            col.hide_render = day not in col.name
            col.hide_viewport = day not in col.name
    root['OpenAmount'] = amount
    root.update_tag()
    bpy.context.view_layer.update()
    # Interior and character are revealed only as the opening clears them.
    # Closed captures must remain actual jaw geometry; no central patch.
    interior.hide_render = amount < .05
    character.hide_render = opt.no_character or amount < .75
    if opt.reference_camera and opt.closed_reference_width:
        camdata.ortho_scale = opt.closed_reference_width * (1-amount) + 16. * amount
        source_height = opt.closed_reference_height * (1-amount) + 410 * amount
        scene.render.resolution_y = round(opt.edge * source_height / 963)

# Authored timeline: closed hold, eased open, hold, eased close.
for frame, amount in [(1, 0.), (12, 0.), (32, 1.), (72, 1.), (94, 0.), (108, 0.)]:
    root['OpenAmount'] = amount
    root.keyframe_insert(data_path='["OpenAmount"]', frame=frame)
scene.frame_start, scene.frame_end = 1, 108
scene.frame_set(42)
set_variant('DAY03', 1.)
for image in bpy.data.images:
    if image.source == 'FILE' and not image.packed_file:
        try:
            image.pack()
        except RuntimeError:
            pass
blend = OUT / f'MC_Menu3D_Candidate_{opt.revision}.blend'
bpy.ops.wm.save_as_mainfile(filepath=str(blend))
report = {'revision': opt.revision, 'blender': bpy.app.version_string, 'sources': sources, 'pose_bindings': pose_bindings, 'texture_states': texture_states, 'character': {'mesh': mesh.name, 'rig': rig.name, 'actions': [a.name for a in actions], 'placement': list(anchor.location), 'uniform_scale': 2.0, 'grounding': grounding}, 'materials': material_provenance, 'device': opt.device, 'renders': [], 'note': 'Existing game character reused. Mesh/texture look requires visual acceptance; scene is not a runtime menu integration.'}
if opt.render:
    for day in (('DAY03',) if opt.hero_only else ('DAY01', 'DAY02', 'DAY03')):
        for state, amount in ([('open', 1.)] if opt.open_only else [('closed', 0.), ('open', 1.)]):
            # Source keyframes are left intact; evaluate at fixed state frame.
            scene.frame_set(42 if amount else 1)
            set_variant(day, amount)
            path = REVIEW / f'{day}_{state}.png'
            scene.render.filepath = str(path)
            bpy.ops.render.render(write_still=True)
            report['renders'].append(str(path))
    # Reset the saved master to the usable open vampire scene.
    scene.frame_set(42)
    set_variant('DAY03', 1.)
    bpy.ops.wm.save_as_mainfile(filepath=str(blend))
if opt.exports:
    export_dir = OUT / 'Exports'
    export_dir.mkdir(exist_ok=True)
    # Native UI/Three.js controls jaw parent transforms. No Python drivers or
    # custom-property animation are required by runtime mouth assets.
    root.animation_data_clear()
    root['OpenAmount'] = 0.
    scene.frame_start, scene.frame_end = 0, 50
    scene.frame_set(0)
    report['exports'] = []
    for day in ('DAY01', 'DAY02', 'DAY03'):
        set_variant(day, 0.)
        interior.hide_render = character.hide_render = False
        selected = [root]
        for col in jaws.children:
            if day in col.name:
                selected.extend(col.all_objects)
        selected.extend(interior.all_objects)
        selected.extend(character.all_objects)
        bpy.ops.object.select_all(action='DESELECT')
        for ob in selected:
            ob.hide_set(False)
            ob.select_set(True)
        bpy.context.view_layer.objects.active = root
        path = export_dir / f'{day}.glb'
        bpy.ops.export_scene.gltf(filepath=str(path), export_format='GLB', use_selection=True,
            export_apply=True, export_yup=True, export_animations=True,
            export_animation_mode='ACTIVE_ACTIONS', export_force_sampling=True,
            export_frame_range=True, export_nla_strips=False, export_skins=True,
            export_morph=True)
        report['exports'].append(str(path))
(REVIEW / 'assembly_manifest.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
print('MENU_ASSEMBLY_READY', json.dumps({'blend': str(blend), 'renders': report['renders']}))
