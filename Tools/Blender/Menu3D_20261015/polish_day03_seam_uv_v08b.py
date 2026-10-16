"""Bounded UV-only A/B from the accepted three-chart enamel candidate.

No mesh or morph edits. Gum MINIMUM_STRETCH keeps its longitudinal/lingual
seam policy; enamel UV-boundary relaxation is measured and rollbackable.
"""
import bpy,bmesh,numpy as np,sys,json,hashlib,argparse,runpy
from pathlib import Path
assert bpy.app.background
ap=argparse.ArgumentParser();ap.add_argument('--candidate',required=True);ap.add_argument('--gum-mode',choices=['arclength','minimum-stretch','minimize-stretch'],default='minimum-stretch');ap.add_argument('--boundary-iters',type=int,default=0);opt=ap.parse_args(sys.argv[sys.argv.index('--')+1:])
BASE=Path('E:/DEVGAME/MessControl/ArtSource/UI/MainMenu3D/20261015_refB_v1/Assembly/UnrealHandoff');BEFORE=BASE/'v08b_SeamUV/Candidates/MinimumStretch_3Charts';OUT=BASE/'v08b_SeamUV/Candidates'/opt.candidate;OUT.mkdir(parents=True,exist_ok=True)
source=BEFORE/'MC_DAY03_UnrealMorphSource.blend';sha=hashlib.sha256(source.read_bytes()).hexdigest();assert sha=='a0e9508abddb5a001777a67157c69f39135bf2ed77eda59cf705b09f818b1616'
bpy.ops.wm.open_mainfile(filepath=str(source));o=bpy.data.objects['SK_DAY03'];me=o.data;keys=me.shape_keys.key_blocks;meta=json.loads((BEFORE/'unreal_mouth_handoff.json').read_text());parts=meta['parts'];old={}
for k in keys:
 a=np.empty(len(me.vertices)*3,np.float32);k.data.foreach_get('co',a);old[k.name]=a.reshape(-1,3);k.value=0.
sys.path.insert(0,'C:/Users/user/.codex/skills/blender-uv-baking/scripts');import bx_uvbake as ub
bpy.ops.object.select_all(action='DESELECT');o.select_set(True);bpy.context.view_layer.objects.active=o
A=ub._Arrays(o,'UV0');labels,_=ub._uv_islands(A);slots=np.empty(A.nf,np.int32);me.polygons.foreach_get('material_index',slots);beforeUV=A.uv.copy();uv=A.uv.copy();report={'before_sha256':sha,'gum_mode':opt.gum_mode,'boundary_iterations':opt.boundary_iters}
# UV-node graph respects every seam. Smooth the UV border, then diffuse that
# displacement only through six nearby mesh rings; deep hero UVs stay fixed.
if opt.boundary_iters:
 print('UV_BOUNDARY_GRAPH_BEGIN',flush=True)
 ids=np.column_stack([A.lv,labels[A.lf]]);_,node=np.unique(ids,axis=0,return_inverse=True);nn=int(node.max()+1);pos=np.zeros((nn,2));num=np.bincount(node,minlength=nn);np.add.at(pos,node,uv);pos/=num[:,None]
 ed=np.sort(np.column_stack([node,node[A.nxt]]),axis=1);ue,cnt=np.unique(ed,axis=0,return_counts=True);be=ue[cnt==1];bn=np.unique(be);enamelnodes=np.unique(node[slots[A.lf]==0]);bn=np.intersect1d(bn,enamelnodes);bmask=np.zeros(nn,bool);bmask[bn]=True;be=be[bmask[be].all(1)];degree=np.bincount(be.ravel(),minlength=nn);assert np.all(degree[bn]==2)
 target=pos.copy()
 for it in range(opt.boundary_iters):
  acc=np.zeros_like(pos);np.add.at(acc,be[:,0],target[be[:,1]]);np.add.at(acc,be[:,1],target[be[:,0]]);lam=.38 if it%2==0 else -.395;target[bn]+=lam*(acc[bn]/2-target[bn])
 # Local diffusion transmits the boundary movement smoothly without a sharp
 # first-ring distortion. Only a small UV band may change.
 depth=np.full(nn,99,np.int32);depth[bn]=0
 for d in range(6):
  hit=np.any(depth[ue]==d,axis=1);take=np.unique(ue[hit]);depth[take]=np.minimum(depth[take],d+1)
 disp=target-pos;fixed=(depth==0)|(depth>6);deg=np.bincount(ue.ravel(),minlength=nn)
 for it in range(60):
  acc=np.zeros_like(pos);np.add.at(acc,ue[:,0],disp[ue[:,1]]);np.add.at(acc,ue[:,1],disp[ue[:,0]]);disp[~fixed]=acc[~fixed]/deg[~fixed,None]
 uv=(pos+disp)[node];uv[slots[A.lf]!=0]=beforeUV[slots[A.lf]!=0];ub._set_uv(me,uv,'UV0');report['boundary_max_displacement_4K_px']=float(np.linalg.norm(uv-beforeUV,axis=1).max()*4096)
 print('UV_BOUNDARY_DIFFUSION_DONE',report,flush=True)
 # Boundary UVs are fixed during the no-flip solve; all other UVs initialize
 # from the current candidate. Explicit no_flip avoids the two tiny lingual
 # inversions left by the initial unconstrained Minimum Stretch solve.
 pins=np.zeros(A.nl,bool);pins[bmask[node]]=True;ub._set_pins(me,pins,'UV0')
 with ub._edit_mode([o],'UV0',select_all=False):
  bm=bmesh.from_edit_mesh(me)
  for f in bm.faces:f.select_set(f.material_index==0)
  bmesh.update_edit_mesh(me);bpy.ops.uv.unwrap(method='MINIMUM_STRETCH',no_flip=True,iterations=60,fill_holes=True,correct_aspect=True,margin=0.)
 print('UV_ENAMEL_PINNED_SOLVE_DONE',flush=True)
 ub._set_pins(me,np.zeros(A.nl,bool),'UV0')
 # Repack only enamel after the bounded outline movement.
 with ub._edit_mode([o],'UV0',select_all=False):
  bm=bmesh.from_edit_mesh(me)
  for f in bm.faces:f.select_set(f.material_index==0)
  bmesh.update_edit_mesh(me);bpy.ops.uv.average_islands_scale();bpy.ops.uv.pack_islands(rotate=False,scale=True,merge_overlap=False,margin_method='FRACTION',margin=.00390625)

# Gums inherit exactly the previous arclength-initialized UV charts/seams.
# Their natural solver outline may curve; rectangular atlases are not a goal.
if opt.gum_mode!='arclength':
 print('UV_GUM_SOLVE_BEGIN',flush=True)
 ub.seams_from_uv_islands(o,uv_map='UV0')
 with ub._edit_mode([o],'UV0',select_all=False):
  bm=bmesh.from_edit_mesh(me)
  for f in bm.faces:f.select_set(f.material_index==1)
  bmesh.update_edit_mesh(me)
  if opt.gum_mode=='minimum-stretch':bpy.ops.uv.unwrap(method='MINIMUM_STRETCH',no_flip=True,iterations=100,fill_holes=True,correct_aspect=True,margin=.001)
  else:bpy.ops.uv.minimize_stretch(fill_holes=True,blend=0.,iterations=100)
 print('UV_GUM_SOLVE_DONE',flush=True)
 # Align gum long axes to UV U and preserve distinct body/cap priority. All
 # islands get a uniform physical texel density before a pixel-true pack.
 B=ub._Arrays(o,'UV0');lab,_=ub._uv_islands(B);newuv=B.uv.copy()
 for la in np.unique(lab[slots==1]):
  lm=(lab==la)[B.lf];pts=newuv[lm];xx=B.co[B.lv[lm],0];center=pts.mean(0)
  if np.ptp(xx)>5:
   direction=np.mean((pts-center)*(xx-xx.mean())[:,None],axis=0);ang=-np.arctan2(direction[1],direction[0]);cs,sn=np.cos(ang),np.sin(ang);newuv[lm]=(pts-center)@np.array([[cs,sn],[-sn,cs]])+center
 ub._set_uv(me,newuv,'UV0');B.free()
 with ub._edit_mode([o],'UV0',select_all=False):
  bm=bmesh.from_edit_mesh(me)
  for f in bm.faces:f.select_set(f.material_index==1)
  bmesh.update_edit_mesh(me);bpy.ops.uv.average_islands_scale();bpy.ops.uv.pack_islands(rotate=False,scale=True,merge_overlap=False,margin_method='FRACTION',margin=.00390625)

uv=ub._get_uv(me,'UV0').astype(np.float32);me.calc_loop_triangles();tri=np.array([t.loops[:]for t in me.loop_triangles],np.int32);trimat=np.array([t.material_index for t in me.loop_triangles],np.int32)
for k in keys:
 a=np.empty(len(me.vertices)*3,np.float32);k.data.foreach_get('co',a);assert np.array_equal(a.reshape(-1,3),old[k.name]),k.name
packet0=np.load(BEFORE/'texture_mesh_packet.npz');packet={k:packet0[k]for k in packet0.files};packet['loop_uv']=uv;np.savez_compressed(OUT/'texture_mesh_packet.npz',**packet)
np.savez_compressed(OUT/'source_world_poses_cm.npz',**{str({'Basis':1.,'Pose_000':0.,'Pose_025':.25,'Pose_050':.5,'Pose_075':.75}[k]):v*100 for k,v in old.items()})
uvmeta=json.loads((BEFORE/'painter_uv_handoff.json').read_text());uvmeta['bounded_uv_polish']=report;uvmeta['gum_charts_solver']='MINIMUM_STRETCH100 iterations on original continuouslingualseamed body +2caps, naturaloutline, uniformdensitypack'if opt.gum_mode=='minimum-stretch'else opt.gum_mode;(OUT/'painter_uv_handoff.json').write_text(json.dumps(uvmeta,indent=2))
meta.update({'parent_source':str(source),'parent_source_sha256':sha,'source':str(OUT/'MC_DAY03_UnrealMorphSource.blend'),'status':'UV A/B candidate; geometry/morphs bitexact, independent actual review pending'});bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'MC_DAY03_UnrealMorphSource.blend'));meta['source_sha256']=hashlib.sha256((OUT/'MC_DAY03_UnrealMorphSource.blend').read_bytes()).hexdigest();(OUT/'unreal_mouth_handoff.json').write_text(json.dumps(meta,indent=2))
bpy.ops.object.select_all(action='DESELECT');o.select_set(True);bpy.data.objects['MC_MenuMouth_Rig'].select_set(True);bpy.context.view_layer.objects.active=o;bpy.ops.export_scene.fbx(filepath=str(OUT/'SK_DAY03.fbx'),use_selection=True,object_types={'MESH','ARMATURE'},use_mesh_modifiers=False,add_leaf_bones=False,use_armature_deform_only=True,bake_anim=False,path_mode='STRIP',axis_forward='-Y',axis_up='Z',apply_unit_scale=False,apply_scale_options='FBX_SCALE_NONE',mesh_smooth_type='FACE',use_tspace=False)
runpy.run_path(str(Path(__file__).with_name('day03_painter_uv_v08.py')))['export_static_obj'](o,parts,OUT)
receipt={'files':{name:{'bytes':(OUT/name).stat().st_size,'sha256':hashlib.sha256((OUT/name).read_bytes()).hexdigest()}for name in ['MC_DAY03_UnrealMorphSource.blend','SK_DAY03.fbx','MC_DAY03_Painter_Open.obj','texture_mesh_packet.npz','source_world_poses_cm.npz','painter_uv_handoff.json','unreal_mouth_handoff.json']},'geometry_morph_exact':True,'UV_polish':report,'status':'Candidate requires actuallayout/checker/heatmap and independentQA beforePainter'};(OUT/'producer_handoff_receipt.json').write_text(json.dumps(receipt,indent=2));assert hashlib.sha256(source.read_bytes()).hexdigest()==sha;print('UV_POLISH_EXPORTED',str(OUT),report)
