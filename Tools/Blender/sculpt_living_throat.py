"""Author layered mucosal folds as an editable skinned mesh with gameplay morphs."""
import bpy, math, json
from pathlib import Path
from mathutils import Vector
root=Path('E:/DEVGAME/MessControl/ArtSource/LivingThroat')
scene=bpy.context.scene
scene.unit_settings.system='METRIC'; scene.unit_settings.scale_length=1
for previous in bpy.data.objects:
    if previous.name.startswith(('ThroatTissue','SK_LivingThroat','ThroatRoot','SM_Uvula','Armature')):
        previous.hide_render=True; previous.hide_set(True)
        previous.name='Archived_'+previous.name
collection=bpy.data.collections.new('MC_Detailed_Throat'); scene.collection.children.link(collection)

def smooth_profile(values,t):
    q=t*(len(values)-1); i=min(len(values)-2,int(q)); f=q-i
    p0=values[max(0,i-1)]; p1=values[i]; p2=values[i+1]; p3=values[min(len(values)-1,i+2)]
    return .5*((2*p1)+(-p0+p2)*f+(2*p0-5*p1+4*p2-p3)*f*f+(-p0+3*p1-3*p2+p3)*f*f*f)

around=192; rows=112
def throat_vertices(opening=0,breath=0,contraction=0):
    vertices=[]
    ry=[11.4,9.15,7.65,6.9,5.9,5.1,4.45,3.6,2.2,.12]
    rz=[8.7,7.20,5.7,5.2,4.35,3.8,3.2,2.55,1.4,.10]
    depth=[-1.25,-.15,.65,1.9,3.3,4.8,6.3,8.1,10.2,12.5]
    for j in range(rows+1):
        t=j/rows; y_radius=max(.01,smooth_profile(ry,t)); z_radius=max(.005,smooth_profile(rz,t))
        closure=math.exp(-((t-.58)/.078)**2)*(1-opening)
        y_radius*=1-.42*closure
        z_radius*=1-.9985*closure
        x_base=smooth_profile(depth,t)
        for i in range(around+1):
            a=2*math.pi*i/around
            organic=1+.045*math.sin(a*3+t*4)+.025*math.cos(a*5-t*3)+.012*math.sin(a*11+t*7)
            folds=(.24*math.sin(t*math.pi*10+.30*math.sin(a*3))+.055*math.sin(a*13+t*12))*math.sin(t*math.pi)**.65
            fine=.012*math.sin(a*39+t*79)*math.sin(a*21-t*53)+.006*math.cos(a*67-t*101)
            x=x_base+folds+fine+.11*math.sin(a*2+.4)*math.sin(t*math.pi)
            roll=.38*math.sin(t*math.pi*8+.17*math.sin(a*3))*math.sin(t*math.pi)**.65
            y=(y_radius+roll*(1-.42*closure))*math.cos(a)*organic+.12*math.sin(t*math.pi*1.5)
            z=(z_radius+roll*(1-.9985*closure))*math.sin(a)*(organic+.02*math.sin(a*7))+.10*math.sin(t*math.pi*2)
            # Radial ridges change silhouette and catch highlights, beyond normal-map detail.
            ripple=1+.012*math.sin(a*17+t*13)*math.sin(t*math.pi)
            y*=ripple; z*=ripple
            y*=1+breath*.009*math.sin(t*math.pi)-contraction*.08*math.exp(-((t-.68)/.20)**2)
            z*=1+breath*.014*math.sin(t*math.pi)-contraction*.09*math.exp(-((t-.68)/.20)**2)
            vertices.append((x,y,z))
    return vertices

faces=[]
for j in range(rows):
    for i in range(around):
        b=j*(around+1)+i; faces.append((b,b+around+1,b+around+2,b+1))
mesh=bpy.data.meshes.new('Detailed_Mucosal_Folds'); mesh.from_pydata(throat_vertices(),[],faces); mesh.update()
throat=bpy.data.objects.new('SK_LivingThroat',mesh); collection.objects.link(throat)
uv=mesh.uv_layers.new(name='UVMap')
for polygon in mesh.polygons:
    polygon.use_smooth=True
    for k in polygon.loop_indices:
        index=mesh.loops[k].vertex_index; uv.data[k].uv=(index%(around+1)/around,index//(around+1)/rows)
throat.shape_key_add(name='Basis')
for name,points in [('SwallowOpen',throat_vertices(1)),('Breath',throat_vertices(0,1)),('Peristalsis',throat_vertices(0,0,1))]:
    key=throat.shape_key_add(name=name)
    for point,co in zip(key.data,points): point.co=co

arm=bpy.data.armatures.new('ThroatRoot'); rig=bpy.data.objects.new('Armature',arm); collection.objects.link(rig)
for o in bpy.context.selected_objects: o.select_set(False)
rig.select_set(True); bpy.context.view_layer.objects.active=rig
bpy.ops.object.mode_set(mode='EDIT'); bone=arm.edit_bones.new('root'); bone.head=(0,0,0); bone.tail=(0,0,1); bpy.ops.object.mode_set(mode='OBJECT')
group=throat.vertex_groups.new(name='root'); group.add(list(range(len(mesh.vertices))),1,'REPLACE')
mod=throat.modifiers.new('ThroatRoot','ARMATURE'); mod.object=rig; throat.parent=rig

mat=bpy.data.materials.get('LivingMucosa_Detailed') or bpy.data.materials.new('LivingMucosa_Detailed'); mat.use_nodes=True
mat.node_tree.nodes.clear(); bsdf=mat.node_tree.nodes.new('ShaderNodeBsdfPrincipled'); output=mat.node_tree.nodes.new('ShaderNodeOutputMaterial')
mat.node_tree.links.new(bsdf.outputs['BSDF'],output.inputs['Surface'])
bsdf.inputs['Roughness'].default_value=.23; bsdf.inputs['Subsurface Weight'].default_value=.14
for suffix,socket in [('BaseColor','Base Color'),('Normal','Normal')]:
    tex=mat.node_tree.nodes.new('ShaderNodeTexImage'); tex.image=bpy.data.images.load(str(root/'Textures'/('TissueSwatch_Tissue_'+suffix+'.png')),check_existing=True); tex.image.reload(); tex.image.pack()
    if suffix=='Normal':
        tex.image.colorspace_settings.name='Non-Color'; normal=mat.node_tree.nodes.new('ShaderNodeNormalMap'); normal.inputs['Strength'].default_value=.4
        mat.node_tree.links.new(tex.outputs['Color'],normal.inputs['Color']); mat.node_tree.links.new(normal.outputs['Normal'],bsdf.inputs[socket])
    else: mat.node_tree.links.new(tex.outputs['Color'],bsdf.inputs[socket])
mesh.materials.append(mat)
for o in bpy.context.selected_objects: o.select_set(False)
throat.select_set(True); rig.select_set(True); bpy.context.view_layer.objects.active=rig
bpy.ops.export_scene.fbx(filepath=str(root/'SK_LivingThroat.fbx'),use_selection=True,object_types={'MESH','ARMATURE'},add_leaf_bones=False,use_armature_deform_only=True,apply_unit_scale=True,apply_scale_options='FBX_SCALE_NONE',axis_forward='-Y',axis_up='Z',use_mesh_modifiers=False,mesh_smooth_type='FACE',bake_anim=False)

# Reachable uvula: curved, uneven tissue with a soft landing bulb.
uv=[]; uf=[]; rings=192; sides=96
for j in range(rings+1):
    t=-1.6+2.6*j/rings
    radius=.38+.11*(1+math.cos(max(0,t)*math.pi))+.05*math.sin(max(0,t)*math.pi)**2*math.sin(t*4.2+.6)
    if t<0:
        flare=min(1,-t/1.5); radius=.60+3.6*flare*flare*(3-2*flare)
    if t>.60:
        bulb=.46*math.sqrt(max(0,1-((t-.78)/.22)**2))
        blend=min(1,max(0,(t-.60)/.16)); blend=blend*blend*(3-2*blend)
        radius=radius*(1-blend)+bulb*blend
    radius=max(.0015,radius)
    for i in range(sides+1):
        a=2*math.pi*i/sides
        folded=radius*(1+.065*math.sin(a*5+t*10)+.035*math.cos(a*9-t*7))
        grain=.0017*math.sin(a*29+t*89)*math.sin(a*17-t*77)
        front_scale=.30+.56*math.exp(-(t/.45)**2) if t<0 else .86
        uv.append((.21*math.sin(t*math.pi)+math.cos(a)*(folded+grain)*front_scale,.065*math.sin(t*math.pi*2)+math.sin(a)*(folded+grain),-t))
        if j<rings and i<sides:
            b=j*(sides+1)+i; uf.append((b,b+sides+1,b+sides+2,b+1))
um=bpy.data.meshes.new('Detailed_Uvula_Folds'); um.from_pydata(uv,[],uf); um.update(); uvula=bpy.data.objects.new('SM_Uvula',um); collection.objects.link(uvula); um.materials.append(mat)
layer=um.uv_layers.new(name='UVMap')
for p in um.polygons:
    p.use_smooth=True
    for k in p.loop_indices:
        index=um.loops[k].vertex_index; layer.data[k].uv=(index%(sides+1)/sides,index//(sides+1)/rings)
for o in bpy.context.selected_objects: o.select_set(False)
uvula.select_set(True); bpy.context.view_layer.objects.active=uvula
bpy.ops.export_scene.fbx(filepath=str(root/'SM_Uvula.fbx'),use_selection=True,object_types={'MESH'},apply_unit_scale=True,axis_forward='-Y',axis_up='Z',mesh_smooth_type='FACE',bake_anim=False)
uvula.location=(-2.7,0,6.3); uvula.scale=(.7,.85,1.9)

# A continuous cheek and palatal vault meets the throat, with broad anatomical folds.
# Coordinates are world metres relative to the arena centre (0,-.3,2.8).
for previous in bpy.data.objects:
    if previous.name.startswith('SM_SoftPalate'): previous.hide_render=True; previous.hide_set(True)
shell_v=[]; shell_f=[]; length_rows=96; arc_sides=192
for j in range(length_rows+1):
    t=j/length_rows; x=-18+31.25*t
    ry_shell=12.8-(12.8-11.4*500/660)*t+.55*math.sin(t*math.pi)
    rz_shell=10.6-1.9*t
    for i in range(arc_sides+1):
        a=i/arc_sides*2*math.pi
        organic=1+.045*math.sin(a*3)+.025*math.cos(a*5)+.012*math.sin(a*11)
        roof=max(0,math.sin(a))**3
        rugae=roof*.10*math.sin(x*2.15+1.7*abs(math.cos(a)))*math.exp(-((t-.61)/.26)**4)
        sidefold=.035*math.cos(a*8+x*.45)*math.sin(t*math.pi)
        shell_v.append((x,(ry_shell+sidefold)*math.cos(a)*organic,(rz_shell+rugae)*math.sin(a)*(organic+.02*math.sin(a*7))))
        if j<length_rows and i<arc_sides:
            b=j*(arc_sides+1)+i; shell_f.append((b,b+arc_sides+1,b+arc_sides+2,b+1))
sm=bpy.data.meshes.new('SoftPalate_Cheeks'); sm.from_pydata(shell_v,[],shell_f); sm.update()
shell=bpy.data.objects.new('SM_SoftPalate',sm); collection.objects.link(shell); sm.materials.append(mat)
layer=sm.uv_layers.new(name='UVMap')
for p in sm.polygons:
    p.use_smooth=True
    for k in p.loop_indices:
        index=sm.loops[k].vertex_index; layer.data[k].uv=(index%(arc_sides+1)/arc_sides,index//(arc_sides+1)/length_rows)
for o in bpy.context.selected_objects: o.select_set(False)
shell.select_set(True); bpy.context.view_layer.objects.active=shell
bpy.ops.export_scene.fbx(filepath=str(root/'SM_SoftPalate.fbx'),use_selection=True,object_types={'MESH'},apply_unit_scale=True,axis_forward='-Y',axis_up='Z',mesh_smooth_type='FACE',bake_anim=False)
shell.location=(-14.5,0,0)
shell.hide_render=True; shell.hide_set(True)

keys=throat.data.shape_keys.key_blocks
for frame,value in [(1,0),(30,0),(42,1),(82,1),(110,0),(150,0)]:
    keys['SwallowOpen'].value=value; keys['SwallowOpen'].keyframe_insert('value',frame=frame)
for frame,value in [(1,0),(24,1),(48,0),(72,1),(96,0),(120,1),(150,0)]:
    keys['Breath'].value=value; keys['Breath'].keyframe_insert('value',frame=frame)
for frame,value in [(1,0),(42,0),(60,1),(78,0),(150,0)]:
    keys['Peristalsis'].value=value; keys['Peristalsis'].keyframe_insert('value',frame=frame)
for frame,value in [(1,1.9),(24,1.9),(30,2.46),(44,2.28),(78,2.28),(100,1.9),(150,1.9)]:
    uvula.scale.z=value; uvula.keyframe_insert('scale',frame=frame)
scene.frame_start=1; scene.frame_end=150; scene.render.fps=30; scene.frame_set(1)
if scene.camera:
    scene.camera.location=(-14,-3,3.5); scene.camera.rotation_euler=(Vector((1,0,0))-scene.camera.location).to_track_quat('-Z','Y').to_euler(); scene.camera.data.lens=42
report={'throat_vertices':len(mesh.vertices),'throat_triangles':len(faces)*2,'uvula_vertices':len(um.vertices),'morphs':['SwallowOpen','Breath','Peristalsis'],'skeletal_export':str(root/'SK_LivingThroat.fbx')}
(root/'SculptReport.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
bpy.ops.wm.save_as_mainfile(filepath=str(root/'LivingThroat.blend'))
result=report
