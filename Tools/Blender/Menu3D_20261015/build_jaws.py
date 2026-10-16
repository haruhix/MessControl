"""Reference-B jaw family. Run only in a lead-approved isolated Blender slot.

Blender 5.1; front is -Y, depth +Y, up +Z. This file owns only Jaws/.
The source is a quad-loft dental construction, not scaled primitives.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import random
import sys
import time

import bpy
import bmesh
from mathutils import Vector


ASSET_ROOT = Path(r"E:\DEVGAME\MessControl\ArtSource\UI\MainMenu3D\20261015_refB_v1")
OUT = ASSET_ROOT / "Jaws"
WIDTH = 16.0
OPEN_TRAVEL = 1.7
RADIAL = 24
GUM_SECTIONS = 96
GUM_RADIAL = 24
T_LEVELS = (0.0, 0.045, 0.11, 0.20, 0.33, 0.48, 0.64, 0.78, 0.88, 0.95, 0.985, 1.0)
SEED = 10152026
REVIEW_REVISION = "v03"
UV_PAD = 8.0 / 2048.0
ROOT_NAME = "MC_JawsRoot"
VARIANTS = (
    {"id": "DAY01", "kind": "Human", "upper_neck": 1.16, "lower_neck": 1.16, "gum_height": 0.82},
    {"id": "DAY02", "kind": "Predator", "upper_neck": 1.13, "lower_neck": 1.13, "gum_height": 0.80},
    {"id": "DAY03", "kind": "Vampire", "upper_neck": 1.30, "lower_neck": 1.48, "gum_height": 0.86},
)
TOOTH_OBJECTS = []
GUM_OBJECTS = []
VARIANT_OBJECTS = {}
UV_MANIFEST = []


def parse_args():
    args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument("--phase", choices=("clay", "polish"), default="clay")
    parser.add_argument("--render", action="store_true")
    parser.add_argument("--exports", action="store_true")
    parser.add_argument("--resolution", type=int, default=640)
    parser.add_argument("--samples", type=int, default=16)
    parser.add_argument("--threads", type=int, default=2)
    return parser.parse_args(args)


def smoothstep(a, b, value):
    f = max(0.0, min(1.0, (value - a) / (b - a)))
    return f * f * (3.0 - 2.0 * f)


def lerp_profile(t, points):
    for index in range(len(points) - 1):
        a, va = points[index]
        b, vb = points[index + 1]
        if a <= t <= b:
            f = smoothstep(a, b, t)
            return va * (1 - f) + vb * f
    return points[-1][1]


def signed_pow(value, power):
    return math.copysign(abs(value) ** power, value)


def arch_y(x):
    return 0.06 + 1.35 * (abs(x) / 8.0) ** 2.6


def arch_slope(x):
    if abs(x) < 1e-8:
        return 0.0
    return math.copysign(1.35 * 2.6 / 8.0 * (abs(x) / 8.0) ** 1.6, x)


def set_input(node, name, value):
    for socket in node.inputs:
        if socket.name == name:
            socket.default_value = value
            return True
    return False


def srgb_channel(v):
    return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4


def hex_linear(value):
    rgb = tuple(int(value[i:i + 2], 16) / 255 for i in (0, 2, 4))
    return tuple(srgb_channel(v) for v in rgb) + (1,)


def material(name, color, roughness, coat=0.0, sss=0.0):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    bsdf = next(n for n in mat.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
    set_input(bsdf, "Base Color", hex_linear(color))
    set_input(bsdf, "Roughness", roughness)
    set_input(bsdf, "Metallic", 0.0)
    set_input(bsdf, "IOR", 1.52 if "Enamel" in name else 1.40)
    set_input(bsdf, "Coat Weight", coat)
    set_input(bsdf, "Coat Roughness", 0.16)
    set_input(bsdf, "Subsurface Weight", sss)
    set_input(bsdf, "Subsurface Radius", (0.7, 0.32, 0.18))
    set_input(bsdf, "Subsurface Scale", 0.055)
    mat.diffuse_color = hex_linear(color)
    return mat


def empty(name, collection, parent=None):
    obj = bpy.data.objects.new(name, None)
    collection.objects.link(obj)
    obj.empty_display_type = 'PLAIN_AXES'
    obj.empty_display_size = 0.28
    obj.parent = parent
    obj["Menu3D_Owner"] = "Jaws"
    return obj


def make_mesh(name, vertices, faces, face_uvs, collection, parent, mat, root_values=None):
    mesh = bpy.data.meshes.new(name + "_Mesh")
    mesh.from_pydata(vertices, [], faces)
    mesh.update()
    if mesh.validate(verbose=False):
        raise RuntimeError(f"Invalid generated topology: {name}")
    obj = bpy.data.objects.new(name, mesh)
    collection.objects.link(obj)
    obj.parent = parent
    obj["Menu3D_Owner"] = "Jaws"
    mesh.materials.append(mat)
    uv = mesh.uv_layers.new(name="UV0")
    for poly, coords in zip(mesh.polygons, face_uvs):
        if len(poly.loop_indices) != len(coords):
            raise RuntimeError(f"UV corner mismatch: {name}")
        for loop, coordinate in zip(poly.loop_indices, coords):
            uv.data[loop].uv = coordinate
        poly.use_smooth = True
    if root_values is not None:
        attr = mesh.attributes.new("MC_RootToTip", 'FLOAT', 'POINT')
        for datum, value in zip(attr.data, root_values):
            datum.value = value
        obj["RootToTipAttribute"] = "MC_RootToTip"
    # Recompute outward normals without changing vertex/face order or UV corners.
    bm = bmesh.new()
    bm.from_mesh(mesh)
    bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
    bm.to_mesh(mesh)
    bm.free()
    mesh.update()
    sub = obj.modifiers.new("CrownSurface_Subdivision", 'SUBSURF')
    sub.subdivision_type = 'CATMULL_CLARK'
    sub.levels = sub.render_levels = 1
    return obj


def tooth_definitions(variant, upper):
    kind = variant["kind"]
    if kind == "Predator":
        count = 11 if upper else 10
        step = 1.45
        positions = [(i - (count - 1) / 2) * step for i in range(count)]
        return [{"x": x, "width": 1.56 if upper else 1.53, "height": 1.73 if upper else 1.60,
                 "depth": 0.65, "profile": "blade", "fang": False} for x in positions]
    if kind == "Vampire" and upper:
        positions = [-7.36, -6.18, -5.02, -3.80, -2.55, -1.53, -0.51,
                     0.51, 1.53, 2.55, 3.80, 5.02, 6.18, 7.36]
        teeth = []
        for i, x in enumerate(positions):
            fang = i in (3, 10)
            center = abs(x) < 3
            teeth.append({"x": x, "width": 1.32 if fang else 0.985 if center else 1.09,
                          "height": 3.25 if fang else 1.85 if center else 1.91,
                          "depth": 0.96 if fang else 0.88,
                          "profile": "fang" if fang else "incisor", "fang": fang})
        return teeth
    widths = ([1.20, 1.40, 1.57, 1.70, 1.83, 1.81, 1.70, 1.57, 1.40, 1.20]
              if kind == "Human" else
              [1.06, 1.16, 1.25, 1.25, 1.34, 1.47, 1.45, 1.34, 1.24, 1.22, 1.13, 1.04])
    total = sum(widths) + 0.045 * (len(widths) - 1)
    cursor = -total / 2
    teeth = []
    for i, width in enumerate(widths):
        x = cursor + width / 2
        cursor += width + 0.045
        upper_h = 2.47 - 0.23 * (abs(x) / 8) ** 1.1
        lower_h = 1.43 - 0.16 * (abs(x) / 8)
        if kind == "Vampire":
            # Closed central rows meet/overlap in projection; no cavity patch is
            # used to disguise a vertical gap between independent jaw parts.
            lower_h = 1.49 - 0.10 * (abs(x) / 8)
        teeth.append({"x": x, "width": width * (1.0 if upper else .90 if kind == "Human" else .99),
                      "height": upper_h if upper else lower_h,
                      "depth": 1.0 if upper else 0.82,
                      "profile": "incisor", "fang": False})
    return teeth


def create_tooth(variant, upper, index, spec, collection, parent, enamel):
    rng = random.Random(SEED + int(variant["id"][-2:]) * 1000 + (0 if upper else 200) + index)
    sign = 1 if upper else -1
    direction = -sign
    neck = variant["upper_neck" if upper else "lower_neck"]
    root_z = sign * (neck + 0.22)
    x = spec["x"]
    height = spec["height"] * rng.uniform(0.978, 1.021)
    width = spec["width"] * rng.uniform(0.975, 1.018)
    depth = spec["depth"] * rng.uniform(0.98, 1.03)
    yaw = math.atan(arch_slope(x)) + rng.uniform(-0.025, 0.025)
    tangent = Vector((math.cos(yaw), math.sin(yaw), 0))
    inward = Vector((-math.sin(yaw), math.cos(yaw), 0))
    lower_recess = .64 if variant["kind"] == "Human" else .27
    root = Vector((x, arch_y(x) + (0.08 if upper else lower_recess), root_z))
    is_pointed = spec["profile"] in ("blade", "fang")
    if spec["profile"] == "blade":
        # Maximal visible width immediately below cervical rim, then a
        # continuous triangular taper. The previous quarter-height swelling
        # made these read as conical canines rather than broad flat blades.
        width_keys = [(0, .98), (.045, 1.0), (.11, .96), (.24, .86), (.43, .69), (.66, .46), (.84, .25), (.95, .09), (1, .014)]
        depth_keys = [(0, .88), (.045, .99), (.11, .96), (.24, .85), (.43, .67), (.66, .45), (.84, .25), (.95, .09), (1, .025)]
    elif spec["profile"] == "fang":
        width_keys = [(0, .60), (.13, .94), (.28, 1.0), (.48, .83), (.68, .57), (.85, .32), (.96, .12), (1, .025)]
        depth_keys = [(0, .62), (.15, .97), (.34, 1.0), (.62, .66), (.82, .33), (.96, .11), (1, .035)]
    else:
        width_keys = [(0, .65), (.11, .85), (.28, 1.0), (.58, 1.02), (.82, 1.0), (.95, .94), (1, .79)]
        depth_keys = [(0, .61), (.11, .83), (.30, 1.0), (.58, .99), (.80, .83), (.94, .48), (1, .055)]
    vertices, roots = [], []
    lean = rng.uniform(-0.045, 0.045)
    for t in T_LEVELS:
        radius_x = width / 2 * lerp_profile(t, width_keys)
        radius_y = depth / 2 * lerp_profile(t, depth_keys)
        exponent = 2 / (4.2 if spec["profile"] == "blade" else 2.55 if is_pointed else 3.4)
        bend_forward = -0.13 * math.sin(math.pi * t * .85)
        if spec["profile"] == "fang":
            # Long canines sweep labially; their tips stay in front of lower
            # gingiva at closed pose rather than disappearing inside its volume.
            bend_forward -= .62 * t ** 1.7
        bend_side = lean * t * t + (0.07 * math.copysign(1, x) * t ** 2 if is_pointed else 0)
        for j in range(RADIAL):
            angle = math.pi / 2 + 2 * math.pi * j / RADIAL
            a, b = math.cos(angle), math.sin(angle)
            lx = signed_pow(a, exponent) * radius_x
            ly = signed_pow(b, exponent) * radius_y
            front_weight = max(0.0, -b) ** 4
            if not is_pointed:
                ly -= front_weight * (0.075 * math.sin(math.pi * t)
                                      + 0.014 * math.cos(lx / max(width, .01) * 5 * math.pi) * math.sin(math.pi * t))
            else:
                # A soft longitudinal labial ridge, curved axis and broad base avoid a cone silhouette.
                ly -= front_weight * (0.08 if spec["profile"] == "blade" else .045) * math.sin(math.pi * t) ** 1.5
            z = root_z + direction * height * t
            if not is_pointed:
                z -= direction * 0.065 * abs(a) ** 3 * t ** 7
            point = root + tangent * (lx + bend_side) + inward * (ly + bend_forward)
            point.z = z
            vertices.append(tuple(point))
            roots.append(t)
    # One pole per end; no coincident radius-zero ring.
    root_pole = len(vertices)
    vertices.append(tuple(root + Vector((0, 0, sign * 0.055))))
    roots.append(0.0)
    tip_pole = len(vertices)
    tip = Vector(vertices[(len(T_LEVELS) - 1) * RADIAL])
    last_ring = [Vector(v) for v in vertices[(len(T_LEVELS) - 1) * RADIAL:len(T_LEVELS) * RADIAL]]
    tip = sum(last_ring, Vector()) / RADIAL
    tip.z += direction * (0.018 if is_pointed else 0.003)
    vertices.append(tuple(tip))
    roots.append(1.0)
    faces, face_uvs = [], []
    for i in range(len(T_LEVELS) - 1):
        for j in range(RADIAL):
            j1 = (j + 1) % RADIAL
            faces.append((i * RADIAL + j, i * RADIAL + j1,
                          (i + 1) * RADIAL + j1, (i + 1) * RADIAL + j))
            face_uvs.append(((j / RADIAL, T_LEVELS[i]), ((j + 1) / RADIAL, T_LEVELS[i]),
                             ((j + 1) / RADIAL, T_LEVELS[i + 1]), (j / RADIAL, T_LEVELS[i + 1])))
    cap_face_start = len(faces)
    for j in range(RADIAL):
        j1 = (j + 1) % RADIAL
        faces.append((root_pole, j1, j))
        face_uvs.append(((0.5, 0.5), (0.5, 0.5), (0.5, 0.5)))
        faces.append((tip_pole, (len(T_LEVELS) - 1) * RADIAL + j,
                      (len(T_LEVELS) - 1) * RADIAL + j1))
        face_uvs.append(((0.5, 0.5), (0.5, 0.5), (0.5, 0.5)))
    side = "Upper" if upper else "Lower"
    label = "Fang" if spec["fang"] else "Blade" if is_pointed else "Crown"
    name = f"MC_{variant['id']}_{side}_Enamel_{index + 1:02d}_{label}"
    obj = make_mesh(name, vertices, faces, face_uvs, collection, parent, enamel, roots)
    obj["Variant"] = variant["id"]
    obj["Jaw"] = side
    obj["ToothType"] = spec["profile"]
    obj["IsLongUpperFang"] = bool(spec["fang"] and upper)
    obj["CervicalRootWorldZ"] = root_z
    obj["CrownTipWorldZ"] = float(tip.z)
    obj["UVCapFaceStart"] = cap_face_start
    TOOTH_OBJECTS.append(obj)
    return obj


def catmull(points, parameter):
    index = int(math.floor(parameter))
    t = parameter - index
    n = len(points)
    p0, p1, p2, p3 = [Vector(points[(index + offset) % n]) for offset in (-1, 0, 1, 2)]
    return (p1 * 2 + (p2 - p0) * t + (p0 * 2 - p1 * 5 + p2 * 4 - p3) * t * t
            + (-p0 + p1 * 3 - p2 * 3 + p3) * t * t * t) * .5


def create_gum(variant, upper, teeth, collection, parent, mat):
    sign = 1 if upper else -1
    neck = variant["upper_neck" if upper else "lower_neck"]
    h = variant["gum_height"]
    profile = [(-.30, 0), (-.52, .12), (-.57, h*.60), (-.35, h*.95),
               (.24, h*1.08), (1.53, h*.79), (2.03, .16), (1.02, -.12), (.29, -.11)]
    vertices = []
    for i in range(GUM_SECTIONS + 1):
        x = -7.94 + 15.88 * i / GUM_SECTIONS
        nearest = min(teeth, key=lambda tooth: abs(tooth["x"] - x))
        phase = (x - nearest["x"]) / max(nearest["width"], .5)
        # The margin arches over each cervical neck and descends into each gap,
        # creating gingival papillae rather than an unrelated straight tube.
        scallop = .16 * math.cos(2 * math.pi * phase) - .045
        taper = min(1.0, math.sqrt(max(.06, (8.14 - abs(x)) / .32)))
        for j in range(GUM_RADIAL):
            q = j / GUM_RADIAL * len(profile)
            p = catmull(profile, q)
            front = max(0.0, 1.0 - abs(q - .45) / 2.0)
            outer_weight = smoothstep(1.2, 3.0, q) * (1.0-smoothstep(4.7, 7.0, q))
            broad_arch = (.14 * (1.0-(x/8.0)**2) + .025 * math.sin(x*.8+.3)) * outer_weight
            neck_bulge = .065 * math.cos(2*math.pi*phase) * max(0.0, 1.0-abs(q-1.8)/2.1)
            wet_irregularity = .008 * math.sin(x * 3.1 + q * 1.7) * math.sin(x * 1.3 - q * .8)
            y = arch_y(x) + (p.x - .50) * taper + .50 + (0 if upper else .18) - neck_bulge
            z = sign * (neck + (p.y - h*.40) * taper + h*.40
                        + scallop * front + wet_irregularity * front + broad_arch)
            vertices.append((x, y, z))
    left_pole = len(vertices)
    vertices.append((-8.13, arch_y(-7.94) + .56, sign * (neck + h*.38)))
    right_pole = len(vertices)
    vertices.append((8.13, arch_y(7.94) + .56, sign * (neck + h*.38)))
    faces, face_uvs = [], []
    for i in range(GUM_SECTIONS):
        for j in range(GUM_RADIAL):
            j1 = (j + 1) % GUM_RADIAL
            faces.append((i * GUM_RADIAL + j, (i + 1) * GUM_RADIAL + j,
                          (i + 1) * GUM_RADIAL + j1, i * GUM_RADIAL + j1))
            face_uvs.append(((i / GUM_SECTIONS, j / GUM_RADIAL),
                             ((i + 1) / GUM_SECTIONS, j / GUM_RADIAL),
                             ((i + 1) / GUM_SECTIONS, (j + 1) / GUM_RADIAL),
                             (i / GUM_SECTIONS, (j + 1) / GUM_RADIAL)))
    for j in range(GUM_RADIAL):
        j1 = (j + 1) % GUM_RADIAL
        faces.append((left_pole, j, j1))
        face_uvs.append(((.5, .5), (.5, .5), (.5, .5)))
        faces.append((right_pole, GUM_SECTIONS * GUM_RADIAL + j1, GUM_SECTIONS * GUM_RADIAL + j))
        face_uvs.append(((.5, .5), (.5, .5), (.5, .5)))
    side = "Upper" if upper else "Lower"
    obj = make_mesh(f"MC_{variant['id']}_{side}_Gum", vertices, faces, face_uvs, collection, parent, mat)
    obj["Variant"] = variant["id"]
    obj["Jaw"] = side
    obj["PalatalDepth"] = 2.03
    GUM_OBJECTS.append(obj)
    return obj


def uv_tooth_atlas():
    columns, rows = 6, 14
    occupied = set()
    for obj in TOOTH_OBJECTS:
        height_cells = 2 if obj["IsLongUpperFang"] else 1
        slot = None
        for row in range(rows - height_cells + 1):
            for col in range(columns):
                cells = {(col, r) for r in range(row, row + height_cells)}
                if not cells & occupied:
                    slot = col, row
                    occupied |= cells
                    break
            if slot is not None:
                break
        if slot is None:
            raise RuntimeError("Enamel UV atlas capacity exceeded")
        col, row = slot
        u0, v0, du, dv = col / columns, row / rows, 1 / columns, height_cells / rows
        body = (u0 + UV_PAD, v0 + UV_PAD,
                u0 + UV_PAD + (du - 2 * UV_PAD) * .83, v0 + dv - UV_PAD)
        cap_radius = min(du*.048, dv*.12)
        cap_u = u0 + du - UV_PAD - cap_radius
        cap_root = (cap_u-cap_radius, v0+UV_PAD, cap_u+cap_radius, v0+UV_PAD+2*cap_radius)
        cap_tip = (cap_u-cap_radius, v0+dv-UV_PAD-2*cap_radius, cap_u+cap_radius, v0+dv-UV_PAD)
        mesh = obj.data
        uv = mesh.uv_layers["UV0"]
        for poly in mesh.polygons:
            if poly.index < obj["UVCapFaceStart"]:
                for loop in poly.loop_indices:
                    old = uv.data[loop].uv.copy()
                    uv.data[loop].uv = (body[0] + old.x*(body[2]-body[0]),
                                        body[1] + old.y*(body[3]-body[1]))
            else:
                is_root = (poly.index - obj["UVCapFaceStart"]) % 2 == 0
                rect = cap_root if is_root else cap_tip
                center = Vector(((rect[0]+rect[2])/2, (rect[1]+rect[3])/2))
                radius = (rect[2]-rect[0])/2
                for loop in poly.loop_indices:
                    vertex = mesh.loops[loop].vertex_index
                    if vertex >= len(T_LEVELS)*RADIAL:
                        uv.data[loop].uv = center
                    else:
                        angle = 2*math.pi*(vertex % RADIAL)/RADIAL
                        uv.data[loop].uv = center+Vector((math.cos(angle), math.sin(angle)))*radius
        obj["UVBodyRect"] = list(body)
        UV_MANIFEST.append({"object": obj.name, "material": "MC_Enamel", "variant": obj["Variant"],
                            "jaw": obj["Jaw"], "long_upper_fang": bool(obj["IsLongUpperFang"]),
                            "main_uv_rect": body, "root_edge": "bottom", "tip_edge": "top",
                            "root_cap_rect": cap_root, "tip_cap_rect": cap_tip,
                            "root_to_tip_attribute": "MC_RootToTip", "cervical_mask_fraction": [0, .24]})


def uv_gum_atlas():
    # Hero front strips receive greater density than the inferred palatal back.
    for index, obj in enumerate(GUM_OBJECTS):
        front = (UV_PAD, index*.075+UV_PAD, .90, (index+1)*.075-UV_PAD)
        back = (UV_PAD, .45+index*.0875+UV_PAD, .90, .45+(index+1)*.0875-UV_PAD)
        cap_rects = [(0.932, front[1]+.006, .990, front[3]-.006),
                     (0.932, back[1]+.006, .990, back[3]-.006)]
        mesh, cut = obj.data, 11
        uv = mesh.uv_layers["UV0"]
        body_faces = GUM_SECTIONS*GUM_RADIAL
        for poly in mesh.polygons:
            if poly.index < body_faces:
                segment = poly.index % GUM_RADIAL
                rect = front if segment < cut else back
                first, span = (0, cut) if segment < cut else (cut, GUM_RADIAL-cut)
                for loop in poly.loop_indices:
                    old = uv.data[loop].uv.copy()
                    v = (old.y*GUM_RADIAL-first)/span
                    uv.data[loop].uv = (rect[0]+old.x*(rect[2]-rect[0]), rect[1]+v*(rect[3]-rect[1]))
            else:
                left = (poly.index-body_faces) % 2 == 0
                rect = cap_rects[0 if left else 1]
                center = Vector(((rect[0]+rect[2])/2, (rect[1]+rect[3])/2))
                for loop in poly.loop_indices:
                    vertex = mesh.loops[loop].vertex_index
                    if vertex >= (GUM_SECTIONS+1)*GUM_RADIAL:
                        uv.data[loop].uv = center
                    else:
                        angle = 2*math.pi*(vertex % GUM_RADIAL)/GUM_RADIAL
                        uv.data[loop].uv = center+Vector((math.cos(angle)*(rect[2]-rect[0])/2,
                                                        math.sin(angle)*(rect[3]-rect[1])/2))
        UV_MANIFEST.append({"object": obj.name, "material": "MC_Gum", "variant": obj["Variant"],
                            "jaw": obj["Jaw"], "front_uv_rect": front, "palatal_uv_rect": back,
                            "end_cap_rects": cap_rects, "density_priority": "hero front > inferred lingual/palatal back"})


def add_opening_driver(jaw, root, sign):
    curve = jaw.driver_add("location", 2)
    curve.driver.type = 'SCRIPTED'
    variable = curve.driver.variables.new()
    variable.name = "open_amount"
    variable.type = 'SINGLE_PROP'
    variable.targets[0].id = root
    variable.targets[0].data_path = '["OpenAmount"]'
    curve.driver.expression = f"{sign * OPEN_TRAVEL} * open_amount"
    jaw["JawTravelSign"] = sign


def make_camera(name, location, target, width, collection):
    camera = bpy.data.cameras.new(name)
    camera.type = 'ORTHO'
    camera.ortho_scale = width
    obj = bpy.data.objects.new(name, camera)
    collection.objects.link(obj)
    obj.location = location
    obj.rotation_euler = (Vector(target)-obj.location).to_track_quat('-Z', 'Y').to_euler()
    return obj


def area(name, location, energy, size, collection, color=(1, 1, 1)):
    data = bpy.data.lights.new(name, 'AREA')
    data.energy, data.size, data.color = energy, size, color
    obj = bpy.data.objects.new(name, data)
    collection.objects.link(obj)
    obj.location = location
    obj.rotation_euler = (Vector((0, .4, 0))-obj.location).to_track_quat('-Z', 'Y').to_euler()


def show_variant(variant_id):
    for key, objects in VARIANT_OBJECTS.items():
        for obj in objects:
            obj.hide_render = key != variant_id


def set_open(root, value):
    root["OpenAmount"] = value
    bpy.context.view_layer.update()


def mesh_metrics(root):
    result = {"blender": bpy.app.version_string, "root": root.name, "variants": {}, "materials": ["MC_Enamel", "MC_Gum"]}
    for variant in VARIANTS:
        objects = [obj for obj in VARIANT_OBJECTS[variant["id"]] if obj.type == 'MESH']
        entry = {"mesh_objects": len(objects), "evaluated_triangles": 0, "non_manifold_edges": 0,
                 "uv_missing": [], "bounds_closed": None}
        points = []
        for obj in objects:
            bm = bmesh.new()
            bm.from_mesh(obj.data)
            entry["non_manifold_edges"] += sum(not edge.is_manifold for edge in bm.edges)
            bm.free()
            if not obj.data.uv_layers.get("UV0"):
                entry["uv_missing"].append(obj.name)
            evaluated = obj.evaluated_get(bpy.context.evaluated_depsgraph_get())
            mesh = evaluated.to_mesh()
            mesh.calc_loop_triangles()
            entry["evaluated_triangles"] += len(mesh.loop_triangles)
            points.extend(obj.matrix_world @ vertex.co for vertex in mesh.vertices)
            evaluated.to_mesh_clear()
        entry["bounds_closed"] = [[min(p[i] for p in points) for i in range(3)], [max(p[i] for p in points) for i in range(3)]]
        entry["dimensions_closed"] = [entry["bounds_closed"][1][i]-entry["bounds_closed"][0][i] for i in range(3)]
        entry["long_upper_fangs"] = sum(bool(obj.get("IsLongUpperFang")) for obj in objects)
        if entry["non_manifold_edges"] or entry["uv_missing"] or entry["evaluated_triangles"] > 150000:
            raise RuntimeError(f"Topology/budget gate failed: {variant['id']} {entry}")
        if variant["kind"] == "Vampire" and entry["long_upper_fangs"] != 2:
            raise RuntimeError("Vampire must have exactly two long upper fangs")
        result["variants"][variant["id"]] = entry
    return result


def selected_objects(objects):
    for obj in list(bpy.context.selected_objects):
        obj.select_set(False)
    for obj in objects:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = next((obj for obj in objects if obj.type == 'MESH'), objects[0])


def export_assets(root):
    folder = OUT / "Exports"
    folder.mkdir(parents=True, exist_ok=True)
    set_open(root, 0)
    for variant in VARIANTS:
        objects = [root] + VARIANT_OBJECTS[variant["id"]]
        selected_objects(objects)
        path = folder / f"MC_Jaws_{variant['id']}_{variant['kind']}.glb"
        bpy.ops.export_scene.gltf(filepath=str(path), export_format='GLB', use_selection=True,
                                  export_apply=True, export_materials='EXPORT', export_extras=True,
                                  export_animations=False, export_yup=True)
        # Semantic jaw parents survive FBX; runtime supplies the same translation
        # as the Blender authoring driver. No driver animation is misrepresented
        # as an exported skeletal clip.
        bpy.ops.export_scene.fbx(filepath=str(folder / f"MC_Jaws_{variant['id']}_{variant['kind']}.fbx"),
                                 use_selection=True, object_types={'MESH', 'EMPTY'},
                                 use_mesh_modifiers=True, mesh_smooth_type='FACE',
                                 apply_unit_scale=True, apply_scale_options='FBX_SCALE_UNITS',
                                 use_custom_props=True, bake_anim=False,
                                 axis_forward='-Y', axis_up='Z')
    # Dedicated exploded Painter geometry keeps moving-jaw and cross-variant AO apart.
    temp = bpy.data.collections.new("_MC_PainterBake_Export")
    bpy.context.scene.collection.children.link(temp)
    clones, renamed = [], []
    try:
        depsgraph = bpy.context.evaluated_depsgraph_get()
        for variant_index, variant in enumerate(VARIANTS):
            for obj in VARIANT_OBJECTS[variant["id"]]:
                if obj.type != 'MESH':
                    continue
                name = obj.name
                obj.name = name + "_SourceTemp"
                renamed.append((obj, name))
                mesh = bpy.data.meshes.new_from_object(obj.evaluated_get(depsgraph))
                clone = bpy.data.objects.new(name, mesh)
                temp.objects.link(clone)
                clone.matrix_world = obj.matrix_world.copy()
                clone.location.x += (variant_index-1)*20
                clone.location.z += 4 if obj["Jaw"] == "Upper" else -4
                clones.append(clone)
        selected_objects(clones)
        bpy.ops.wm.obj_export(filepath=str(folder / "Jaws_PainterBake.obj"), export_selected_objects=True,
                              apply_modifiers=False, export_uv=True, export_normals=True,
                              export_materials=True, export_triangulated_mesh=False,
                              forward_axis='NEGATIVE_Y', up_axis='Z')
    finally:
        for obj in clones:
            mesh = obj.data
            bpy.data.objects.remove(obj, do_unlink=True)
            if not mesh.users:
                bpy.data.meshes.remove(mesh)
        for obj, name in renamed:
            obj.name = name
        bpy.data.collections.remove(temp)


def render_reviews(root, stage_collection, args, clay):
    scene = bpy.context.scene
    folder = OUT / "Review" / (f"Clay_{REVIEW_REVISION}" if args.phase == "clay" else f"Polish_{REVIEW_REVISION}")
    folder.mkdir(parents=True, exist_ok=True)
    front = make_camera("CAM_ReferenceB_Front", (0, -25, .10), (0, .4, .10), 17.2, stage_collection)
    three_quarter = make_camera("CAM_Review_ThreeQuarter", (-10, -23, 7), (0, .7, 0), 18.7, stage_collection)
    scene.view_layers[0].material_override = clay if args.phase == "clay" else None
    for variant in VARIANTS:
        show_variant(variant["id"])
        for view, opening, camera, ratio in (
            ("front_closed", 0, front, 3.45 if variant["kind"] == "Vampire" else 4.05),
            ("front_open", 1, front, 2.05),
            ("threequarter_open", 1, three_quarter, 16/9),
        ):
            set_open(root, opening)
            scene.camera = camera
            scene.render.resolution_x = args.resolution
            scene.render.resolution_y = round(args.resolution/ratio)
            scene.render.filepath = str(folder / f"{variant['id']}_{view}.png")
            bpy.ops.render.render(write_still=True)
    scene.view_layers[0].material_override = None
    show_variant("DAY01")
    set_open(root, 0)
    scene.camera = front


def main():
    start = time.time()
    args = parse_args()
    if bpy.app.version < (5, 1, 0):
        raise RuntimeError(f"Blender5.1 required; got {bpy.app.version_string}")
    print("BLENDER_VERSION", bpy.app.version_string, flush=True)
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "Review").mkdir(exist_ok=True)
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete(use_global=False)
    for collection in list(bpy.data.collections):
        if collection.name != "Collection" and not collection.objects:
            bpy.data.collections.remove(collection)
    scene = bpy.context.scene
    scene.render.engine = 'CYCLES'
    scene.cycles.device = 'CPU'
    scene.cycles.samples = args.samples
    scene.cycles.use_denoising = True
    scene.render.threads_mode = 'FIXED'
    scene.render.threads = args.threads
    scene.render.image_settings.file_format = 'PNG'
    scene.render.image_settings.color_mode = 'RGBA'
    scene.render.resolution_percentage = 100
    scene.world = bpy.data.worlds.new("MC_Jaws_ReviewWorld")
    scene.world.use_nodes = True
    scene.world.node_tree.nodes['Background'].inputs['Color'].default_value = (0.055, .07, .09, 1)
    scene.world.node_tree.nodes['Background'].inputs['Strength'].default_value = .35
    scene.view_settings.view_transform = 'AgX'
    scene.view_settings.look = 'AgX - Medium High Contrast'
    scene.frame_start, scene.frame_end = 1, 60
    scene.render.fps = 30
    root_collection = bpy.data.collections.new("MC_Jaws_Family")
    scene.collection.children.link(root_collection)
    root = empty(ROOT_NAME, root_collection)
    root["OpenAmount"] = 0.0
    root.id_properties_ui("OpenAmount").update(min=0.0, max=1.0, description="Translate upper/lower jaws by +1.7/-1.7Z")
    root["SpatialContract"] = "X horizontal; -Y front; +Y palate; Z up; width16; translation-only shutters"
    enamel = material("MC_Enamel", "F7E4C9", .23, .24, .055)
    gum = material("MC_Gum", "E76373", .265, .40, .12)
    clay = material("MC_Clay_Review", "999DA1", .49)
    for variant in VARIANTS:
        collection = bpy.data.collections.new(f"MC_Jaws_{variant['id']}_{variant['kind']}")
        root_collection.children.link(collection)
        variant_root = empty(f"MC_Jaws_{variant['id']}_{variant['kind']}_Root", collection, root)
        VARIANT_OBJECTS[variant["id"]] = [variant_root]
        for upper in (True, False):
            jaw = empty(f"MC_Jaw_{variant['id']}_{'Upper' if upper else 'Lower'}", collection, variant_root)
            VARIANT_OBJECTS[variant["id"]].append(jaw)
            add_opening_driver(jaw, root, 1 if upper else -1)
            teeth = tooth_definitions(variant, upper)
            for index, tooth in enumerate(teeth):
                obj = create_tooth(variant, upper, index, tooth, collection, jaw, enamel)
                VARIANT_OBJECTS[variant["id"]].append(obj)
            gum_obj = create_gum(variant, upper, teeth, collection, jaw, gum)
            VARIANT_OBJECTS[variant["id"]].append(gum_obj)
    uv_tooth_atlas()
    uv_gum_atlas()
    stage = bpy.data.collections.new("MC_Jaws_ReviewRig")
    scene.collection.children.link(stage)
    area("KEY_Clay_Soft", (-6, -10, 9), 1500, 8.5, stage, (1, .94, .86))
    area("FILL_Clay_Soft", (8, -7, 3), 750, 7, stage, (.86, .93, 1))
    area("RIM_Clay_Soft", (0, 5, 8), 1700, 6.5, stage, (1, .88, .82))
    set_open(root, 0)
    metrics = mesh_metrics(root)
    metrics["source_sha256"] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    metrics["phase"] = args.phase
    metrics["elapsed_build_seconds"] = round(time.time()-start, 3)
    (OUT / "jaw_metrics.json").write_text(json.dumps(metrics, indent=2), encoding="utf-8")
    (OUT / "jaw_uv_manifest.json").write_text(json.dumps({"texture_size": 2048, "minimum_island_gap_pixels": 16,
        "materials": ["MC_Enamel", "MC_Gum"], "enamel_objects": len(TOOTH_OBJECTS),
        "gum_objects": len(GUM_OBJECTS), "parts": UV_MANIFEST}, indent=2), encoding="utf-8")
    if args.exports:
        export_assets(root)
    if args.render:
        render_reviews(root, stage, args, clay)
    else:
        scene.camera = make_camera("CAM_ReferenceB_Front", (0, -25, .1), (0, .4, .1), 17.2, stage)
    show_variant("DAY01")
    set_open(root, 0)
    selected_objects([root])
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT / "MC_Jaws_Family.blend"))
    print("RESULT", json.dumps({"ok": True, "blend": str(OUT / "MC_Jaws_Family.blend"),
          "phase": args.phase, "enamel_objects": len(TOOTH_OBJECTS), "gum_objects": len(GUM_OBJECTS),
          "metrics": metrics, "elapsed_seconds": round(time.time()-start, 2)}), flush=True)


if __name__ == "__main__":
    main()
