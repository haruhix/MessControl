"""Read-only saved Wizard rig architecture inspection; never saves the blend."""
import bpy
import json
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / 'Saved/Codex/WizardWorkbenchRigInspection.json'
rig = bpy.data.objects['rig']

def val(v):
    if isinstance(v, (str, int, float, bool)) or v is None:
        return v
    if hasattr(v, 'name'):
        return v.name
    try:
        return [val(i) for i in v]
    except TypeError:
        return str(v)

def props(obj):
    return {k: val(obj[k]) for k in obj.keys()}

def constraint(c):
    keys = ['name', 'type', 'influence', 'mute', 'target', 'subtarget', 'owner_space', 'target_space',
            'use_x', 'use_y', 'use_z', 'use_offset', 'mix_mode', 'head_tail', 'chain_count', 'pole_target',
            'pole_subtarget', 'pole_angle', 'use_stretch', 'rest_length', 'volume',
            'use_limit_x', 'use_limit_y', 'use_limit_z', 'min_x', 'max_x', 'min_y', 'max_y',
            'min_z', 'max_z', 'inverse_matrix']
    return {k: val(getattr(c, k)) for k in keys if hasattr(c, k)}

bones = []
for p in rig.pose.bones:
    b = p.bone
    basis = b.matrix_local.to_3x3()
    bones.append(dict(name=b.name, parent=b.parent.name if b.parent else None,
        deform=b.use_deform, head=list(b.head_local), tail=list(b.tail_local),
        axes={axis: list(basis.col[i]) for i, axis in enumerate('XYZ')},
        length=b.length, inherit_scale=b.inherit_scale, use_local_location=b.use_local_location,
        rotation_mode=p.rotation_mode, lock_location=list(p.lock_location), lock_rotation=list(p.lock_rotation),
        lock_scale=list(p.lock_scale), custom_shape=p.custom_shape.name if p.custom_shape else None,
        custom_shape_transform=p.custom_shape_transform.name if p.custom_shape_transform else None,
        properties=props(p), constraints=[constraint(c) for c in p.constraints]))

meshes = []
for obj in bpy.data.objects:
    if obj.type != 'MESH' or not any(m.type == 'ARMATURE' and m.object == rig for m in obj.modifiers):
        continue
    groups = {g.index: g.name for g in obj.vertex_groups}
    sums, counts, mins, maxs = defaultdict(float), defaultdict(int), {}, {}
    members = defaultdict(set)
    deform_weight_sum_range = [float('inf'), 0.0]
    for v in obj.data.vertices:
        for g in v.groups:
            if g.weight <= 0:
                continue
            name = groups[g.group]
            sums[name] += g.weight
            counts[name] += 1
            members[name].add(v.index)
            mins[name] = [min(a, z) for a, z in zip(mins.get(name, list(v.co)), v.co)]
            maxs[name] = [max(a, z) for a, z in zip(maxs.get(name, list(v.co)), v.co)]
        total = sum(g.weight for g in v.groups if groups[g.group] in rig.data.bones
                    and rig.data.bones[groups[g.group]].use_deform)
        deform_weight_sum_range[0] = min(deform_weight_sum_range[0], total)
        deform_weight_sum_range[1] = max(deform_weight_sum_range[1], total)
    shapes = []
    if obj.data.shape_keys:
        for key in obj.data.shape_keys.key_blocks:
            shapes.append(dict(name=key.name, value=key.value, min=key.slider_min, max=key.slider_max,
                relative_key=key.relative_key.name, vertex_group=key.vertex_group))
    meshes.append(dict(name=obj.name, vertices=len(obj.data.vertices), polygons=len(obj.data.polygons),
        parent=obj.parent.name if obj.parent else None, properties=props(obj),
        matrix_world=[list(r) for r in obj.matrix_world],
        root_head_weight_overlap=len(members['root.x'] & members['head.x']),
        deform_weight_sum_range=deform_weight_sum_range,
        modifiers=[dict(name=m.name, type=m.type, object=m.object.name if hasattr(m, 'object') and m.object else None)
                   for m in obj.modifiers], shape_keys=shapes,
        weights={k: dict(sum=sums[k], vertices=counts[k], min=mins[k], max=maxs[k]) for k in sums}))

drivers = []
if rig.animation_data:
    for f in rig.animation_data.drivers:
        drivers.append(dict(path=f.data_path, index=f.array_index, expression=f.driver.expression,
            variables=[dict(name=v.name, type=v.type,
                targets=[dict(id=t.id.name if t.id else None, data_path=t.data_path, bone_target=t.bone_target,
                    transform_type=t.transform_type, transform_space=t.transform_space) for t in v.targets])
                       for v in f.driver.variables]))

result = dict(file=bpy.data.filepath, blender=bpy.app.version_string,
    rig_properties=props(rig), rig_data_properties=props(rig.data),
    rig_matrix=[list(r) for r in rig.matrix_world], bones=bones, meshes=meshes, drivers=drivers,
    actions=[a.name for a in bpy.data.actions],
    counts=dict(bones=len(bones), deform=sum(b['deform'] for b in bones),
                controls=sum(b['name'].startswith('c_') for b in bones)))
OUTPUT.parent.mkdir(parents=True, exist_ok=True)
OUTPUT.write_text(json.dumps(result, indent=2), encoding='utf8')
print('WIZARD_RIG_INSPECTION ' + str(OUTPUT), flush=True)
print(json.dumps(result['counts']), flush=True)
