"""Export the three held props from Character.blend without its scene placement.

Run with the artist file open in background Blender. Geometry and packed maps
are written to ArtSource/CharacterCurrent/Tools; the source file is never saved.
"""
import bpy
import hashlib
import json
from pathlib import Path
from mathutils import Matrix, Vector

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'ArtSource/CharacterCurrent/Tools'
OUT.mkdir(parents=True, exist_ok=True)
source_path = Path(bpy.data.filepath)
scene = bpy.data.scenes.new('CharacterTools')
scene.unit_settings.system = 'METRIC'
scene.unit_settings.scale_length = .01
records = []
for name in ('SM_Brush', 'SM_Pick', 'SM_Spray'):
    source = bpy.data.objects[name]
    assert source.type == 'MESH' and len(source.data.materials) == 1
    obj = source.copy()
    obj.data = source.data.copy()
    obj.animation_data_clear()
    obj.parent = None
    # The pick is rotated upright only for display in the artist scene. Its
    # local +X handle axis is the gameplay swing axis. Bake scale, not placement.
    obj.data.transform(Matrix.Diagonal(Vector((*[s * 100 for s in source.scale], 1))))
    obj.matrix_world = Matrix.Identity(4)
    scene.collection.objects.link(obj)
    bpy.context.window.scene = scene
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.export_scene.fbx(filepath=str(OUT / (name + '.fbx')),
        use_selection=True, object_types={'MESH'}, bake_anim=False,
        use_mesh_modifiers=True, mesh_smooth_type='FACE', add_leaf_bones=False)
    textures = {}
    for node in source.data.materials[0].node_tree.nodes:
        if node.type != 'TEX_IMAGE' or not node.image:
            continue
        kind = next((k for k in ('BaseColor', 'Normal', 'OcclusionRoughnessMetallic')
                     if k in node.image.name), None)
        if not kind or kind in textures:
            continue
        filename = name[3:] + '_' + kind + '.png'
        node.image.filepath_raw = str(OUT / filename)
        node.image.file_format = 'PNG'
        node.image.save()
        textures[kind] = filename
    assert len(textures) == 3
    lo = [min(v.co[a] for v in obj.data.vertices) for a in range(3)]
    hi = [max(v.co[a] for v in obj.data.vertices) for a in range(3)]
    records.append({'name': name, 'vertices': len(obj.data.vertices),
        'faces': len(obj.data.polygons), 'material': source.data.materials[0].name,
        'bounds_cm': [lo, hi], 'textures': textures})
    bpy.data.objects.remove(obj, do_unlink=True)
report = {'source': str(source_path),
    'source_sha256': hashlib.sha256(source_path.read_bytes()).hexdigest(),
    'tools': records, 'brush_contact_cm': [72, 0, -20]}
(OUT / 'ToolReport.json').write_text(json.dumps(report, indent=2), encoding='utf8')
print('MC_TOOLS_EXPORT_PASS', json.dumps(records))
