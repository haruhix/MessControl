"""Isolated DAY01/DAY02 reference-form proposal. No UV or final FBX authoring.

New regular surface construction replaces the old asymmetric primitive family.
Uses only saved legacy rigid-row transforms; does not read or edit vampire data.
"""
from pathlib import Path
import argparse
import hashlib
import json
import math
import sys
from functools import lru_cache
import bpy
import bmesh
import numpy as np

P = argparse.ArgumentParser()
P.add_argument('--day', choices=('DAY01', 'DAY02'), required=True)
P.add_argument('--revision', default='v02')
P.add_argument('--render', action='store_true')
P.add_argument('--resolution', type=int, default=1400)
A = P.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
assert bpy.app.background, 'Do not modify a GUI scene'
BASE = Path(__file__).resolve().parents[3]/'ArtSource/UI/MainMenu3D/20261015_refB_v1'
ROOT = BASE/'Assembly/UnrealHandoff/v09_FamilyReferenceForms'
assert A.revision.replace('_', '').isalnum(), 'Use a simple isolated revision name'
OUT = ROOT/A.day/A.revision
OUT.mkdir(parents=True, exist_ok=True)
(OUT/'family_finish_reference_forms_executed.py').write_bytes(Path(__file__).read_bytes())
TRANSFORMS = json.loads((ROOT/'legacy_row_transforms.json').read_text())[A.day]
bpy.ops.wm.read_factory_settings(use_empty=True)


def smoothstep(x):
    x = np.clip(x, 0., 1.)
    return x*x*(3.-2.*x)


def arch_samples():
    # A shallow visible front turning continuously into a real posterior arch.
    # No camera-facing shell and no whole-mouth nonuniform scaling.
    t = np.linspace(0., 1., 3000)
    front = np.column_stack((7.80*t, -1.62*t**3))
    p0 = front[-1]
    if A.day == 'DAY02':
        # Turn the hidden posterior line inward and away from the camera.
        # It must not form an extra narrow tip outside the dominant row edge.
        p1, p2, p3 = np.array([7.95, -2.24]), np.array([7.15, -5.60]), np.array([6.00, -6.45])
    else:
        p1, p2, p3 = np.array([8.18, -1.86]), np.array([7.60, -5.60]), np.array([6.20, -6.45])
    b = t[:, None]
    rear = (1-b)**3*p0+3*(1-b)**2*b*p1+3*(1-b)*b*b*p2+b**3*p3
    points = np.r_[front, rear[1:]]
    length = np.r_[0., np.cumsum(np.linalg.norm(np.diff(points, axis=0), axis=1))]
    return points, length


ARC, LENGTH = arch_samples()


def at_distance(s):
    x, y = [float(np.interp(s, LENGTH, ARC[:, k])) for k in range(2)]
    eps = .006
    l = np.array([np.interp(max(0., s-eps), LENGTH, ARC[:, k]) for k in range(2)])
    r = np.array([np.interp(min(LENGTH[-1], s+eps), LENGTH, ARC[:, k]) for k in range(2)])
    tangent = r-l
    tangent /= np.linalg.norm(tangent)
    facial = np.array([-tangent[1], tangent[0]])
    return np.array([x, y]), tangent, facial


@lru_cache(maxsize=4)
def cube_unit(posterior=False, resolution=32):
    # Six coherent quad patches. Analytic superellipsoid gives smooth broad
    # facial convexity, soft incisal corners and clean continuous silhouette.
    # There are no inherited ragged pixel contours or radial cap triangles.
    table, coords, faces, surface = {}, [], [], []
    for axis in range(3):
        side_axes = [k for k in range(3) if k != axis]
        for sign in (-1, 1):
            ids = np.empty((resolution+1, resolution+1), dtype=int)
            for i in range(resolution+1):
                for j in range(resolution+1):
                    key = [0, 0, 0]
                    key[axis] = sign*resolution
                    key[side_axes[0]], key[side_axes[1]] = -resolution+2*i, -resolution+2*j
                    key = tuple(key)
                    if key not in table:
                        table[key] = len(coords)
                        coords.append(np.array(key, float)/resolution)
                        surface.append((axis, sign, i/resolution, j/resolution))
                    ids[i, j] = table[key]
            for i in range(resolution):
                for j in range(resolution):
                    faces.append([int(ids[i, j]), int(ids[i+1, j]), int(ids[i+1, j+1]), int(ids[i, j+1])])
    coords = np.asarray(coords)
    exponent = 4.05 if not posterior else 3.1
    powers = np.array([exponent, 2.65, exponent])
    lo, hi = np.full(len(coords), .2), np.full(len(coords), 2.)
    for unused in range(32):
        mid = (lo+hi)*.5
        mask = np.sum(np.abs(coords/mid[:, None])**powers, axis=1) > 1.
        lo = np.where(mask, mid, lo)
        hi = np.where(mask, hi, mid)
    coords /= ((lo+hi)*.5)[:, None]
    return coords, faces, surface


def cube_crown(width, height, depth, lower=False, canine=False, posterior=False, resolution=32):
    unit, faces, surface = cube_unit(posterior, resolution)
    p = unit.copy()
    t = (p[:, 2]+1.)*.5 if lower else (1.-p[:, 2])*.5
    taper = .91+.09*smoothstep(t/.38)
    p[:, 0] *= width*.5*taper
    p[:, 1] *= depth*.5
    p[:, 2] *= height*.5
    if canine:
        p[:, 2] += (1. if lower else -1.)*.018*height*(1.-np.minimum(1., np.abs(p[:, 0])/(width*.5)))**2*smoothstep((t-.64)/.36)
    front = np.maximum(0., p[:, 1]/(depth*.5))
    p[:, 1] += .018*width*front**3*np.sin(np.pi*t)**2*(.55+.45*np.cos(p[:, 0]/width*4.*np.pi))
    return p, faces, surface


def pchip_scalar(t, x, y):
    x, y = np.asarray(x, float), np.asarray(y, float)
    h, d = np.diff(x), np.diff(y)/np.diff(x)
    m = np.r_[d[0], np.zeros(len(x)-2), d[-1]]
    for k in range(1, len(x)-1):
        if d[k-1]*d[k] > 0.:
            w1, w2 = 2*h[k]+h[k-1], h[k]+2*h[k-1]
            m[k] = (w1+w2)/(w1/d[k-1]+w2/d[k])
    k = int(np.clip(np.searchsorted(x, t)-1, 0, len(x)-2))
    u = np.clip((t-x[k])/h[k], 0., 1.)
    return float((2*u**3-3*u*u+1)*y[k]+(u**3-2*u*u+u)*h[k]*m[k]+(-2*u**3+3*u*u)*y[k+1]+(u**3-u*u)*h[k]*m[k+1])


def predator_crown(width, height, depth, lower=False, posterior=False, resolution=64):
    # Elliptic/superelliptic volumetric loft: broad shoulders, convex facial
    # belly, curved tapered tip. It is neither a cone nor a planar pyramid.
    radial, levels = resolution, 60
    verts, faces = [], []
    for i in range(levels+1):
        t = i/levels
        # PCHIP-shaped body: radius rises into a soft cervical shoulder and
        # continues with a rounded, slightly concave side toward the tip.
        nodes = [0., .07, .16, .30, .48, .67, .82, .92, .975, 1.]
        radii = [.40, .49, .50, .475, .38, .25, .135, .070, .030, .016]
        rr = pchip_scalar(t, nodes, radii)
        ry = depth*.5*(.80+.20*np.sin(np.pi*t))*(rr/.5)**.72
        bend = (.10 if not posterior else .045)*width*t*t
        if lower:
            bend *= -1.
        for j in range(radial):
            angle = 2*math.pi*j/radial
            qx = math.copysign(abs(math.cos(angle))**.87, math.cos(angle))
            qy = math.copysign(abs(math.sin(angle))**.90, math.sin(angle))
            x = width*rr*qx+bend
            y = ry*qy+.06*depth*np.sin(np.pi*t)-.10*depth*t*t
            z = (t-.5)*height*(1. if lower else -1.)
            verts.append((x, y, z))
    for i in range(levels):
        for j in range(radial):
            k = (j+1) % radial
            faces.append([i*radial+j, (i+1)*radial+j, (i+1)*radial+k, i*radial+k])
    for i in (0, levels):
        center = len(verts)
        verts.append(np.mean(np.asarray(verts)[i*radial:(i+1)*radial], axis=0))
        for j in range(radial):
            k = (j+1) % radial
            faces.append([center, i*radial+j, i*radial+k])
    return np.asarray(verts), faces, []


allcoords, allfaces, mats, parts, pairs = [], [], [], [], []
offset = 0


def add(name, c, f, slot, row, **metadata):
    global offset
    st = offset
    allcoords.append(c)
    allfaces.extend([[v+offset for v in face] for face in f])
    mats.extend([slot]*len(f))
    offset += len(c)
    parts.append(dict(mesh=name, vertex_start=st, vertex_count=len(c), material_slot=slot, row=row, **metadata))
    return len(parts)-1


def place_crown(c, distance, root, lower):
    point, tangent, facial = at_distance(distance)
    # Local cube crown centered inZ; cervical base is seated under the gum.
    lo, hi = c[:, 2].min(), c[:, 2].max()
    c[:, 2] += root-(lo if lower else hi)
    xy = c[:, 0:1]*tangent+c[:, 1:2]*facial+point
    c[:, :2] = xy
    if lower:
        c[:, 1] -= .46  # accepted lower/upper depth separation in closed bite
    return c


stations = {}
for row in ('Upper', 'Lower'):
    lower = row == 'Lower'
    if A.day == 'DAY01':
        widths = [2.02, 1.86, 1.65, 1.48, 1.25, 1.16, 1.10, 1.00] if not lower else [1.32, 1.30, 1.35, 1.25, 1.20, 1.15, 1.10, 1.00]
        heights = [2.42, 2.32, 2.27, 2.20, 2.08, 1.95, 1.83, 1.72] if not lower else [1.27, 1.29, 1.40, 1.38, 1.36, 1.31, 1.28, 1.25]
    else:
        widths = [1.38, 1.38, 1.36, 1.34, 1.31, 1.26, 1.16, 1.08]
        heights = [1.82, 1.80, 1.87, 1.80, 1.76, 1.70, 1.29, 1.18] if not lower else [1.75, 1.80, 1.78, 1.76, 1.72, 1.66, 1.26, 1.17]
    distance, rowstations = .018, []
    for ordinal, (width, height) in enumerate(zip(widths, heights), 1):
        if A.day == 'DAY02' and ordinal == 7:
            distance += .45
        elif A.day == 'DAY02' and ordinal == 8:
            distance += .15
        distance += width*.5
        root = (-3.12 if lower else 3.12)+(1. if lower else -1.)*.04*(distance/8.)**2
        rowstations.append((distance, root, ordinal, width, height))
        if A.day == 'DAY01':
            c, f, _ = cube_crown(width, height, 1.04 if ordinal <= 3 else 1.10, lower, ordinal == 3, ordinal >= 6)
        else:
            c, f, _ = predator_crown(width, height, .80 if ordinal <= 6 else .90, lower, ordinal >= 7)
        c = place_crown(c, distance, root, lower)
        kind = ['Central', 'Lateral', 'Canine', 'Premolar01', 'Premolar02', 'Molar01', 'Molar02', 'Molar03'][ordinal-1] if A.day == 'DAY01' else f'Pointed_{ordinal:02d}'
        ri = add(f'MC_{A.day}_{row}_{kind}_R', c, f, 0, row, arch_distance=distance, root_z=root, ordinal=ordinal,
                 inferred=ordinal >= (6 if A.day == 'DAY01' else 7))
        mirror = c.copy()
        mirror[:, 0] *= -1.
        li = add(f'MC_{A.day}_{row}_{kind}_L', mirror, [face[::-1] for face in f], 0, row,
                 arch_distance=distance, root_z=root, ordinal=ordinal, inferred=ordinal >= (6 if A.day == 'DAY01' else 7))
        pairs.append(dict(right_part=ri, left_part=li, row=row, kind=kind))
        distance += width*.5+.022
    stations[row] = rowstations


def gum(row):
    lower = row == 'Lower'
    centers = np.array([s[0] for s in stations[row]])
    roots = np.array([s[1] for s in stations[row]])
    maxdistance = float(centers[-1]+stations[row][-1][3]*.52)
    sections, radial = 340, 64
    # Anatomical labial collar -> broad root eminence -> receding attached
    # tissue -> lingual return. A circular ring produces a sausage silhouette
    # even with displacement; the actual cross-section must change upstream.
    control = np.array([[.12, 0.], [.45, .015], [.64, .09], [.69, .22],
                        [.62, .40], [.41, .76], [.05, 1.], [-.42, .86],
                        [-.56, .50], [-.49, .10], [-.20, 0.]])
    cross = []
    for j in range(radial):
        f = j/radial*len(control)
        k, t = int(f), f-int(f)
        a, b, c, d = [control[n % len(control)] for n in (k-1, k, k+1, k+2)]
        cross.append(.5*((2*b)+(-a+c)*t+(2*a-5*b+4*c-d)*t*t+(-a+3*b-3*c+d)*t*t*t))
    cross = np.asarray(cross)
    verts, faces = [], []
    for i in range(sections+1):
        signed = -maxdistance+2*maxdistance*i/sections
        s = abs(signed)
        point, tangent, facial = at_distance(s)
        if signed < 0:
            point[0] *= -1.
            facial[0] *= -1.
        root = float(np.interp(s, centers, roots))
        if s <= centers[0]:
            scallop = .19*.5*(1.-math.cos(math.pi*s/centers[0]))
        elif s >= centers[-1]:
            t = np.clip((s-centers[-1])/(maxdistance-centers[-1]), 0., 1.)
            scallop = .19*.5*(1.+math.cos(math.pi*t))
        else:
            k = int(np.searchsorted(centers, s)-1)
            t = (s-centers[k])/(centers[k+1]-centers[k])
            scallop = .19*.5*(1.+math.cos(2*math.pi*t))
        side = -1. if lower else 1.
        margin = root-side*(.30-scallop)
        outer = root+side*.62
        zc, rz = (margin+outer)*.5, abs(outer-margin)*.5
        end = max(0., (s-(maxdistance-.36))/.36)
        cap = math.sqrt(max(.0009, 1.-end*end))
        root_field = sum(math.exp(-((s-st[0])/(st[3]*.43))**2) for st in stations[row])
        for j in range(radial):
            outward, h = cross[j]
            z = zc+(margin+(outer-margin)*h-zc)*cap
            front = max(0., outward/.69)
            broad_root = .105*root_field*math.exp(-((z-root-side*.20)/.43)**2)
            radial_distance = (outward+broad_root*front)*cap
            xy = point+facial*radial_distance
            if lower:
                xy[1] -= .46
            verts.append((xy[0], xy[1], z))
    for i in range(sections):
        for j in range(radial):
            k = (j+1) % radial
            faces.append([i*radial+j, (i+1)*radial+j, (i+1)*radial+k, i*radial+k])
    for i in (0, sections):
        center = len(verts)
        verts.append(np.mean(np.asarray(verts)[i*radial:(i+1)*radial], 0))
        for j in range(radial):
            faces.append([center, i*radial+j, i*radial+(j+1) % radial])
    return np.asarray(verts), faces


for row in ('Upper', 'Lower'):
    c, f = gum(row)
    add(f'MC_{A.day}_{row}_Gum', c, f, 1, row, source='Closed scalloped root-seat loft, real U-arch')
basis = np.concatenate(allcoords).astype(np.float32)
poses = {}
for amount in (1., 0., .25, .5, .75):
    out = basis.astype(float).copy()
    for part in parts:
        a = TRANSFORMS[str(amount)][part['row']]
        st, en = part['vertex_start'], part['vertex_start']+part['vertex_count']
        out[st:en] = out[st:en]@np.array(a['rotation'])+np.array(a['translation'])
    poses[amount] = out.astype(np.float32)

me = bpy.data.meshes.new(f'MC_{A.day}_ReferenceSurface')
me.from_pydata(basis.tolist(), [], allfaces)
me.update()
assert not me.validate(verbose=False)
bm = bmesh.new()
bm.from_mesh(me)
bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
bm.to_mesh(me)
bm.free()
me.update()
ob = bpy.data.objects.new('SK_'+A.day, me)
bpy.context.scene.collection.objects.link(ob)
for name, color in [('MC_Enamel', (.68, .72, .73, 1.)), ('MC_Gum', (.28, .36, .38, 1.))]:
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    m.node_tree.nodes['Principled BSDF'].inputs['Base Color'].default_value = color
    m.node_tree.nodes['Principled BSDF'].inputs['Roughness'].default_value = .35
    me.materials.append(m)
me.polygons.foreach_set('material_index', np.array(mats, dtype=np.int32))
me.polygons.foreach_set('use_smooth', np.ones(len(me.polygons), dtype=bool))
ob.shape_key_add(name='Basis')
for amount, name in [(0., 'Pose_000'), (.25, 'Pose_025'), (.5, 'Pose_050'), (.75, 'Pose_075')]:
    k = ob.shape_key_add(name=name)
    k.data.foreach_set('co', poses[amount].ravel())
arm = bpy.data.armatures.new('MC_MenuMouth_Skeleton')
rig = bpy.data.objects.new('MC_MenuMouth_Rig', arm)
bpy.context.scene.collection.objects.link(rig)
bpy.context.view_layer.objects.active = rig
rig.select_set(True)
bpy.ops.object.mode_set(mode='EDIT')
bone = arm.edit_bones.new('root')
bone.head, bone.tail = (0, 0, 0), (0, 0, 100)
bpy.ops.object.mode_set(mode='OBJECT')
g = ob.vertex_groups.new(name='root')
g.add(list(range(len(me.vertices))), 1., 'REPLACE')
mod = ob.modifiers.new('RootSkin', 'ARMATURE')
mod.object = rig
ob.parent = rig
scene = bpy.context.scene
scene.unit_settings.system = 'NONE'
scene.render.threads_mode, scene.render.threads = 'FIXED', 2
report = dict(status='PRIMARY_SECONDARY_FORM_PROPOSAL_NOT_FINAL_ART', day=A.day,
              script=str(Path(__file__).resolve()), source=str(OUT/f'MC_{A.day}_ReferenceForms.blend'),
              reference=str(BASE/'References/B_FullWidth_Approved.png'), vertices=len(me.vertices),
              triangles=sum(len(p.vertices)-2 for p in me.polygons), parts=parts, mirror_pairs=pairs,
              materials=['MC_Enamel', 'MC_Gum'], primary_uv=None, UV_status='WITHHELD until root shape review',
              morphs=['Pose_000', 'Pose_025', 'Pose_050', 'Pose_075'], root_bone='root',
              final_export=False, source_to_native_cm=100., no_nonuniform_scale=True,
              baseline_trajectory='Exact least-squares rigid row transforms from saved functional blocking, max deviation <3e-5 source units',
              pose_bounds={str(a): [v.min(0).tolist(), v.max(0).tolist()] for a, v in poses.items()}, symmetry={})
for amount, co in poses.items():
    errors = []
    for p in pairs:
        r, l = parts[p['right_part']], parts[p['left_part']]
        rc = co[r['vertex_start']:r['vertex_start']+r['vertex_count']].copy()
        lc = co[l['vertex_start']:l['vertex_start']+l['vertex_count']]
        rc[:, 0] *= -1.
        errors.append(float(np.linalg.norm(rc-lc, axis=1).max()))
    assert max(errors) < 3e-5
    report['symmetry'][str(amount)] = dict(max_paired_crown_error=max(errors))
for row in ('Upper', 'Lower'):
    p = next(p for p in parts if p['material_slot'] == 0 and p['row'] == row)
    central = basis[p['vertex_start']:p['vertex_start']+p['vertex_count']]
    assert central[:, 0].min() > 0., 'Paired midline required'
report['paired_center_seams'] = True
np.savez_compressed(OUT/'source_world_poses_cm.npz', **{str(k): v*100 for k, v in poses.items()})

# Evidence lighting is neutral, symmetric and deliberately texture-free.
w = bpy.data.worlds.new('GeometryReview')
scene.world = w
w.use_nodes = True
w.node_tree.nodes['Background'].inputs[0].default_value = (.02, .025, .03, 1)
w.node_tree.nodes['Background'].inputs[1].default_value = .55
for name, loc, power, size in [('Front', (0, 12, 8), 1600, 10), ('Left', (-9, 8, 3), 1000, 8),
                               ('Right', (9, 8, 3), 1000, 8), ('Rim', (0, -7, 8), 1800, 8)]:
    d = bpy.data.lights.new('GEO-Review'+name, 'AREA')
    l = bpy.data.objects.new('GEO-Review'+name, d)
    scene.collection.objects.link(l)
    l.location = loc
    l.rotation_euler = (-l.location).to_track_quat('-Z', 'Y').to_euler()
    d.energy, d.shape, d.size = power, 'DISK', size
cd = bpy.data.cameras.new('ReferenceFrontCamera')
cam = bpy.data.objects.new('ReferenceFrontCamera', cd)
scene.collection.objects.link(cam)
scene.camera = cam
scene.render.engine = 'BLENDER_EEVEE'
scene.render.resolution_x, scene.render.resolution_y = A.resolution, round(A.resolution*9/16)
scene.render.resolution_percentage = 100
scene.render.image_settings.file_format = 'PNG'
scene.render.film_transparent = False
scene.view_settings.view_transform = 'AgX'
bpy.ops.wm.save_as_mainfile(filepath=report['source'])
report['source_sha256'] = hashlib.sha256(Path(report['source']).read_bytes()).hexdigest()
(OUT/'reference_forms_handoff.json').write_text(json.dumps(report, indent=2))
if A.render:
    for name, amount, loc, typ in [('front_closed', 0., (0, 35, 0), 'ORTHO'), ('front_open', 1., (0, 35, 0), 'ORTHO'),
                                    ('oblique_closed', 0., (12, 30, 7), 'PERSP'), ('oblique_open', 1., (12, 30, 7), 'PERSP')]:
        for k in ob.data.shape_keys.key_blocks:
            samples = {'Pose_000': 0., 'Pose_025': .25, 'Pose_050': .5, 'Pose_075': .75}
            k.value = max(0., 1.-abs(amount-samples[k.name])/.25) if k.name in samples else 0.
        cam.location = loc
        cam.rotation_euler = (-cam.location).to_track_quat('-Z', 'Y').to_euler()
        cd.type, cd.ortho_scale, cd.lens = typ, 19.5, 49.5
        scene.render.filepath = str(OUT/(name+'.png'))
        bpy.ops.render.render(write_still=True)
print('FAMILY_REFERENCE_FORM_PROPOSAL', json.dumps(dict(day=A.day, out=str(OUT), vertices=report['vertices'], triangles=report['triangles'], fbx_exported=False)))
