"""Editable tissue mesh and shape-key preview of the runtime swallow cycle."""
import bpy, math
from pathlib import Path
root=Path('E:/DEVGAME/MessControl/ArtSource/LivingThroat')
scene=bpy.context.scene
scene.unit_settings.system='METRIC'
collection=bpy.data.collections.new('MC_Throat_Animation'); scene.collection.children.link(collection)
for prior in bpy.data.objects:
    if prior.name.startswith('ThroatTissue'):
        prior.hide_render=True; prior.hide_set(True)
around=96; rows=20
def surface(opening,breath=0):
    points=[]
    for r in range(rows+1):
        t=r/rows
        for i in range(around+1):
            a=2*math.pi*i/around
            x=t*(1.8+opening*1.2)-math.sin(t*3*math.pi)*.2+math.sin(a*9+.5)*math.sin(t*math.pi)*.08+breath*.03*math.sin(t*math.pi)
            y=(1+(.28+opening*.4-1)*t)*6.6*math.cos(a)
            z=(1+(opening*.74-1)*t)*5.0*math.sin(a)
            points.append((x,y,z))
    return points
faces=[]
for r in range(rows):
    for i in range(around):
        b=r*(around+1)+i; faces.append((b,b+around+1,b+around+2,b+1))
mesh=bpy.data.meshes.new('LivingThroat_Folds'); mesh.from_pydata(surface(0),[],faces); mesh.update()
obj=bpy.data.objects.new('ThroatTissue',mesh); collection.objects.link(obj)
uv=mesh.uv_layers.new(name='UVMap')
for p in mesh.polygons:
    p.use_smooth=True
    for j in p.loop_indices:
        k=mesh.loops[j].vertex_index; uv.data[j].uv=(k%(around+1)/around,k//(around+1)/rows)
obj.shape_key_add(name='Closed')
opened=obj.shape_key_add(name='Swallow_Open')
for p,co in zip(opened.data,surface(1)): p.co=co
breath=obj.shape_key_add(name='Breathing')
for p,co in zip(breath.data,surface(0,1)): p.co=co
for frame,value in [(1,0),(30,0),(42,1),(82,1),(108,0),(150,0)]:
    opened.value=value; opened.keyframe_insert('value',frame=frame)
for frame,value in [(1,0),(24,1),(48,0),(72,1),(96,0),(120,1),(150,0)]:
    breath.value=value; breath.keyframe_insert('value',frame=frame)
mat=bpy.data.materials.new('MouthTissue_Painter'); mat.use_nodes=True
nodes=mat.node_tree.nodes; links=mat.node_tree.links; bsdf=nodes.get('Principled BSDF')
bsdf.inputs['Roughness'].default_value=.25
bsdf.inputs['Subsurface Weight'].default_value=.12
for suffix,target in [('BaseColor','Base Color'),('Normal','Normal')]:
    tex=nodes.new('ShaderNodeTexImage'); tex.image=bpy.data.images.load(str(root/'Textures'/('TissueSwatch_Tissue_'+suffix+'.png')),check_existing=True)
    tex.image.pack()
    if suffix=='Normal':
        tex.image.colorspace_settings.name='Non-Color'; n=nodes.new('ShaderNodeNormalMap'); n.inputs['Strength'].default_value=.22
        links.new(tex.outputs['Color'],n.inputs['Color']); links.new(n.outputs['Normal'],bsdf.inputs[target])
    else: links.new(tex.outputs['Color'],bsdf.inputs[target])
mesh.materials.append(mat)
uvula=next(o for o in reversed(list(bpy.data.objects)) if o.name.startswith('SM_Uvula') and not o.hide_render)
uvula.location=(-2.7,0,3.7); uvula.scale=(1.35,1.5,5.7); uvula.data.materials.clear(); uvula.data.materials.append(mat)
for frame,z in [(1,5.7),(24,5.7),(30,6.26),(45,6.08),(78,6.08),(100,5.7),(150,5.7)]:
    uvula.scale.z=z; uvula.keyframe_insert('scale',frame=frame)
for frame,angle in [(1,-1.2),(24,1.2),(30,0),(82,0),(105,-1.2),(126,1.2),(150,-1.2)]:
    uvula.rotation_euler.x=math.radians(angle); uvula.keyframe_insert('rotation_euler',frame=frame)
scene.render.fps=30; scene.frame_start=1; scene.frame_end=150; scene.frame_set(1)
for o in bpy.context.selected_objects: o.select_set(False)
obj.select_set(True); uvula.select_set(True); bpy.context.view_layer.objects.active=obj
scene['Preview']='Frames 1–30: closed and breathing; 30–42: weighted stretch/open; 42–82: gulp; 82–108: close.'
bpy.ops.wm.save_as_mainfile(filepath=str(root/'LivingThroat.blend'))
result={'mesh':obj.name,'vertices':len(mesh.vertices),'shape_keys':[k.name for k in obj.data.shape_keys.key_blocks],'animation_frames':[1,150]}
