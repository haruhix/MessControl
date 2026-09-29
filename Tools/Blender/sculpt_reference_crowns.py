"""Rounded molar crowns with four cusps, shallow fissures and broad enamel faces."""
import bpy,math,json
from pathlib import Path
from mathutils import Vector
root=Path('E:/DEVGAME/MessControl/ArtSource/LivingThroat')
report=[]
for obj in [o for o in bpy.data.objects if o.type=='MESH' and o.name.startswith('SM_ReferenceTooth_')]:
    old=obj.data
    lo=Vector([min(v.co[k] for v in old.vertices) for k in range(3)])
    hi=Vector([max(v.co[k] for v in old.vertices) for k in range(3)])
    old.use_fake_user=True
    variant=int(obj.name.split('_')[2]);mirror=-1 if 'Mirrored' in obj.name else 1
    sides=96;verts=[];faces=[];rings=[]
    for j in range(25):
        t=j/24;r=.80+.205*math.sin(t*math.pi*.74)
        if t>.80:r-=.06*((t-.80)/.20)**2
        rings.append((r,.84*t,False))
    for j in range(1,25):rings.append((rings[24][0]*(1-j/25),.84,True))
    for j,(r,z,cap) in enumerate(rings):
        for i in range(sides):
            a=2*math.pi*i/sides;c=math.cos(a);s=math.sin(a)
            x=r*math.copysign(abs(c)**.62,c);y=r*math.copysign(abs(s)**.62,s)
            zz=z
            if cap:
                cusp=0
                for cx,cy,weight in [(-.47,-.46,1),(-.47,.46,.88),(.47,-.46,.86),(.47,.46,.96)]:
                    cusp+=weight*math.exp(-((x-cx)**2/.17+(y-cy)**2/.16))
                edge=1-math.exp(-((1-r)/.19)**2)
                groove=.008*math.exp(-((x+.025*math.sin(y*8+variant))/.095)**2)+.006*math.exp(-(y/.10)**2)
                zz+=edge*(.055*cusp+.020*(1-r*r)-groove)
            else:
                t=z/.84
                x*=1+.014*math.cos(a*3+variant)*math.sin(t*math.pi)**2
                x-=math.copysign(.014*math.exp(-(y/.12)**2)*math.sin(t*math.pi)**2,x)
            zz+=.008*mirror*y*math.sin(j/len(rings)*math.pi)
            verts.append((x,y,zz))
            if j<len(rings)-1:
                b=j*sides+i;n=j*sides+(i+1)%sides;faces.append((b,n,n+sides,b+sides))
    faces.append(tuple(reversed(range(sides))))
    faces.append(tuple((len(rings)-1)*sides+i for i in range(sides)))
    mesh=bpy.data.meshes.new(obj.name+'_FourCusps');mesh.from_pydata(verts,[],faces);mesh.update()
    for m in old.materials:mesh.materials.append(m)
    obj.data=mesh
    for o in bpy.context.selected_objects:o.select_set(False)
    obj.hide_set(False);obj.select_set(True);bpy.context.view_layer.objects.active=obj
    mod=obj.modifiers.new('Rounded enamel shoulders','SUBSURF');mod.levels=1;bpy.ops.object.modifier_apply(modifier=mod.name)
    mesh=obj.data
    newlo=Vector([min(v.co[k] for v in mesh.vertices) for k in range(3)]);newhi=Vector([max(v.co[k] for v in mesh.vertices) for k in range(3)])
    for v in mesh.vertices:
        for k in range(3):v.co[k]=lo[k]+(v.co[k]-newlo[k])/(newhi[k]-newlo[k])*(hi[k]-lo[k])
    for p in mesh.polygons:p.use_smooth=True
    bpy.ops.object.mode_set(mode='EDIT');bpy.ops.mesh.select_all(action='SELECT');bpy.ops.uv.smart_project(island_margin=.02);bpy.ops.object.mode_set(mode='OBJECT')
    matrix=obj.matrix_world.copy();obj.location=(0,0,0)
    bpy.ops.export_scene.fbx(filepath=str(root/'Teeth'/(obj.name+'.fbx')),use_selection=True,object_types={'MESH'},apply_unit_scale=True,axis_forward='-Y',axis_up='Z',mesh_smooth_type='FACE',bake_anim=False)
    obj.matrix_world=matrix;obj.hide_set(True)
    report.append({'mesh':obj.name,'vertices':len(mesh.vertices),'triangles':sum(len(p.vertices)-2 for p in mesh.polygons)})
(root/'Teeth/TeethReport.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
bpy.ops.wm.save_as_mainfile(filepath=str(root/'LivingThroat.blend'))
result={'crowns':report}
