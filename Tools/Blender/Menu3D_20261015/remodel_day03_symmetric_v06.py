"""Reference B DAY03 geometry repair: 8+8 mirrored crowns and real scalloped gums.

Isolated headless only. Never mutates v05 or a GUI scene. Native FBX export is a
separate --export pass after fixed-camera geometry review. Source units remain
meters; existing +Y native-facing convention and one-root morph contract remain.
"""
import bpy, bmesh, numpy as np, json, math, hashlib, argparse, sys
from pathlib import Path
from mathutils import Vector

assert bpy.app.background
P=argparse.ArgumentParser();P.add_argument('--export',action='store_true');P.add_argument('--render-all',action='store_true');a=P.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
BASE=Path('E:/DEVGAME/MessControl/ArtSource/UI/MainMenu3D/20261015_refB_v1')
SRC=BASE/'Assembly/UnrealHandoff/v05_GumWidthRepair'
OUT=BASE/'Assembly/UnrealHandoff/v06_SymmetricRemodel';OUT.mkdir(parents=True,exist_ok=True)
meta=json.loads((SRC/'unreal_mouth_handoff.json').read_text())
bpy.ops.wm.open_mainfile(filepath=str(SRC/'MC_DAY03_UnrealMorphSource.blend'))
old=bpy.data.objects['SK_DAY03'];me=old.data
co=np.empty(len(me.vertices)*3,np.float32);me.shape_keys.key_blocks['Basis'].data.foreach_get('co',co);co=co.reshape(-1,3)
uv=np.empty(len(me.loops)*2,np.float32);me.uv_layers['UV0'].data.foreach_get('uv',uv);uv=uv.reshape(-1,2)
loopverts=np.empty(len(me.loops),np.int32);me.loops.foreach_get('vertex_index',loopverts)
starts=np.empty(len(me.polygons),np.int32);counts=np.empty(len(me.polygons),np.int32);me.polygons.foreach_get('loop_start',starts);me.polygons.foreach_get('loop_total',counts)
parts={p['mesh']:p for p in meta['parts']}

def source_part(name):
 p=parts['MC_DAY03_'+name];st=p['vertex_start'];n=p['vertex_count'];f=[];u=[]
 for s,c in zip(starts,counts):
  ids=loopverts[s:s+c]
  if st<=ids[0]<st+n:
   assert np.all((ids>=st)&(ids<st+n));f.append((ids-st).tolist());u.append(uv[s:s+c].copy())
 return co[st:st+n].astype(np.float64).copy(),f,u

allcoords=[];allfaces=[];alluv=[];mats=[];records=[];pairs=[];offset=0
def add(name,c,f,u,slot,pair=None,source=None):
 global offset
 st=offset;allcoords.append(c.astype(np.float32));allfaces.extend([[int(i)+st for i in row] for row in f]);alluv.extend(u);mats.extend([slot]*len(f));offset+=len(c)
 records.append({'mesh':name,'vertex_start':st,'vertex_count':len(c),'material_slot':slot,'source_profile':source,'mirror_partner':pair})
 return len(records)-1

# Anatomical arc from front to real posterior depth (native-facing +Y).
# Place successive crowns by physical arc length, rather than shrinking X.
xs=np.linspace(0,8.7,12001);ys=-3.25*(xs/8.4)**3.1;arc=np.r_[0,np.cumsum(np.sqrt(np.diff(xs)**2+np.diff(ys)**2))]
def ay(x):return -3.25*(abs(x)/8.4)**3.1
def slope(x):return -3.25*3.1/8.4*(max(abs(x),1e-9)/8.4)**2.1
def smooth_root(x,points):
 xx=np.array([q[0] for q in points]);yy=np.array([q[1] for q in points]);h=np.diff(xx);d=np.diff(yy)/h;m=np.r_[d[0],np.zeros(len(xx)-2),d[-1]]
 for k in range(1,len(xx)-1):
  if d[k-1]*d[k]>0:
   w1=2*h[k]+h[k-1];w2=h[k]+2*h[k-1];m[k]=(w1+w2)/(w1/d[k-1]+w2/d[k])
 if x<=xx[0]:return float(yy[0])
 if x>=xx[-1]:return float(yy[-1])
 k=int(np.searchsorted(xx,x)-1);t=(x-xx[k])/h[k]
 return float((2*t**3-3*t*t+1)*yy[k]+(t**3-2*t*t+t)*h[k]*m[k]+(-2*t**3+3*t*t)*yy[k+1]+(t**3-t*t)*h[k]*m[k+1])
upper=[('Central','Upper_Central_L',1.55),('Lateral','Upper_Lateral_L',1.20),('Fang','Upper_Fang_L',1.30),('Premolar01','Upper_Posterior_L04',1.20),('Premolar02','Upper_Posterior_L03',1.05),('Molar01','Upper_Posterior_L02',.95),('Molar02','Upper_Posterior_L01',.88),('Molar03','Upper_Posterior_L01',.80)]
upper_centers=[];distance=.016
for kind,source,width in upper:
 c,f,u=source_part(source)
 # Reflect accepted left profile into canonical positive-X hemiarch.
 c[:,0]*=-1.;f=[q[::-1] for q in f];u=[q[::-1].copy() for q in u]
 lo=c.min(0);hi=c.max(0);scale=width/(hi[0]-lo[0]);c-=np.array([(hi[0]+lo[0])/2,(hi[1]+lo[1])/2,hi[2]])
 c*=scale
 distance+=width/2;cx=float(np.interp(distance,arc,xs));distance+=width/2+.016
 rootz=3.14-.13*(cx/8.4)**2
 theta=math.atan(slope(cx));cs,sn=math.cos(theta),math.sin(theta)
 c[:,:2]=c[:,:2]@np.array([[cs,sn],[-sn,cs]])
 c+=np.array([cx,ay(cx),rootz]);upper_centers.append((cx,rootz))
 names=['MC_DAY03_Upper_'+kind+'_R','MC_DAY03_Upper_'+kind+'_L'];ri=add(names[0],c,f,u,0,names[1],source)
 mirror=c.copy();mirror[:,0]*=-1;li=add(names[1],mirror,[q[::-1] for q in f],[q[::-1].copy() for q in u],0,names[0],source)
 pairs.append({'right':names[0],'left':names[1],'right_part':ri,'left_part':li,'row':'Upper','kind':kind})

lower_centers=[]
for kind in ['Central','Lateral','Canine','Premolar','Molar','Posterior_01','Posterior_02','Posterior_03']:
 source='Lower_'+(kind.replace('Posterior_','Posterior_L') if kind.startswith('Posterior_') else kind+'_L')
 c,f,u=source_part(source);c[:,0]*=-1;f=[q[::-1] for q in f];u=[q[::-1].copy() for q in u]
 # Existing lower has correct centered eight-pair spacing; keep accepted shape.
 cx=float((c[:,0].min()+c[:,0].max())*.5);rootz=float(c[:,2].min());lower_centers.append((cx,rootz))
 names=['MC_DAY03_Lower_'+kind+'_R','MC_DAY03_Lower_'+kind+'_L'];ri=add(names[0],c,f,u,0,names[1],source)
 mirror=c.copy();mirror[:,0]*=-1;li=add(names[1],mirror,[q[::-1] for q in f],[q[::-1].copy() for q in u],0,names[0],source)
 pairs.append({'right':names[0],'left':names[1],'right_part':ri,'left_part':li,'row':'Lower','kind':kind})

def gum(lower):
 # Closed tube loft: scalloped cervical margin follows eight paired tooth roots.
 # No broad rectangular planar ribbon; terminal rings round into capped ends.
 centers=sorted(lower_centers if lower else upper_centers)
 X=8.32 if lower else 8.46;sections=240;radial=40;c=[];f=[];u=[]
 for i in range(sections+1):
  x=-X+2*X*i/sections;ax=abs(x)
  root=smooth_root(ax,centers)
  nearest=min(abs(ax-q[0]) for q in centers)
  scallop=.17*math.exp(-(nearest/.29)**2)
  # Upper marginal edge at root-.28..-.45; lower mirrored around root.
  margin=root+(.30-scallop if lower else -.30+scallop)
  cap=math.sqrt(max(0.,1-((ax-(X-.40))/.40)**2)) if ax>X-.40 else 1.
  cap=max(.015,cap);ry=.61*cap
  # Cervical edge is scalloped; outer crest stays smooth across the arch.
  outer=(-4.90+1.80*(ax/X)**2.8) if lower else (4.14-.13*(ax/X)**2)
  zc=(margin+outer)/2;rz=abs(outer-margin)/2*cap
  # Preserve lower's deeper U-shaped arch and seated posterior roots.
  yc=-(.03+3.05*(ax/8.15)**2.9) if lower else ay(x)
  for j in range(radial):
   t=2*math.pi*j/radial
   c.append((x,yc+ry*math.cos(t),zc+rz*math.sin(t)))
 for i in range(sections):
  for j in range(radial):
   k=(j+1)%radial;f.append([i*radial+j,(i+1)*radial+j,(i+1)*radial+k,i*radial+k]);u.append(np.array([[i/sections,j/radial],[(i+1)/sections,j/radial],[(i+1)/sections,(j+1)/radial],[i/sections,(j+1)/radial]]))
 # End caps use a center fan, nondegenerate and valid UVs.
 for i in (0,sections):
  center=len(c);c.append(np.mean(np.asarray(c)[i*radial:(i+1)*radial],0))
  for j in range(radial):
   k=(j+1)%radial;f.append([center,i*radial+k,i*radial+j] if i==0 else [center,i*radial+j,i*radial+k]);u.append(np.array([[.5,.5],[.5+.5*math.cos(2*math.pi*k/radial),.5+.5*math.sin(2*math.pi*k/radial)],[.5+.5*math.cos(2*math.pi*j/radial),.5+.5*math.sin(2*math.pi*j/radial)]]))
 return np.asarray(c),f,u

for lower in (False,True):
 c,f,u=gum(lower);add('MC_DAY03_'+('Lower' if lower else 'Upper')+'_Gum',c,f,u,1,source='v06 closed scalloped arch loft')

basis=np.concatenate(allcoords);allposes={}
def pose(amount):
 out=basis.astype(np.float64).copy();closed=1.-amount
 for p in records:
  s=slice(p['vertex_start'],p['vertex_start']+p['vertex_count']);lower='_Lower_' in p['mesh'];c=out[s]
  pivot=np.array([0.,-3.,-3.6 if lower else 3.])
  angle=math.radians((8. if lower else -6.)*closed);cs,sn=math.cos(angle),math.sin(angle)
  c-=pivot;c[:,1:3]=c[:,1:3]@np.array([[cs,sn],[-sn,cs]]);c+=pivot
  c[:,2]+=(2.10 if lower else -1.54)*closed
  c[:,1]+=(-.55 if lower else .05)*closed
  out[s]=c
 return out.astype(np.float32)
for amount in (1.,0.,.25,.5,.75):allposes[amount]=pose(amount)

bpy.ops.wm.read_factory_settings(use_empty=True)
mesh=bpy.data.meshes.new('MC_DAY03_SymmetricSurface');mesh.from_pydata(basis.tolist(),[],allfaces);mesh.update()
ob=bpy.data.objects.new('SK_DAY03',mesh);bpy.context.scene.collection.objects.link(ob)
for name in ['MC_Enamel','MC_Gum']:mesh.materials.append(bpy.data.materials.new(name))
for p,m in zip(mesh.polygons,mats):p.material_index=m;p.use_smooth=True
layer=mesh.uv_layers.new(name='UV0');layer.data.foreach_set('uv',np.concatenate(alluv).astype(np.float32).ravel())
bm=bmesh.new();bm.from_mesh(mesh);bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces));bm.to_mesh(mesh);bm.free();mesh.update()
ob.shape_key_add(name='Basis')
for amount,name in [(0.,'Pose_000'),(.25,'Pose_025'),(.5,'Pose_050'),(.75,'Pose_075')]:
 k=ob.shape_key_add(name=name);k.data.foreach_set('co',allposes[amount].ravel())
arm=bpy.data.armatures.new('MC_MenuMouth_Skeleton');rig=bpy.data.objects.new('MC_MenuMouth_Rig',arm);bpy.context.scene.collection.objects.link(rig);bpy.context.view_layer.objects.active=rig;rig.select_set(True);bpy.ops.object.mode_set(mode='EDIT');bone=arm.edit_bones.new('root');bone.head=(0,0,0);bone.tail=(0,0,100);bpy.ops.object.mode_set(mode='OBJECT')
g=ob.vertex_groups.new(name='root');g.add(list(range(len(mesh.vertices))),1.,'REPLACE');mod=ob.modifiers.new('RootSkin','ARMATURE');mod.object=rig;ob.parent=rig
bpy.context.scene.unit_settings.system='NONE'
np.savez_compressed(OUT/'source_world_poses_cm.npz',**{str(k):v*100 for k,v in allposes.items()})
report={'status':'geometry review before Unreal import','source':str(SRC/'MC_DAY03_UnrealMorphSource.blend'),'source_sha256':hashlib.sha256((SRC/'MC_DAY03_UnrealMorphSource.blend').read_bytes()).hexdigest(),'source_unit_to_cm':100.,'fbx_geometry_scale':1.,'native_y_flip':True,'vertices':len(mesh.vertices),'polygons':len(mesh.polygons),'triangles':sum(len(p.vertices)-2 for p in mesh.polygons),'materials':['MC_Enamel','MC_Gum'],'primary_uv':'UV0','parts':records,'mirror_pairs':pairs,'morphs':['Pose_000','Pose_025','Pose_050','Pose_075'],'control':'w_i=max(0,1-abs(OpenAmount-sample_i)/.25); Open is Basis','interior':'Reuse v04 separately; root importer reviews fit','bounds_open_cm':[(basis.min(0)*100).tolist(),(basis.max(0)*100).tolist()],'fbx_length_policy':'Blender unit meter, metadata converts to native cm; do not pre-multiply geometry by100.','symmetry_checks':{},'gum_symmetry_checks':{},'fang_landmarks':{},'centering_checks':{},'preview_camera':{'front':'Orthographic width19.5units','diagnostic_perspective':'Source distance30units FOV39.97degrees. Not a nativecard/cavityruntimecomposition claim.','native_context_validation':'Root owns actualUnreal9sampleclip/interior/tongue review.'},'construction':'Rigid mirrored source crown profiles,32crowns8pairs/row; upper2premolar3molarhierarchy. Closed scalloped gumtube lofts, realUarchdepth, shared rigidjaw5poses.'}
for amount,arr in allposes.items():
 checks=[]
 for pair in pairs:
  r=records[pair['right_part']];l=records[pair['left_part']];rc=arr[r['vertex_start']:r['vertex_start']+r['vertex_count']].copy();lc=arr[l['vertex_start']:l['vertex_start']+l['vertex_count']];rc[:,0]*=-1;checks.append({'pair':pair['kind'],'row':pair['row'],'max_corresponding_vertex_error':float(np.linalg.norm(rc-lc,axis=1).max())})
 report['symmetry_checks'][str(amount)]=checks
 report['gum_symmetry_checks'][str(amount)]={}
 for p in [q for q in records if q['material_slot']==1]:
  g=arr[p['vertex_start']:p['vertex_start']+p['vertex_count']];mapping=np.r_[np.arange(241*40).reshape(241,40)[::-1].ravel(),241*40+1,241*40];mirror=g.copy();mirror[:,0]*=-1
  error=float(np.linalg.norm(mirror-g[mapping],axis=1).max());assert error<1e-6
  report['gum_symmetry_checks'][str(amount)][p['mesh']]={'max_paired_ring_vertex_error':error,'symmetry_index_policy':'section i mirrored to240-i, same radialj; endcaps exchanged'}
 report['fang_landmarks'][str(amount)]={}
 for p in [q for q in records if '_Fang_' in q['mesh']]:
  g=arr[p['vertex_start']:p['vertex_start']+p['vertex_count']];report['fang_landmarks'][str(amount)][p['mesh']]={'tip_xyz':g[np.argmin(g[:,2])].tolist(),'root_max_z':float(g[:,2].max())}
 report['centering_checks'][str(amount)]={}
 for row in ('Upper','Lower'):
  p=next(q for q in records if q['mesh']=='MC_DAY03_'+row+'_Central_R');r=arr[p['vertex_start']:p['vertex_start']+p['vertex_count']];report['centering_checks'][str(amount)][row]={'right_central_min_x':float(r[:,0].min()),'center_seam_gap':float(2*r[:,0].min()),'no_center_crown':bool(r[:,0].min()>0.)}
 assert max(q['max_corresponding_vertex_error'] for q in checks)<1e-6
assert all(v[row]['no_center_crown'] for v in report['centering_checks'].values() for row in ('Upper','Lower'))
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'MC_DAY03_UnrealMorphSource.blend'))
# Current artifact identity and input lineage are distinct evidence fields.
report['parent_source']=report['source'];report['parent_source_sha256']=report['source_sha256']
report['source']=str(OUT/'MC_DAY03_UnrealMorphSource.blend')
report['source_sha256']=hashlib.sha256((OUT/'MC_DAY03_UnrealMorphSource.blend').read_bytes()).hexdigest()
(OUT/'unreal_mouth_handoff.json').write_text(json.dumps(report,indent=2))
if a.export:
 bpy.ops.object.select_all(action='DESELECT');ob.select_set(True);rig.select_set(True);bpy.context.view_layer.objects.active=ob
 bpy.ops.export_scene.fbx(filepath=str(OUT/'SK_DAY03.fbx'),use_selection=True,object_types={'MESH','ARMATURE'},use_mesh_modifiers=False,add_leaf_bones=False,use_armature_deform_only=True,bake_anim=False,path_mode='STRIP',axis_forward='-Y',axis_up='Z',apply_unit_scale=False,apply_scale_options='FBX_SCALE_NONE',mesh_smooth_type='FACE',use_tspace=False)

# Symmetric neutral illumination: diagnosis ofgeometry, notfinalPBR.
scene=bpy.context.scene
for i,m in enumerate(mesh.materials):
 m.use_nodes=True;bs=m.node_tree.nodes.get('Principled BSDF');bs.inputs['Base Color'].default_value=(.68,.72,.73,1) if i==0 else (.28,.36,.38,1);bs.inputs['Roughness'].default_value=.38
w=bpy.data.worlds.new('GeometryReview');scene.world=w;w.use_nodes=True;w.node_tree.nodes['Background'].inputs[0].default_value=(.02,.025,.03,1);w.node_tree.nodes['Background'].inputs[1].default_value=.55
for name,loc,power,size in [('Front',(0,12,8),1600,10),('Left',(-9,8,3),1000,8),('Right',(9,8,3),1000,8),('Rim',(0,-7,8),1800,8)]:
 d=bpy.data.lights.new('GEO-Review'+name,'AREA');l=bpy.data.objects.new('GEO-Review'+name,d);scene.collection.objects.link(l);l.location=loc;l.rotation_euler=(-l.location).to_track_quat('-Z','Y').to_euler();d.energy=power;d.shape='DISK';d.size=size
cd=bpy.data.cameras.new('GEO-ReviewCamera');cam=bpy.data.objects.new('GEO-ReviewCamera',cd);scene.collection.objects.link(cam);scene.camera=cam
scene.render.engine='BLENDER_EEVEE';scene.render.resolution_x=1280;scene.render.resolution_y=720;scene.render.resolution_percentage=100;scene.render.image_settings.file_format='PNG';scene.view_settings.view_transform='AgX';scene.render.film_transparent=False
shots=[('front_closed',0.,(0,35,0),'ORTHO'),('front_open',1.,(0,35,0),'ORTHO')]
if a.render_all:shots += [('native_'+str(k).replace('.',''),k,(0,30,0),'PERSP') for k in (0.,.125,.25,.375,.5,.625,.75,.875,1.)]+[('oblique_closed',0.,(12,30,7),'PERSP'),('oblique_open',1.,(12,30,7),'PERSP')]
for name,amount,loc,typ in shots:
 samples={'Pose_000':0.,'Pose_025':.25,'Pose_050':.5,'Pose_075':.75}
 for k in ob.data.shape_keys.key_blocks:k.value=max(0.,1.-abs(amount-samples[k.name])/.25) if k.name in samples else 0.
 cam.location=loc;cam.rotation_euler=(-cam.location).to_track_quat('-Z','Y').to_euler();cd.type=typ;cd.ortho_scale=19.5;cd.lens=49.5 if typ=='PERSP' else 50
 scene.render.filepath=str(OUT/(name+'.png'));bpy.ops.render.render(write_still=True)
print('SYMMETRIC_REMODEL_READY',json.dumps({'out':str(OUT),'parts':len(records),'triangles':report['triangles'],'bounds':report['bounds_open_cm'],'fbx_exported':a.export}))
