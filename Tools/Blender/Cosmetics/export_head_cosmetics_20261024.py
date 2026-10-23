"""Export the seven source accessories; input and gameplay character are never saved."""
import bpy,json,math,hashlib
from pathlib import Path
from mathutils import Vector,Matrix,Quaternion
ROOT=Path(__file__).resolve().parents[3];OUT=ROOT/'ArtSource/CharacterCurrent/HeadCosmetics20261024';OUT.mkdir(parents=True,exist_ok=True);(OUT/'Icons').mkdir(exist_ok=True)
SPECS=[('Cube.001','CowboyHat','Hat','Ковбойская шляпа','Cowboy hat'),('glass_1','SquareGlasses','Face','Прямоугольные очки','Square glasses'),('Plane','Feather','Hat','Перо','Feather'),('SM_Character.001','Brows','Face','Ресницы','Eyelashes'),('Sphere','ConeHat','Hat','Коническая шляпа','Conical hat'),('Sphere.001','ArtistCap','Hat','Авторская кепка','Artist cap'),('Sphere.002','FootballHelmet','Hat','Спортивный шлем','Football helmet')]
reference=json.loads((OUT/'ReferenceHead.json').read_text());q=reference['rotation'];bind=Matrix.Translation(Vector(reference['translation']))@Quaternion((q[3],q[0],q[1],q[2])).to_matrix().to_4x4()@Matrix.Diagonal((*reference['scale'],1));bind_inv=bind.inverted()
def bounds(ps):return [[min(p[i] for p in ps) for i in range(3)],[max(p[i] for p in ps) for i in range(3)]]
report={'source':bpy.data.filepath,'source_sha256':hashlib.sha256(Path(bpy.data.filepath).read_bytes()).hexdigest(),'reference_head':reference,'items':[],'notes':['Seven authored objects from character1.003, excluding controls and reference meshes.','Geometry is baked into actual UE head_x reference-bone space; identity attachment preserves rotated nonuniform head scale.','The parked cap is translated back to the verified original cap fit without changing its shape.','Unassigned materials use neutral source default grey; authored black glasses remain black.']}
scene=bpy.data.scenes.new('ExportHeadCosmetics');bpy.context.window.scene=scene;scene.unit_settings.system='METRIC';scene.unit_settings.scale_length=1
def material(name,color):
    m=bpy.data.materials.new(name);m.use_nodes=True;p=m.node_tree.nodes.get('Principled BSDF');p.inputs['Base Color'].default_value=(*color,1);p.inputs['Roughness'].default_value=.5;return m
neutral=material('HeadCosmetics_Neutral',(.8,.8,.8));black=material('HeadCosmetics_Black',(.0074877683,)*3);brow=material('HeadCosmetics_Brows',(.045,.023,.015))
meshes=[];preview_points=[];deps=bpy.context.evaluated_depsgraph_get()
for source_name,slug,slot,label,en in SPECS:
    source=bpy.data.objects[source_name];evaluated=source.evaluated_get(deps);data=bpy.data.meshes.new_from_object(evaluated,preserve_all_data_layers=True,depsgraph=deps)
    shift=Vector()
    if slug=='ArtistCap':
        cap=json.loads((ROOT/'ArtSource/CharacterCurrent/Cosmetics/CosmeticsReport.json').read_text(encoding='utf8'))['items'][0]
        target=[Vector(x)*.01 for x in cap['bounds_cm']];bb=[Vector(x) for x in bounds([source.matrix_world@v.co for v in data.vertices])]
        assert ((target[1]-target[0])-(bb[1]-bb[0])).length<.00001
        shift=(target[0]+target[1]-bb[0]-bb[1])*.5
    reference_points=[source.matrix_world@v.co+shift for v in data.vertices]
    for v,p in zip(data.vertices,reference_points):
        local=bind_inv@Vector((p.x*100,-p.y*100,p.z*100));v.co=Vector((local.x,-local.y,local.z))
    name='SM_HeadCosmetic_'+slug;o=bpy.data.objects.new(name,data);scene.collection.objects.link(o);o.scale=(.01,)*3
    original_materials=[m.name if m else None for m in data.materials];data.materials.clear();data.materials.append(black if slug=='SquareGlasses' else brow if slug=='Brows' else neutral)
    for p in data.polygons:p.material_index=0
    bpy.context.view_layer.update()
    ps=[o.matrix_world@v.co for v in data.vertices];bb=bounds(ps)
    for other in scene.objects:other.select_set(False)
    o.select_set(True);bpy.context.view_layer.objects.active=o;file=OUT/(name+'.fbx')
    bpy.ops.export_scene.fbx(filepath=str(file),use_selection=True,object_types={'MESH'},bake_anim=False,use_mesh_modifiers=True,mesh_smooth_type='FACE',add_leaf_bones=False)
    row={'source_object':source_name,'slug':slug,'id':slot+'_Head_'+slug,'slot':slot,'label_ru':label,'label_en':en,'mesh':name,'fbx':str(file),'fbx_sha256':hashlib.sha256(file.read_bytes()).hexdigest(),'vertices':len(data.vertices),'triangles':sum(len(p.vertices)-2 for p in data.polygons),'bounds_blender_cm':[[p*100 for p in x] for x in bb],'bounds_unreal_cm':[[bb[0][0]*100,-bb[1][1]*100,bb[0][2]*100],[bb[1][0]*100,-bb[0][1]*100,bb[1][2]*100]],'geometry_space':'UE reference head_x bone space','restored_translation_m':list(shift),'material_role':'Black' if slug=='SquareGlasses' else 'Brows' if slug=='Brows' else 'Neutral','original_materials':original_materials}
    # Preserve world-space source geometry after the sole documented cap translation.
    reconstructed=[bind@Vector((v.co.x,-v.co.y,v.co.z)) for v in data.vertices]
    error=max((Vector((p.x*.01,-p.y*.01,p.z*.01))-original).length for p,original in zip(reconstructed,reference_points))
    assert error<.000001,error;row['max_source_vertex_error_m']=error;report['items'].append(row);meshes.append(o);preview_points.append(reference_points)
bpy.data.libraries.write(str(OUT/'HeadCosmetics_Export.blend'),set(meshes),path_remap='RELATIVE',fake_user=True,compress=True)
# Icons use the authored reference orientation; only the exported meshes use bone space.
for o,points in zip(meshes,preview_points):
    for v,p in zip(o.data.vertices,points):v.co=p*100
    o.data.update()
scene.render.engine='CYCLES';scene.cycles.samples=20;scene.cycles.device='CPU';scene.render.resolution_x=512;scene.render.resolution_y=512;scene.render.resolution_percentage=100;scene.render.film_transparent=True
scene.world=bpy.data.worlds.new('HeadIconsWorld');scene.world.use_nodes=True;scene.world.node_tree.nodes['Background'].inputs['Strength'].default_value=.4
for name,loc,power,size in [('Key',(-2,-3,4),350,3),('Fill',(3,-1,2),120,3),('Rim',(0,3,3),220,2)]:
    d=bpy.data.lights.new(name,'AREA');d.energy=power;d.size=size;l=bpy.data.objects.new(name,d);scene.collection.objects.link(l);l.location=loc;l.rotation_euler=(Vector((0,0,1))-l.location).to_track_quat('-Z','Y').to_euler()
d=bpy.data.cameras.new('IconCamera');cam=bpy.data.objects.new('IconCamera',d);scene.collection.objects.link(cam);scene.camera=cam;d.type='ORTHO'
for o in meshes:o.hide_render=True
for o,row in zip(meshes,report['items']):
    o.hide_render=False;ps=[o.matrix_world@v.co for v in o.data.vertices];bb=[Vector(x) for x in bounds(ps)];center=(bb[0]+bb[1])*.5;cam.location=center+Vector((2,-3,1.5));cam.rotation_euler=(center-cam.location).to_track_quat('-Z','Y').to_euler();d.ortho_scale=max(bb[1]-bb[0])*1.4
    row['icon']=str(OUT/'Icons'/(row['id']+'.png'));scene.render.filepath=row['icon'];bpy.ops.render.render(write_still=True);o.hide_render=True
(OUT/'ExportReport.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
assert hashlib.sha256(Path(bpy.data.filepath).read_bytes()).hexdigest()==report['source_sha256']
print('HEAD_COSMETICS_EXPORT_READY',len(meshes),str(OUT/'ExportReport.json'))
