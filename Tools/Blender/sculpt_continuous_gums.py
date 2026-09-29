"""Continuous curved gingival ridges, replacing visible joins between gum cuffs."""
import bpy,math
from pathlib import Path
root=Path('E:/DEVGAME/MessControl/ArtSource/LivingThroat')
collection=bpy.data.collections.get('MC_Reference_Gums')
if collection is None:collection=bpy.data.collections.new('MC_Reference_Gums');bpy.context.scene.collection.children.link(collection)
report=[]
for side in (-1,1):
    name='SM_ReferenceGum_'+('L' if side<0 else 'R')
    previous=bpy.data.objects.get(name)
    if previous:previous.name='Archived_'+name;previous.hide_set(True);previous.hide_render=True
    verts=[];faces=[];rows=192;sides=64
    for j in range(rows+1):
        x=-10+23*j/rows;t=max(0,min(1,(x+5.1)/15.35))
        y=side*(9.4-2*t*t)-.3
        end=min(1,(x+10)/1.0,(13-x)/1.0);end=math.sqrt(max(.001,1-(1-end)**2))
        z=-.95+.06*math.sin(t*math.pi)
        for i in range(sides):
            a=2*math.pi*i/sides
            vertical=.69*math.sin(a)
            scallop=.055*math.cos((x+5.1)/2.30*math.pi*2)*max(0,math.sin(a))**3
            verts.append((x,y+1.35*math.cos(a)*end,z+(vertical+scallop)*end))
            if j<rows:
                b=j*sides+i;n=j*sides+(i+1)%sides;faces.append((b,n,n+sides,b+sides))
    faces.append(tuple(reversed(range(sides))));faces.append(tuple(rows*sides+i for i in range(sides)))
    mesh=bpy.data.meshes.new(name);mesh.from_pydata(verts,[],faces);mesh.update()
    obj=bpy.data.objects.new(name,mesh);collection.objects.link(obj)
    mat=bpy.data.materials.get('LivingMucosa_Detailed')
    if mat:mesh.materials.append(mat)
    for p in mesh.polygons:p.use_smooth=True
    uv=mesh.uv_layers.new(name='UVMap')
    for p in mesh.polygons:
        for k in p.loop_indices:
            v=mesh.loops[k].vertex_index;uv.data[k].uv=(v//sides/rows,v%sides/sides)
    for o in bpy.context.selected_objects:o.select_set(False)
    obj.select_set(True);bpy.context.view_layer.objects.active=obj
    bpy.ops.export_scene.fbx(filepath=str(root/(name+'.fbx')),use_selection=True,object_types={'MESH'},apply_unit_scale=True,axis_forward='-Y',axis_up='Z',mesh_smooth_type='FACE',bake_anim=False)
    obj.hide_set(True);obj.hide_render=True;report.append({'mesh':name,'vertices':len(verts)})
bpy.ops.wm.save_as_mainfile(filepath=str(root/'LivingThroat.blend'))
result={'gums':report}
