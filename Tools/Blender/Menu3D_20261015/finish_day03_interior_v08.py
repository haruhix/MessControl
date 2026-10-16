"""Bounded dimensional cavity/tongue finish on the saved V03 source.

No visible speculative throat aperture or uvula. Closed surfaces/unique source
UV0/pivots are preserved. +Y native-facing export uses the same source Y flip and
normal winding conversion as the accepted native mouth handoff.
"""
import bpy,bmesh,numpy as np,json,hashlib,math,argparse,sys
from pathlib import Path
from mathutils import Vector,Matrix
assert bpy.app.background
p=argparse.ArgumentParser();p.add_argument('--finalize',action='store_true');args=p.parse_args(sys.argv[sys.argv.index('--')+1:]if'--'in sys.argv else[])
BASE=Path('E:/DEVGAME/MessControl/ArtSource/UI/MainMenu3D/20261015_refB_v1');SOURCE=BASE/'Interior/V03/MouthInterior.blend';OUT=BASE/'Assembly/UnrealHandoff/v08_ReferenceFinish/Interior';OUT.mkdir(parents=True,exist_ok=True)
source_sha=hashlib.sha256(SOURCE.read_bytes()).hexdigest();bpy.ops.wm.open_mainfile(filepath=str(SOURCE))
source_names={'SM_DAY03_Interior':'Interior_OrganicBuccalPalate_Closed','SM_DAY03_Tongue':'Interior_BroadDorsalTongue'};saved={};metrics=[]
for name,source_name in source_names.items():
 ob=bpy.data.objects[source_name];ev=ob.evaluated_get(bpy.context.evaluated_depsgraph_get());me=bpy.data.meshes.new_from_object(ev,preserve_all_data_layers=True,depsgraph=bpy.context.evaluated_depsgraph_get());coords=np.empty(len(me.vertices)*3,np.float32);me.vertices.foreach_get('co',coords);coords=coords.reshape(-1,3);M=np.asarray(ev.matrix_world);coords=coords@M[:3,:3].T+M[:3,3];before=coords.copy();uv=np.empty(len(me.loops)*2,np.float32);me.uv_layers['UV0'].data.foreach_get('uv',uv)
 if name=='SM_DAY03_Interior':
  # The first front_count vertices are the actual inner concave surface; the
  # outer return/backwall remains untouched and keeps the closed volume.
  front_count=1+192*56;assert len(coords)==10946
  x=coords[:front_count,0];z=coords[:front_count,2];radius=np.sqrt((x/9.35)**2+((z-.18)/4.65)**2);edge=np.clip((.98-radius)/.15,0,1);edge=edge*edge*(3-2*edge)
  palate=np.clip((z-.3)/1.6,0,1);palate=palate*palate*(3-2*palate)
  vault=.28*np.exp(-(x/4.5)**4-((z-2.15)/1.25)**2)
  # Broad shallow folds are integrated into the recess, not floating tubes.
  folds=-(.13*np.exp(-((radius-.64)/.065)**2)+.10*np.exp(-((radius-.82)/.06)**2))*palate
  buccal=-.11*(np.exp(-((x-6.3)/1.0)**2)+np.exp(-((x+6.3)/1.0)**2))*np.exp(-((z+.10)/1.8)**2)
  floor=.14*np.exp(-(x/4.0)**4-((z+2.6)/.65)**2)
  coords[:front_count,1]+=edge*(vault+folds+buccal+floor)
 else:
  # Dorsal UV orientation is already stable: Uleft/right,Vfront/back. The
  # source upper backedge remains fixed; only inferred interior mass is raised.
  loops=np.empty(len(me.loops),np.int32);me.loops.foreach_get('vertex_index',loops);uv2=uv.reshape(-1,2);dorsal=np.zeros(len(coords),bool);s=np.zeros(len(coords));hits=np.zeros(len(coords))
  for poly in me.polygons:
   li=np.asarray(poly.loop_indices);values=uv2[li]
   if values[:,1].max()<.776:
    ids=loops[li];dorsal[ids]=True;np.add.at(s,ids,np.clip((values[:,1]-.035)/.72,0,1));np.add.at(hits,ids,1)
  s=np.divide(s,np.maximum(hits,1));x=coords[:,0]
  dome=.10*np.exp(-(x/4.8)**4)*np.sin(np.pi*s)**2
  groove=.045*np.exp(-(x/.44)**2)*np.sin(np.pi*s)**2
  # Preserve the authorized existing-character source foot zone.
  foot_hold=1-np.exp(-((x+5.757009)/.85)**2-((coords[:,1]-2.887758)/.80)**2)
  coords[dorsal,2]+=(dome-groove)[dorsal]*foot_hold[dorsal]
  # Roll and relax the closed perimeter. Preserve the measured rear silhouette
  # and deform both layers together; no thin front plate is introduced.
  e=np.empty(len(me.edges)*2,np.int32);me.edges.foreach_get('vertices',e);e=e.reshape(-1,2);src=np.r_[e[:,0],e[:,1]];dst=np.r_[e[:,1],e[:,0]];degree=np.maximum(1,np.bincount(src,minlength=len(coords)));sm=coords.copy()
  for j in range(36):
   avg=np.column_stack([np.bincount(src,weights=sm[dst,k],minlength=len(sm))/degree for k in range(3)]);sm+=(.45 if j%2==0 else-.47)*(avg-sm)
  blend=np.ones(len(coords));blend[dorsal]=np.clip((.88-s[dorsal])/.18,0,1);coords+=.75*blend[:,None]*(sm-coords)
 # Native reflection changes handedness; reverse winding and matching UVloops.
 coords[:,1]*=-1;me.vertices.foreach_set('co',coords.astype(np.float32).ravel());me.flip_normals();me.update()
 for face in me.polygons:face.use_smooth=True
 me.name=name+'_Surface';saved[name]=me
 metrics.append({'mesh':name,'parent_source_object':source_name,'vertices':len(me.vertices),'triangles':sum(len(f.vertices)-2 for f in me.polygons),'source_max_displacement':float(np.linalg.norm(coords*np.array([1,-1,1])-before,axis=1).max()),'bounds_cm':[(coords.min(0)*100).tolist(),(coords.max(0)*100).tolist()],'source_uv0_sha256':hashlib.sha256(uv.tobytes()).hexdigest(),'uv_policy':'Preserve complete uniqueV03per-materialUV0; mirroredsourceYflip reversesface/UVwinding together'})

for ob in list(bpy.data.objects):bpy.data.objects.remove(ob,do_unlink=True)
objects=[]
for name,me in saved.items():
 ob=bpy.data.objects.new(name,me);bpy.context.scene.collection.objects.link(ob);objects.append(ob)
bpy.context.scene.unit_settings.system='NONE';bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'MC_DAY03_InteriorSource.blend'))
report={'parent_source':str(SOURCE),'parent_source_sha256':source_sha,'source':str(OUT/'MC_DAY03_InteriorSource.blend'),'source_sha256':hashlib.sha256((OUT/'MC_DAY03_InteriorSource.blend').read_bytes()).hexdigest(),'status':'Boundedcavity/tonguecoherentgeometryreview beforefinalstaticexport','materials':['MC_Interior','MC_Tongue'],'parts':metrics,'origin_policy':'Eachstaticmesh identityorigin0/0/0; metertoUEcmFBXmetadata, no geometryx100; nativeYflip+winding','shape_policy':'V03root/contact/silhouette retained; shallowintegratedvault/buccal/palatalfoldvolume; centraldorsalmass/groove and rolledclosedperimeter. Noportal/uvula.','preview_fit':'Frozenv08outer+nativefacingcavity/tongue, onlypreviewrelativeX.90; sourceexportmeshunstretched. Rootownsactualcamera/cardclip/character.'}
(OUT/'interior_handoff.json').write_text(json.dumps(report,indent=2));assert hashlib.sha256(SOURCE.read_bytes()).hexdigest()==source_sha
if args.finalize:
 for ob in objects:
  bpy.ops.object.select_all(action='DESELECT');ob.select_set(True);bpy.context.view_layer.objects.active=ob
  bpy.ops.export_scene.fbx(filepath=str(OUT/(ob.name+'.fbx')),use_selection=True,object_types={'MESH'},use_mesh_modifiers=False,bake_anim=False,path_mode='STRIP',axis_forward='-Y',axis_up='Z',apply_unit_scale=False,apply_scale_options='FBX_SCALE_NONE',mesh_smooth_type='FACE',use_tspace=False)
 raise RuntimeError('CombinedPainterpacket/export follows leadcoherentinteriorreview')

# Actual frozen outer geometry in the same dimensional source frame.
OUTER=BASE/'Assembly/UnrealHandoff/v08b_SeamUV/Candidates/Final96_GumMinStretch_LocalFangRepair/MC_DAY03_UnrealMorphSource.blend'
with bpy.data.libraries.load(str(OUTER),link=False)as(src,dst):dst.objects=['SK_DAY03']
outer=dst.objects[0];outer.parent=None;outer.matrix_world=Matrix.Identity(4);bpy.context.scene.collection.objects.link(outer)
for mod in list(outer.modifiers):outer.modifiers.remove(mod)
for k in outer.data.shape_keys.key_blocks:k.value=0.
for ob in objects:ob.scale.x=.90
for ob in [outer]+objects:
 for i,m in enumerate(ob.data.materials):
  if not m.use_nodes:m.use_nodes=True
  m.use_backface_culling=True
  bs=m.node_tree.nodes.get('Principled BSDF');color=(.68,.72,.73,1)if m.name.startswith('MC_Enamel')else(.28,.36,.38,1)if m.name.startswith('MC_Gum')else(.43,.46,.48,1)if m.name.startswith('MC_Tongue')else(.16,.19,.21,1)
  for link in list(bs.inputs['Base Color'].links):m.node_tree.links.remove(link)
  bs.inputs['Base Color'].default_value=color;bs.inputs['Roughness'].default_value=.36;bs.inputs['Coat Weight'].default_value=0.;bs.inputs['Subsurface Weight'].default_value=0.
scene=bpy.context.scene;w=bpy.data.worlds.new('GeometryReview');scene.world=w;w.use_nodes=True;w.node_tree.nodes['Background'].inputs[0].default_value=(.02,.025,.03,1);w.node_tree.nodes['Background'].inputs[1].default_value=.45
for name,loc,power,size in [('Front',(0,12,8),1600,10),('Left',(-9,8,3),1000,8),('Right',(9,8,3),1000,8),('Rim',(0,-7,8),1500,8),('Cavity',(0,4,0),160,5)]:
 d=bpy.data.lights.new('GEO-Review'+name,'AREA');l=bpy.data.objects.new('GEO-Review'+name,d);scene.collection.objects.link(l);l.location=loc;l.rotation_euler=(Vector((0,-3,0))-l.location).to_track_quat('-Z','Y').to_euler();d.energy=power;d.shape='DISK';d.size=size
cd=bpy.data.cameras.new('GEO-ReviewCamera');cam=bpy.data.objects.new('GEO-ReviewCamera',cd);scene.collection.objects.link(cam);scene.camera=cam;scene.render.engine='BLENDER_EEVEE';scene.render.resolution_x=1600;scene.render.resolution_y=900;scene.render.resolution_percentage=100;scene.render.image_settings.file_format='PNG';scene.view_settings.view_transform='AgX'
for name,loc in [('composite_open_front',(0,35,0)),('composite_open_oblique',(12,30,7)),('interior_only_front',(0,35,0)),('interior_only_oblique',(12,30,7))]:
 outer.hide_render='interior_only'in name;cam.location=loc;cam.rotation_euler=(Vector((0,-1,0))-cam.location).to_track_quat('-Z','Y').to_euler();cd.type='ORTHO'if'front'in name else'PERSP';cd.ortho_scale=19.5;cd.lens=49.5;scene.render.filepath=str(OUT/(name+'.png'));bpy.ops.render.render(write_still=True)
print('INTERIOR_FORM_PREVIEW',json.dumps({'out':str(OUT),'source_sha256':report['source_sha256'],'native_origin':'identity','fbx_exported':args.finalize}))
