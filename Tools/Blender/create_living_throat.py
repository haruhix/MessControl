"""Create new editable throat art in an isolated collection; never edit the artist arena."""
import bpy, math
from pathlib import Path
root=Path('E:/DEVGAME/MessControl/ArtSource/LivingThroat'); root.mkdir(parents=True,exist_ok=True)
scene=bpy.context.scene
for previous in bpy.data.objects:
    if previous.name.startswith(('SM_Uvula','TissueSwatch')):
        previous.hide_render=True; previous.hide_set(True)
collection=bpy.data.collections.new('MC_LivingThroat'); scene.collection.children.link(collection)
material=bpy.data.materials.new('Tissue'); material.diffuse_color=(.55,.08,.12,1)
verts=[]; faces=[]; around=64; rows=64
for j in range(rows+1):
    t=j/rows
    radius=.14+.32*(1-t)**2+.025*math.sin(t*math.pi)
    if t>.912:
        radius=max(radius if t<.95 else 0,.48*math.sqrt(max(0,1-((t-.956)/.044)**2)))
    radius=max(.002,radius)
    for i in range(around+1):
        a=2*math.pi*i/around
        verts.append((math.cos(a)*radius*.78,math.sin(a)*radius,-t))
        if i<around and j<rows:
            b=j*(around+1)+i; faces.append((b,b+1,b+around+2,b+around+1))
mesh=bpy.data.meshes.new('Uvula_Editable'); mesh.from_pydata(verts,[],faces); mesh.update()
obj=bpy.data.objects.new('SM_Uvula',mesh); collection.objects.link(obj); mesh.materials.append(material)
uv=mesh.uv_layers.new(name='UVMap')
for polygon in mesh.polygons:
    polygon.use_smooth=True
    for index in polygon.loop_indices:
        vi=mesh.loops[index].vertex_index; uv.data[index].uv=(vi%(around+1)/around,vi//(around+1)/rows)
for other in bpy.context.selected_objects: other.select_set(False)
obj.select_set(True); bpy.context.view_layer.objects.active=obj
bpy.ops.export_scene.fbx(filepath=str(root/'SM_Uvula.fbx'),use_selection=True,object_types={'MESH'},apply_unit_scale=True,axis_forward='-Y',axis_up='Z',bake_anim=False)
# A plane uses the full 0..1 tile so Painter exports a reusable tissue texture.
mesh2=bpy.data.meshes.new('TissueSwatch'); mesh2.from_pydata([(-1,-1,0),(1,-1,0),(1,1,0),(-1,1,0)],[],[(0,1,2,3)]); mesh2.update()
swatch=bpy.data.objects.new('TissueSwatch',mesh2); collection.objects.link(swatch); mesh2.materials.append(material)
uv2=mesh2.uv_layers.new(name='UVMap')
for i,p in enumerate([(0,0),(1,0),(1,1),(0,1)]): uv2.data[i].uv=p
obj.select_set(False); swatch.select_set(True); bpy.context.view_layer.objects.active=swatch
bpy.ops.export_scene.fbx(filepath=str(root/'TissueSwatch.fbx'),use_selection=True,object_types={'MESH'},bake_anim=False)
swatch.hide_render=True; swatch.hide_set(True)
obj.select_set(True); bpy.context.view_layer.objects.active=obj
bpy.ops.wm.save_as_mainfile(filepath=str(root/'LivingThroat.blend'))
result={'blend':str(root/'LivingThroat.blend'),'uvula':str(root/'SM_Uvula.fbx'),'vertices':len(verts),'tissue_swatch':str(root/'TissueSwatch.fbx')}
