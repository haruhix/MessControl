"""Read-only shadow-caster inventory/ray probes for two unexpected Closed disks."""
import argparse
import json
from pathlib import Path
import sys
import bpy
from mathutils import Vector

p=argparse.ArgumentParser()
p.add_argument('--candidate',required=True)
p.add_argument('--out',required=True)
opt=p.parse_args(sys.argv[sys.argv.index('--')+1:])
bpy.ops.wm.open_mainfile(filepath=str(Path(opt.candidate).resolve()))
scene=bpy.context.scene
root=bpy.data.objects['MC_JawsRoot']
root.animation_data_clear();root['OpenAmount']=0.;root.update_tag();scene.frame_set(42)
bpy.context.view_layer.update()
dg=bpy.context.evaluated_depsgraph_get()
objects=[]
for obj in scene.objects:
    objects.append({'name':obj.name,'type':obj.type,'hide_render':obj.hide_render,
                    'collections':[{ 'name':c.name,'hide_render':c.hide_render} for c in obj.users_collection],
                    'visible_diffuse':obj.visible_diffuse,'visible_glossy':obj.visible_glossy,
                    'visible_shadow':obj.visible_shadow,'location':list(obj.matrix_world.translation),
                    'materials':[slot.material.name if slot.material else None for slot in obj.material_slots] if obj.type=='MESH' else []})
results=[]
for x,y in [(325,89),(307,89),(342,89),(416,89),(399,89),(433,89)]:
    X=(x+.5-963/2)*14.2/963
    Z=(257/2-y-.5)*14.2/963
    hit,location,normal,face,obj,matrix=scene.ray_cast(dg,Vector((X,-30,Z)),Vector((0,1,0)))
    row={'pixel':[x,y],'first_hit':obj.name if hit else None,'position':list(location),'face_normal':list(normal),'face':face,'shadow_hits':{}}
    if hit:
        for light in scene.objects:
            if light.type!='LIGHT':continue
            delta=light.matrix_world.translation-location
            distance=delta.length
            sh,point,norm,idx,blocker,_=scene.ray_cast(dg,location+delta.normalized()*.0001,delta.normalized(),distance=distance-.0002)
            row['shadow_hits'][light.name]={'blocked':sh,'by':blocker.name if sh else None,'distance':(point-location).length if sh else None}
    results.append(row)
out=Path(opt.out);out.parent.mkdir(parents=True,exist_ok=True)
out.write_text(json.dumps({'source':opt.candidate,'objects':objects,'probes':results,'limit':'Scene rays include hide_render objects/collections; check visibility inventory before calling a render blocker.'},indent=2),encoding='utf8')
print('CASTER_INVENTORY',len(objects),str(out),flush=True)
