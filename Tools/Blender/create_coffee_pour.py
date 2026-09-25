"""Small authored meshes for a controllable pour, impact crown and throat outflow."""
import bpy, math
from pathlib import Path
root=Path(__file__).resolve().parents[2]
out=root/'ArtSource'/'CoffeeWater'; out.mkdir(parents=True,exist_ok=True)
bpy.ops.object.select_all(action='SELECT'); bpy.ops.object.delete(use_global=False)
bpy.context.scene.unit_settings.system='METRIC'; bpy.context.scene.unit_settings.scale_length=.01

def grid(name,nx,ny,point,flip=False):
    verts=[point(x/nx,y/ny) for y in range(ny+1) for x in range(nx+1)]
    faces=[]
    for y in range(ny):
        for x in range(nx):
            i=y*(nx+1)+x; face=(i,i+1,i+nx+2,i+nx+1); faces.append(tuple(reversed(face)) if flip else face)
    mesh=bpy.data.meshes.new(name); mesh.from_pydata(verts,[],faces); mesh.update()
    obj=bpy.data.objects.new(name,mesh); bpy.context.collection.objects.link(obj)
    uv=mesh.uv_layers.new(name='UVMap')
    for face in mesh.polygons:
        face.use_smooth=True
        for loop in face.loop_indices:
            i=mesh.loops[loop].vertex_index; uv.data[loop].uv=((i%(nx+1))/nx,(i//(nx+1))/ny)
    return obj

def jet(u,v):
    a=u*math.tau
    r=50*(.77+.17*(1-v)**3+.045*math.sin(a*7+v*9)+.03*math.cos(a*11-v*12))
    return (r*math.cos(a)+4*math.sin(v*5),r*math.sin(a)+3*math.sin(v*8),100*v)

def crown(u,v):
    a=u*math.tau
    scallop=(.5+.5*math.sin(a*11+.7*math.sin(a*5)))**3
    r=45+v*(112+scallop*23)
    # Thin sheet bends out, with eleven uneven fingers rising from its rim.
    z=4+10*math.sin(v*math.pi)+v**3*(18+scallop*66)
    return (r*math.cos(a),r*math.sin(a),z)

def drain(u,v):
    width=50*(1-.58*v)
    return (100*v,(u*2-1)*width,-85*v*v+3*math.sin(u*math.pi))

objects=[grid('SM_CoffeeJet',48,32,jet),grid('SM_CoffeeCrown',96,8,crown,True),grid('SM_CoffeeDrain',12,28,drain,True)]
bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=2,radius=50)
drop=bpy.context.object; drop.name='SM_CoffeeDrop'
for face in drop.data.polygons: face.use_smooth=True
objects.append(drop)
for obj in objects:
    bpy.ops.object.select_all(action='DESELECT'); obj.select_set(True); bpy.context.view_layer.objects.active=obj
    bpy.ops.export_scene.fbx(filepath=str(out/(obj.name+'.fbx')),use_selection=True,object_types={'MESH'},
        apply_unit_scale=True,axis_forward='-Y',axis_up='Z',bake_anim=False,add_leaf_bones=False,mesh_smooth_type='FACE')
bpy.context.preferences.filepaths.save_version=0
bpy.ops.wm.save_as_mainfile(filepath=str(out/'CoffeePour.blend'))
print('MC_COFFEE_POUR_GEOMETRY_PASS',[(o.name,len(o.data.polygons)) for o in objects])
