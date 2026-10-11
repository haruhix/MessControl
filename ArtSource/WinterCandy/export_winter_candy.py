"""Export the saved artist candy at its authored size, without saving the source.

Run in a separate background Blender loaded from candy.blend, with autoexec off.
The visible evaluated mesh supplies geometry; hidden sculpt backups are excluded.
"""
import bpy
import json
import hashlib
from collections import Counter
from pathlib import Path
from mathutils import Matrix, Vector

destination = Path('D:/PROJECT/GAME/WaifuTD/MessControl/ArtSource/WinterCandy')
source_scene = bpy.context.scene
assert Path(bpy.data.filepath).name.lower() == 'candy.blend'
source_was_dirty=bpy.data.is_dirty
source_digest=hashlib.sha256(Path(bpy.data.filepath).read_bytes()).hexdigest()
assert bpy.context.mode == 'OBJECT', 'Export the saved object, not an active edit operation.'
source_objects = [obj for obj in source_scene.objects if obj.type=='MESH' and obj.visible_get()]
assert source_objects, 'Export the visible painted candy, excluding hidden sculpt backups.'
graph=bpy.context.evaluated_depsgraph_get()
evaluated_meshes=[]
for obj in source_objects:
    evaluated=obj.evaluated_get(graph)
    mesh=bpy.data.meshes.new_from_object(evaluated,preserve_all_data_layers=True,depsgraph=graph)
    evaluated_meshes.append((obj,evaluated.matrix_world.copy(),mesh))
corners = [matrix @ vertex.co for _,matrix,mesh in evaluated_meshes for vertex in mesh.vertices]
minimum = Vector([min(point[i] for point in corners) for i in range(3)])
maximum = Vector([max(point[i] for point in corners) for i in range(3)])
center = (minimum + maximum) * .5
# Preserve the authored proportions and physical size. Only centre the pivot;
# FBX unit conversion changes metres to Unreal centimetres, uniformly on all axes.
units=source_scene.unit_settings.scale_length
normalize = Matrix.Scale(units, 4) @ Matrix.Translation(-center)
old_selected = list(bpy.context.selected_objects)
old_active = bpy.context.view_layer.objects.active
temporary_scene = bpy.data.scenes.new('MC_WinterCandy_Export')
copies, temporary_materials = [], []
report = {'source': bpy.data.filepath, 'source_dirty': source_was_dirty,
    'source_disk_sha256':source_digest, 'parts': []}
report['source_dimensions_cm']=[float((maximum[i]-minimum[i])*units*100) for i in range(3)]
report['source_unit_scale']=float(units)
report['source_geometry']='saved evaluated mesh'
report['authored_size_preserved']=True
try:
    bpy.context.window.scene = temporary_scene
    for source,matrix,mesh in evaluated_meshes:
        mesh.transform(normalize @ matrix)
        mesh.update()
        obj = bpy.data.objects.new('WinterCandy', mesh)
        temporary_scene.collection.objects.link(obj)
        copies.append(obj)
        mesh.materials.clear()
        for role in ('Body','Face'):
            material = bpy.data.materials.new('WinterCandy' + role)
            temporary_materials.append(material)
            mesh.materials.append(material)
        colours=mesh.color_attributes[0]
        main_colour=Counter(tuple(v.color) for v in colours.data).most_common(1)[0][0]
        for polygon in mesh.polygons:
            paint=[colours.data[i].color for i in polygon.loop_indices]
            body=sum(sum(abs(c[i]-main_colour[i]) for i in range(3))<.02 for c in paint)==len(paint)
            polygon.material_index=0 if body else 1
        mesh.color_attributes.render_color_index = 0
        obj.select_set(True)
        report['parts'].append({'name': source.name, 'vertices': len(mesh.vertices),
            'polygons': len(mesh.polygons), 'color_attribute': mesh.color_attributes[0].name,
            'color_domain': mesh.color_attributes[0].domain,
            'body_polygons':sum(p.material_index==0 for p in mesh.polygons),
            'face_polygons':sum(p.material_index==1 for p in mesh.polygons)})
    bpy.context.view_layer.objects.active = copies[0]
    destination.mkdir(parents=True, exist_ok=True)
    (destination/'VertexPositions.json').write_text(json.dumps(
        [[float(v.co.x*100),float(-v.co.y*100),float(v.co.z*100)] for obj in copies for v in obj.data.vertices]),encoding='utf8')
    bpy.ops.export_scene.fbx(filepath=str(destination / 'SM_WinterCandy.fbx'),
        use_selection=True, object_types={'MESH'}, apply_unit_scale=True,
        apply_scale_options='FBX_SCALE_ALL', axis_forward='-Y', axis_up='Z',
        mesh_smooth_type='FACE', use_mesh_modifiers=True, colors_type='SRGB',
        bake_anim=False, use_custom_props=False)
    report['fbx'] = str(destination / 'SM_WinterCandy.fbx')
    assert hashlib.sha256(Path(bpy.data.filepath).read_bytes()).hexdigest()==source_digest
    report['source_file_preserved']=True
    (destination / 'ExportReport.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report))
finally:
    bpy.context.window.scene = source_scene
    for obj in copies:
        mesh = obj.data
        bpy.data.objects.remove(obj, do_unlink=True)
        bpy.data.meshes.remove(mesh)
    bpy.data.scenes.remove(temporary_scene)
    for material in temporary_materials:
        bpy.data.materials.remove(material)
    for obj in source_scene.objects:
        obj.select_set(obj in old_selected)
    bpy.context.view_layer.objects.active = old_active
