"""Read-only audit of an opened character blend: --background file.blend --python this.py."""
import bpy, json
from pathlib import Path

def curves(action):
    if hasattr(action, 'fcurves'):
        return list(action.fcurves)
    result = []
    for layer in action.layers:
        for strip in layer.strips:
            for slot in action.slots:
                bag = strip.channelbag(slot)
                if bag:
                    result.extend(bag.fcurves)
    return result

report = {'source': bpy.data.filepath, 'version': bpy.app.version_string,
          'fps': bpy.context.scene.render.fps, 'objects': [], 'actions': []}
for obj in bpy.data.objects:
    if obj.type not in {'MESH', 'ARMATURE'}:
        continue
    item = {'name': obj.name, 'type': obj.type, 'dimensions': list(obj.dimensions),
            'location': list(obj.location), 'scale': list(obj.scale), 'parent': obj.parent.name if obj.parent else None}
    if obj.type == 'MESH':
        item.update(vertices=len(obj.data.vertices), materials=[m.name if m else None for m in obj.data.materials],
                    shape_keys=[k.name for k in obj.data.shape_keys.key_blocks] if obj.data.shape_keys else [])
    else:
        item['bones'] = [{'name': b.name, 'parent': b.parent.name if b.parent else None,
                          'deform': b.use_deform, 'head': list(b.head_local), 'tail': list(b.tail_local)} for b in obj.data.bones]
    if obj.animation_data:
        item['active_action'] = obj.animation_data.action.name if obj.animation_data.action else None
        item['nla'] = [{'track': t.name, 'strips': [{'name': s.name, 'action': s.action.name if s.action else None,
                    'start': s.frame_start, 'end': s.frame_end} for s in t.strips]} for t in obj.animation_data.nla_tracks]
    report['objects'].append(item)
for action in bpy.data.actions:
    fc = curves(action)
    report['actions'].append({'name': action.name, 'range': list(action.frame_range), 'curves': len(fc),
        'slots': [s.identifier for s in action.slots], 'paths': sorted({f.data_path for f in fc}),
        'keyframes': sorted({round(p.co.x, 3) for f in fc for p in f.keyframe_points})})
folder = Path(__file__).resolve().parents[2] / 'Saved/AnimationAudit'
folder.mkdir(parents=True, exist_ok=True)
path = folder / (Path(bpy.data.filepath).stem + '.json')
path.write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding='utf8')
print('ANIMATION_AUDIT', path)
print('ACTIONS', [(a['name'], a['range'], a['curves']) for a in report['actions']])
print('RIGS', [(o['name'], len(o['bones'])) for o in report['objects'] if o['type'] == 'ARMATURE'])
print('MESHES', [(o['name'], o['vertices'], o['shape_keys']) for o in report['objects'] if o['type'] == 'MESH'])
