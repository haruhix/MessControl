"""A 100 cm grid for the lightweight coffee surface; no collision or fluid simulation."""
import bpy
from pathlib import Path
root=Path(__file__).resolve().parents[2]
out=root/'ArtSource'/'CoffeeWater'
out.mkdir(parents=True,exist_ok=True)
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
bpy.context.scene.unit_settings.system='METRIC'
bpy.context.scene.unit_settings.scale_length=.01
nx,ny=64,48
vertices=[(-50+100*x/nx,-50+100*y/ny,0) for y in range(ny+1) for x in range(nx+1)]
faces=[]
for y in range(ny):
    for x in range(nx):
        i=y*(nx+1)+x
        faces.append((i,i+1,i+nx+2,i+nx+1))
mesh=bpy.data.meshes.new('CoffeeGrid'); mesh.from_pydata(vertices,[],faces); mesh.update()
obj=bpy.data.objects.new('SM_CoffeeSurface',mesh); bpy.context.collection.objects.link(obj)
uv=mesh.uv_layers.new(name='UVMap')
for face in mesh.polygons:
    face.use_smooth=True
    for loop in face.loop_indices:
        co=mesh.vertices[mesh.loops[loop].vertex_index].co
        uv.data[loop].uv=((co.x+50)/100,(co.y+50)/100)
obj.select_set(True); bpy.context.view_layer.objects.active=obj
bpy.ops.export_scene.fbx(filepath=str(out/'SM_CoffeeSurface.fbx'),use_selection=True,object_types={'MESH'},
    apply_unit_scale=True,axis_forward='-Y',axis_up='Z',bake_anim=False,add_leaf_bones=False,mesh_smooth_type='FACE')
bpy.ops.wm.save_as_mainfile(filepath=str(out/'CoffeeSurface.blend'))
print('MC_COFFEE_GRID_PASS',len(vertices),len(faces)*2)
