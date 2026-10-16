"""DAY03 reference finish: restrained symmetric enamel/gingiva refinement + UVs.

Accepted v07 jaw paths and 32 crowns remain. Preview is a shape review milestone;
--finalize adds unique per-material Painter UV0 and native/static exports after
the lead's geometry review. Never opens a GUI or mutates accepted v07.
"""
import bpy,numpy as np,json,hashlib,math,argparse,sys
from pathlib import Path
from mathutils import Vector
assert bpy.app.background
parser=argparse.ArgumentParser();parser.add_argument('--finalize',action='store_true');opt=parser.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
BASE=Path('E:/DEVGAME/MessControl/ArtSource/UI/MainMenu3D/20261015_refB_v1/Assembly/UnrealHandoff')
SRC=BASE/'v07_LowerRowReadability';OUT=BASE/'v08_ReferenceFinish';OUT.mkdir(parents=True,exist_ok=True)
source=SRC/'MC_DAY03_UnrealMorphSource.blend';source_sha=hashlib.sha256(source.read_bytes()).hexdigest()
assert source_sha=='4144fae22714fee23b4b29c25e41b556d10b055df70519766c077afab3665062'
meta=json.loads((SRC/'unreal_mouth_handoff.json').read_text());parts=meta['parts'];pairs=meta['mirror_pairs']
bpy.ops.wm.open_mainfile(filepath=str(source));o=bpy.data.objects['SK_DAY03'];me=o.data;keys=me.shape_keys.key_blocks;n=len(me.vertices)
old={}
for k in keys:
 a=np.empty(n*3,np.float32);k.data.foreach_get('co',a);old[k.name]=a.reshape(-1,3)
edges=np.empty(len(me.edges)*2,np.int32);me.edges.foreach_get('vertices',edges);edges=edges.reshape(-1,2)
basis=old['Basis'].astype(np.float64).copy();delta=np.zeros_like(basis);polish=[]
def smooth(c,e,iterations):
 v=c.copy();src=np.r_[e[:,0],e[:,1]];dst=np.r_[e[:,1],e[:,0]];degree=np.bincount(src,minlength=len(v));degree=np.maximum(1,degree)
 for j in range(iterations):
  avg=np.column_stack([np.bincount(src,weights=v[dst,k],minlength=len(v))/degree for k in range(3)])
  v+=(.48 if j%2==0 else -.50)*(avg-v)
 return v
def clean_outline(c,exponent=3.4,blend=.72):
 lo=c.min(0);hi=c.max(0);center=(lo+hi)/2;half=np.maximum((hi-lo)/2,1e-8);xz=(c[:,[0,2]]-center[[0,2]])/half[[0,2]];theta=np.arctan2(xz[:,1],xz[:,0]);rad=np.linalg.norm(xz,axis=1)
 # Angular bins of interior cap points are not a boundary and create radial
 # wrinkles. Use an actual convex silhouette hull and ray/edge intersections.
 points=sorted(set(map(tuple,xz.tolist())))
 def cross(a,b,c):return (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0])
 lower=[]
 for p in points:
  while len(lower)>=2 and cross(lower[-2],lower[-1],p)<=0:lower.pop()
  lower.append(p)
 upper=[]
 for p in reversed(points):
  while len(upper)>=2 and cross(upper[-2],upper[-1],p)<=0:upper.pop()
  upper.append(p)
 hull=np.array(lower[:-1]+upper[:-1]);edge=np.roll(hull,-1,axis=0)-hull;normal=np.column_stack([edge[:,1],-edge[:,0]]);plane=np.sum(normal*hull,axis=1)
 direction=np.column_stack([np.cos(theta),np.sin(theta)]);den=direction@normal.T
 actual=np.min(np.where(den>1e-10,plane[None,:]/np.maximum(den,1e-10),np.inf),axis=1)
 target=(np.abs(np.cos(theta))**exponent+np.abs(np.sin(theta))**exponent)**(-1/exponent)
 fraction=np.clip(rad/np.maximum(actual,1e-8),0,1)
 # Saturating radial remap heals concave incisal notches instead of carrying
 # them through a convex-hull scale. Continuous derivative at the perimeter
 # gives smooth rounded shoulders under specular lighting.
 desired=target*np.sin(np.pi*.5*fraction)
 factor=1+blend*(desired/np.maximum(rad,1e-8)-1)
 out=c.copy();out[:,[0,2]]=center[[0,2]]+xz*factor[:,None]*half[[0,2]]
 return out

for pair in pairs:
 r=parts[pair['right_part']];l=parts[pair['left_part']];st=r['vertex_start'];en=st+r['vertex_count'];c=basis[st:en].copy();ed=edges[(edges[:,0]>=st)&(edges[:,0]<en)]-st
 assert np.all((ed>=0)&(ed<len(c)))
 iterations=160 if pair['kind']!='Fang' else 120
 v=smooth(c,ed,iterations)
 lo=c.min(0);hi=c.max(0);center=(lo+hi)/2;size=hi-lo;lower=pair['row']=='Lower'
 root=lo[2] if lower else hi[2];tip=hi[2] if lower else lo[2];t=np.clip((c[:,2]-root)/(tip-root),0,1)
 analytic=pair['kind'] in ('Central','Lateral') or (lower and pair['kind']=='Canine')
 if analytic:v=clean_outline(v,3.2 if lower else 3.5,1.)
 # Preserve incisal contact landmarks and the center gap; remove high-frequency
 # outline chatter while maintaining the accepted crown dimensions/positions.
 if analytic:v[:,2]+=tip-(v[:,2].max() if lower else v[:,2].min())
 else:v[:,2]=c[:,2]+(v[:,2]-c[:,2])*np.minimum(1,np.minimum(t,1-t)/.075)
 # Cervical taper is a local crown shape refinement, never whole-mouth Xstretch.
 x=v[:,0]-center[0];v[:,0]=center[0]+x*(1-.028*(1-t)**4)
 front=np.clip((c[:,1]-center[1])/(size[1]*.28),0,1)
 lateral=np.clip(1-((c[:,0]-center[0])/(size[0]*.54))**2,0,1)
 facial=(.075 if pair['kind']!='Fang' else .055)*size[0]*lateral**.8*np.sin(np.pi*t)**.7*front
 v[:,1]+=facial
 # Root-approved lower front shortening preserves the existing incisal edge and
 # native opening path. Cervical tissue below is reseated, not left floating.
 if lower and pair['kind'] in ('Central','Lateral'):v[:,2]=tip+(v[:,2]-tip)*.85
 if pair['kind']=='Central':v[:,0]+=lo[0]-v[:,0].min()
 if analytic:
  # A clean outline alone retains diagonal undulations in the inherited dense
  # cap grid. Restate its facial/lingual mass as one smooth analytic surface.
  vlo=v.min(0);vhi=v.max(0);vc=(vlo+vhi)/2;vh=np.maximum((vhi-vlo)/2,1e-8);exponent=3.2 if lower else 3.5
  field=np.clip(np.abs((v[:,0]-vc[0])/vh[0])**exponent+np.abs((v[:,2]-vc[2])/vh[2])**exponent,0,1)
  depth=(.20+.80*np.sqrt(1-field))*size[1]*.5
  yn=(c[:,1]-center[1])/(size[1]*.12);wall=np.clip(np.abs(yn),0,1);wall=wall*wall*(3-2*wall)
  target_y=center[1]+np.sign(yn)*depth*wall
  strength=np.clip(t/.12,0,1);strength=strength*strength*(3-2*strength)
  v[:,1]=v[:,1]*(1-strength)+target_y*strength
 delta[st:en]=v-c
 lst=l['vertex_start'];len_=lst+l['vertex_count'];mirror=v.copy();mirror[:,0]*=-1.;delta[lst:len_]=mirror-basis[lst:len_]
 polish.append({'part':r['mesh'],'paired_part':l['mesh'],'iterations':iterations,'max_delta':float(np.linalg.norm(v-c,axis=1).max()),'facial_bulge_max':float(facial.max()),'lower_cervical_height_factor':.85 if lower and pair['kind'] in ('Central','Lateral') else 1.,'root_tip_axis':'Root atV1, tip atV0 anatomicalmask; lower+Ztip,upper-Ztip'})

# Gingival facial volume varies with root seats. Keep cervical X/Z coordinates
# and all accepted tooth exposure; material microdetail belongs to Painter.
for p in [q for q in parts if q['material_slot']==1]:
 st=p['vertex_start'];en=st+p['vertex_count'];c=basis[st:en];lower='_Lower_' in p['mesh'];crest=[]
 for tooth in [q for q in parts if q['material_slot']==0 and ('_Lower_' in q['mesh'])==lower and q['mesh'].endswith('_R')]:
  tc=basis[tooth['vertex_start']:tooth['vertex_start']+tooth['vertex_count']];crest.append(((tc[:,0].min()+tc[:,0].max())/2,tc[:,2].min() if lower else tc[:,2].max()))
 crest.sort();ax=np.abs(c[:,0]);root=np.interp(ax,[q[0]for q in crest],[q[1]for q in crest]);centerline=np.column_stack([np.zeros(len(c)),np.interp(ax,np.linspace(0,8.46,1000),-3.25*(np.linspace(0,8.46,1000)/8.4)**3.1),np.zeros(len(c))])[:,1]
 if lower:centerline=-(.03+3.05*(ax/8.15)**2.9)
 facial=np.clip((c[:,1]-centerline)/.52,0,1)
 neck=np.exp(-((c[:,2]-root)/.42)**2)
 root_distance=np.min(np.abs(ax[:,None]-np.array([q[0]for q in crest])[None,:]),axis=1)
 seat=.055+.035*np.exp(-(root_distance/.35)**2)
 delta[st:en,1]=facial*neck*seat
 # Deliberate cervical root eminences extend into the attached gingiva, while
 # interdental papillae approach the crowns between roots. This is broad
 # anatomical volume, not random displacement or painted contour shading.
 side=-1. if lower else 1.
 eminence=.14*np.exp(-(root_distance/.38)**2)*np.exp(-((c[:,2]-(root+side*.35))/.64)**2)
 delta[st:en,1]+=facial*eminence
 papilla=(1-np.exp(-(root_distance/.42)**2))*np.exp(-((c[:,2]-root)/.42)**2)
 delta[st:en,2]+=-side*.055*papilla
 if lower:
  weight=np.where(ax<=3.,1.,np.where(ax<3.65,.5+.5*np.cos(np.pi*np.clip((ax-3.)/.65,0,1)),0.))
  delta[st:en,2]+=.216*weight*np.exp(-((c[:,2]-root)/.72)**2)

# Rigid best-fit rows transport the local form refinements into every existing
# pose. v07 Open lift and Closed bite therefore remain in the same motion path.
new={};transforms={}
for name,arr in old.items():
 out=arr.astype(np.float64).copy();transforms[name]={}
 for row in ('Upper','Lower'):
  ps=[p for p in parts if '_'+row+'_' in p['mesh']];ids=np.concatenate([np.arange(p['vertex_start'],p['vertex_start']+p['vertex_count'])for p in ps]);b=basis[ids];a=arr[ids].astype(np.float64);bc=b.mean(0);ac=a.mean(0);u,s,vt=np.linalg.svd((b-bc).T@(a-ac));R=u@vt
  assert np.linalg.det(R)>.99999
  error=np.linalg.norm((b-bc)@R+ac-a,axis=1).max();assert error<2e-5,(name,row,error)
  out[ids]+=delta[ids]@R;transforms[name][row]={'rigid_fit_max_error':float(error),'rotation':R.tolist(),'centroid_basis':bc.tolist(),'centroid_pose':ac.tolist()}
 new[name]=out.astype(np.float32);keys[name].data.foreach_set('co',new[name].ravel());keys[name].value=0.
me.vertices.foreach_set('co',new['Basis'].ravel());me.update()

# Whole-pair proof, including both solid gums; sourceparts remain unchanged.
for name,arr in new.items():
 for pair in pairs:
  r=parts[pair['right_part']];l=parts[pair['left_part']];rc=arr[r['vertex_start']:r['vertex_start']+r['vertex_count']].copy();lc=arr[l['vertex_start']:l['vertex_start']+l['vertex_count']];rc[:,0]*=-1.;assert np.linalg.norm(rc-lc,axis=1).max()<1e-6

meta.update({'parent_source':str(source),'parent_source_sha256':source_sha,'source':str(OUT/'MC_DAY03_UnrealMorphSource.blend'),'source_sha256':None,'status':'v08 coherent form preview; final UV/export pending','reference_finish':{'stage':'primary/secondary surface refinement beforePainterUV','scope':'Paired crown outline polish, restrained facial convexity/cervical taper, gingival root-seat volume; acceptedv07jawpath retained','polish':polish,'row_transforms':transforms},'bounds_each_pose_cm':{name:{'min':(v.min(0)*100).tolist(),'max':(v.max(0)*100).tolist()}for name,v in new.items()},'bounds_open_cm':[(new['Basis'].min(0)*100).tolist(),(new['Basis'].max(0)*100).tolist()]})
np.savez_compressed(OUT/'source_world_poses_cm.npz',**{str({'Basis':1.,'Pose_000':0.,'Pose_025':.25,'Pose_050':.5,'Pose_075':.75}[k]):v*100 for k,v in new.items()})

uv_helpers=None
if opt.finalize:
 import runpy
 uv_helpers=runpy.run_path(str(Path(__file__).with_name('day03_painter_uv_v08.py')))
 uv_report=uv_helpers['finish_uv'](o,parts,OUT);me=o.data;keys=me.shape_keys.key_blocks
 for k in keys:
  a=np.empty(n*3,np.float32);k.data.foreach_get('co',a);assert np.array_equal(a.reshape(-1,3),new[k.name]),'UV authoring changedgeometry'
 meta['status']='v08 refinedgeometry and uniquePainterUV handoff; independentQA/nativecomparison pending'
 meta['reference_finish']['stage']='Unique per-materialUV0/nativeFBX/PainterstaticOpenOBJ'
 meta['painter_uv_handoff']='painter_uv_handoff.json';meta['primary_uv']='UV0'

bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'MC_DAY03_UnrealMorphSource.blend'));meta['source_sha256']=hashlib.sha256((OUT/'MC_DAY03_UnrealMorphSource.blend').read_bytes()).hexdigest();(OUT/'unreal_mouth_handoff.json').write_text(json.dumps(meta,indent=2))
assert hashlib.sha256(source.read_bytes()).hexdigest()==source_sha
if opt.finalize:
 bpy.ops.object.select_all(action='DESELECT');o.select_set(True);rig=bpy.data.objects['MC_MenuMouth_Rig'];rig.select_set(True);bpy.context.view_layer.objects.active=o
 bpy.ops.export_scene.fbx(filepath=str(OUT/'SK_DAY03.fbx'),use_selection=True,object_types={'MESH','ARMATURE'},use_mesh_modifiers=False,add_leaf_bones=False,use_armature_deform_only=True,bake_anim=False,path_mode='STRIP',axis_forward='-Y',axis_up='Z',apply_unit_scale=False,apply_scale_options='FBX_SCALE_NONE',mesh_smooth_type='FACE',use_tspace=False)
 uv_helpers['export_static_obj'](o,parts,OUT)
 names=['MC_DAY03_UnrealMorphSource.blend','SK_DAY03.fbx','MC_DAY03_Painter_Open.obj','source_world_poses_cm.npz','texture_mesh_packet.npz','unreal_mouth_handoff.json','painter_uv_handoff.json']
 receipt={'parent_source':str(source),'parent_source_sha256':source_sha,'files':{name:{'bytes':(OUT/name).stat().st_size,'sha256':hashlib.sha256((OUT/name).read_bytes()).hexdigest()}for name in names},'scope':'v07rigidjawpaths retained; shape refinements +15% lowerfrontcervicalshortening/rootreseat + uniqueUV0','source_geometry_symmetry':'Exactpairedcrownvertices across5samples; independent9sample/sourceFBX/gums/nativechecks pending','validation':'Leadgeometryreview beforefinalize; producerinvariants. Independenttechnical/Painter/native validation follows.'}
 (OUT/'producer_handoff_receipt.json').write_text(json.dumps(receipt,indent=2))

scene=bpy.context.scene
for i,m in enumerate(me.materials):
 m.use_nodes=True;bs=m.node_tree.nodes.get('Principled BSDF');bs.inputs['Base Color'].default_value=(.68,.72,.73,1) if i==0 else (.28,.36,.38,1);bs.inputs['Roughness'].default_value=.32
w=bpy.data.worlds.new('GeometryReview');scene.world=w;w.use_nodes=True;w.node_tree.nodes['Background'].inputs[0].default_value=(.02,.025,.03,1);w.node_tree.nodes['Background'].inputs[1].default_value=.55
for name,loc,power,size in [('Front',(0,12,8),1600,10),('Left',(-9,8,3),1000,8),('Right',(9,8,3),1000,8),('Rim',(0,-7,8),1800,8)]:
 d=bpy.data.lights.new('GEO-Review'+name,'AREA');l=bpy.data.objects.new('GEO-Review'+name,d);scene.collection.objects.link(l);l.location=loc;l.rotation_euler=(-l.location).to_track_quat('-Z','Y').to_euler();d.energy=power;d.shape='DISK';d.size=size
cd=bpy.data.cameras.new('GEO-ReviewCamera');cam=bpy.data.objects.new('GEO-ReviewCamera',cd);scene.collection.objects.link(cam);scene.camera=cam
scene.render.engine='BLENDER_EEVEE';scene.render.resolution_x=1600;scene.render.resolution_y=900;scene.render.resolution_percentage=100;scene.render.image_settings.file_format='PNG';scene.view_settings.view_transform='AgX';scene.render.film_transparent=False
shots=[('front_closed',0.,(0,35,0),'ORTHO'),('front_open',1.,(0,35,0),'ORTHO'),('oblique_closed',0.,(12,30,7),'PERSP'),('oblique_open',1.,(12,30,7),'PERSP')]
if opt.finalize:shots += [('pose_'+str(k).replace('.',''),k,(0,30,0),'PERSP')for k in (0.,.125,.25,.375,.5,.625,.75,.875,1.)]
samples={'Pose_000':0.,'Pose_025':.25,'Pose_050':.5,'Pose_075':.75}
for name,amount,loc,typ in shots:
 for k in keys:k.value=max(0.,1.-abs(amount-samples[k.name])/.25) if k.name in samples else 0.
 cam.location=loc;cam.rotation_euler=(-cam.location).to_track_quat('-Z','Y').to_euler();cd.type=typ;cd.ortho_scale=19.5;cd.lens=49.5 if typ=='PERSP' else 50
 scene.render.filepath=str(OUT/(name+'.png'));bpy.ops.render.render(write_still=True)
if opt.finalize:
 for m in me.materials:
  nodes=m.node_tree.nodes;bs=nodes.get('Principled BSDF');checker=nodes.new('ShaderNodeTexChecker');checker.inputs['Color1'].default_value=(.04,.17,.44,1);checker.inputs['Color2'].default_value=(.85,.85,.85,1);checker.inputs['Scale'].default_value=32;tex=nodes.new('ShaderNodeTexCoord');m.node_tree.links.new(tex.outputs['UV'],checker.inputs['Vector']);m.node_tree.links.new(checker.outputs['Color'],bs.inputs['Base Color']);bs.inputs['Roughness'].default_value=.5
 for k in keys:k.value=0.
 for name,loc in [('checker_front_open',(0,35,0)),('checker_oblique_open',(12,30,7)),('checker_back_open',(0,-35,0))]:
  cam.location=loc;cam.rotation_euler=(-cam.location).to_track_quat('-Z','Y').to_euler();cd.type='ORTHO' if 'front'in name or'back'in name else'PERSP';cd.ortho_scale=19.5;cd.lens=49.5;scene.render.filepath=str(OUT/(name+'.png'));bpy.ops.render.render(write_still=True)
print('REFERENCE_FINISH_UV_HANDOFF' if opt.finalize else'REFERENCE_FORM_PREVIEW',json.dumps({'out':str(OUT),'source_sha256':meta['source_sha256'],'polished_pairs':len(polish),'fbx_exported':opt.finalize}))
