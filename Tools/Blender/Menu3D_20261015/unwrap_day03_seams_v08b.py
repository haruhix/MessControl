"""v08b semantic dental unwrap. Only UV/seam/export metadata changes.

Continuous facial/incisal body per crown, hidden lingual cut and a connected
cervical cap; continuous longitudinal body per gum, lingual seam and cap disks.
Explicit measured unwrap methods, no SmartProject and no intentional stacking.
"""
import bpy,bmesh,numpy as np,json,math,heapq,hashlib,sys,runpy,argparse
from pathlib import Path
from mathutils import Vector
assert bpy.app.background
ap=argparse.ArgumentParser();ap.add_argument('--method',default='ANGLE_BASED',choices=['ANGLE_BASED','MINIMUM_STRETCH','BEST']);ap.add_argument('--lingual-panel',action='store_true');ap.add_argument('--candidate');opt=ap.parse_args(sys.argv[sys.argv.index('--')+1:]if'--'in sys.argv else[])
BASE=Path('E:/DEVGAME/MessControl/ArtSource/UI/MainMenu3D/20261015_refB_v1/Assembly/UnrealHandoff');SRC=BASE/'v08_ReferenceFinish';OUT=BASE/'v08b_SeamUV'
if opt.candidate:OUT=OUT/'Candidates'/opt.candidate
OUT.mkdir(parents=True,exist_ok=True)
source=SRC/'MC_DAY03_UnrealMorphSource.blend';sha=hashlib.sha256(source.read_bytes()).hexdigest();assert sha=='f7a613f17f97ae1681a13325da81fe33488837146d8702536b3b4ff34a7273d6'
meta=json.loads((SRC/'unreal_mouth_handoff.json').read_text());parts=meta['parts'];bpy.ops.wm.open_mainfile(filepath=str(source));o=bpy.data.objects['SK_DAY03'];me=o.data;keys=me.shape_keys.key_blocks;n=len(me.vertices)
old={}
for k in keys:
 a=np.empty(n*3,np.float32);k.data.foreach_get('co',a);old[k.name]=a.reshape(-1,3);k.value=0.
coords=old['Basis'];edges=np.empty(len(me.edges)*2,np.int32);me.edges.foreach_get('vertices',edges);edges=edges.reshape(-1,2)
lv=np.empty(len(me.loops),np.int32);me.loops.foreach_get('vertex_index',lv);starts=np.empty(len(me.polygons),np.int32);counts=np.empty(len(me.polygons),np.int32);slots=np.empty(len(me.polygons),np.int32);me.polygons.foreach_get('loop_start',starts);me.polygons.foreach_get('loop_total',counts);me.polygons.foreach_get('material_index',slots)
facepart=np.searchsorted(np.array([p['vertex_start']for p in parts]),lv[starts],side='right')-1
edge_faces=[[]for e in edges]
for poly in me.polygons:
 for li in poly.loop_indices:edge_faces[me.loops[li].edge_index].append(poly.index)
seams=np.zeros(len(edges),bool);records=[]
for pi,p in enumerate(parts):
 if p['material_slot']!=0:continue
 st=p['vertex_start'];en=st+p['vertex_count'];c=coords[st:en].astype(np.float64);lower='_Lower_'in p['mesh'];root=c[:,2].min()if lower else c[:,2].max();tip=c[:,2].max()if lower else c[:,2].min();t=np.clip((c[:,2]-root)/(tip-root),0,1);fids=np.flatnonzero(facepart==pi)
 # Connected cervical cap obtained by face flood; threshold is hidden at roots.
 ft=np.array([t[lv[starts[f]:starts[f]+counts[f]]-st].mean()for f in fids]);allowed=set(fids[ft<.115].tolist());seed=int(fids[np.argmin(ft)]);cap={seed};queue=[seed]
 while queue:
  f=queue.pop()
  for li in me.polygons[f].loop_indices:
   for other in edge_faces[me.loops[li].edge_index]:
    if other in allowed and other not in cap:cap.add(other);queue.append(other)
 cap_boundary=[];eids=np.flatnonzero((edges[:,0]>=st)&(edges[:,0]<en));assert np.all((edges[eids]>=st)&(edges[eids]<en))
 for ei in eids:
  fs=edge_faces[ei]
  if len(fs)==2 and ((fs[0]in cap)!=(fs[1]in cap)):seams[ei]=True;cap_boundary.extend(edges[ei]-st)
 assert cap_boundary,p['mesh']
 # Dental arc outward normal; the opposite side is the hidden lingual route.
 cx=abs((c[:,0].min()+c[:,0].max())*.5);slope=-3.25*3.1/8.4*(cx/8.4)**2.1 if not lower else -3.05*2.9/8.15*(cx/8.15)**1.9
 sign=1 if c[:,0].mean()>0 else -1;front=np.array([-slope*sign,1.,0.]);front/=np.linalg.norm(front);q=c@front;qnorm=(q-q.min())/max(np.ptp(q),1e-9)
 if opt.lingual_panel:
  # A separate hidden lingual panel removes the dome-flattening pole from the
  # entire facial/incisal body. Its boundary is behind the visible sidewalls.
  fq=np.array([qnorm[lv[starts[f]:starts[f]+counts[f]]-st].mean()for f in fids]);allowed_back=set(fids[fq<.44].tolist())-cap;seed=min(allowed_back,key=lambda f:float(qnorm[lv[starts[f]:starts[f]+counts[f]]-st].mean()));back={seed};queue=[seed]
  while queue:
   f=queue.pop()
   for li in me.polygons[f].loop_indices:
    for other in edge_faces[me.loops[li].edge_index]:
     if other in allowed_back and other not in back:back.add(other);queue.append(other)
  for ei in eids:
   fs=edge_faces[ei]
   if len(fs)==2 and ((fs[0]in back)!=(fs[1]in back)):seams[ei]=True
  records.append({'part':p['mesh'],'cervical_cap_faces':len(cap),'lingual_panel_faces':len(back),'cap_threshold_root_to_tip':.115,'lingual_panel_depth_threshold':.44,'policy':'Three logicalcharts: cervicalcap,hiddenlingualpanel, continuousfacial/incisal/visiblesidewallbody. Noseam/poleonthefacialhero.'})
  continue
 boundary=np.unique(cap_boundary);start=int(boundary[np.argmin(qnorm[boundary])]);candidates=np.flatnonzero(t>.985);target=int(candidates[np.argmin(qnorm[candidates])])
 adj=[[]for i in c]
 for ei in eids:
  aa,bb=edges[ei]-st;length=float(np.linalg.norm(c[aa]-c[bb]));penalty=1+12*((qnorm[aa]+qnorm[bb])*.5)**3;adj[aa].append((int(bb),int(ei),length*penalty));adj[bb].append((int(aa),int(ei),length*penalty))
 distance=np.full(len(c),np.inf);distance[start]=0;parent={};heap=[(0.,start)]
 while heap:
  d,v=heapq.heappop(heap)
  if d!=distance[v]:continue
  if v==target:break
  for nb,ei,w in adj[v]:
   candidate=d+w
   if candidate<distance[nb]:distance[nb]=candidate;parent[nb]=(v,ei);heapq.heappush(heap,(candidate,nb))
 assert target in parent or target==start
 v=target;path=[]
 while v!=start:
  prev,ei=parent[v];seams[ei]=True;path.append(ei);v=prev
 records.append({'part':p['mesh'],'cervical_cap_faces':len(cap),'cap_threshold_root_to_tip':.115,'lingual_path_edges':len(path),'lingual_start_vertex':st+start,'lingual_end_vertex':st+target,'policy':'Connected hiddenrootcap + continuous facial/incisalbody, lingualcut only'})
me.edges.foreach_set('use_seam',seams);me.update()

# The skill helper clears stale UVs with sentinel values and measures solver
# failures/folds even when Blender returns FINISHED. BEST compares3methods.
sys.path.insert(0,'C:/Users/user/.codex/skills/blender-uv-baking/scripts');import bx_uvbake as ub
bpy.ops.object.select_all(action='DESELECT');o.select_set(True);bpy.context.view_layer.objects.active=o
unwrap=ub.unwrap(o,method=opt.method,uv_map='UV0',faces=np.flatnonzero(slots==0).tolist(),iterations=30)
(OUT/('unwrap_'+opt.method+'.json')).write_text(json.dumps(unwrap,indent=2));assert all(v==0 for v in unwrap['unsolved_faces'].values()),unwrap['unsolved_faces'];assert not unwrap['folding_islands'],unwrap['folding_islands']

# Align each enamel body's root-to-tip direction before uniform-density pack.
A=ub._Arrays(o,'UV0');labels,_=ub._uv_islands(A);uv=A.uv.copy();anatomy_t=np.zeros(n)
for p in parts:
 if p['material_slot']!=0:continue
 st=p['vertex_start'];en=st+p['vertex_count'];c=coords[st:en];lower='_Lower_'in p['mesh'];root=c[:,2].min()if lower else c[:,2].max();tip=c[:,2].max()if lower else c[:,2].min();anatomy_t[st:en]=(c[:,2]-root)/(tip-root)
for label in np.unique(labels[slots==0]):
 lm=(labels==label)[A.lf];tt=anatomy_t[A.lv[lm]];points=uv[lm];center=points.mean(0)
 if np.ptp(tt)>.3:
  direction=np.mean((points-center)*(tt-tt.mean())[:,None],axis=0);angle=-np.pi/2-math.atan2(direction[1],direction[0]);cs,sn=math.cos(angle),math.sin(angle);uv[lm]=(points-center)@np.array([[cs,sn],[-sn,cs]])+center
ub._set_uv(me,uv,'UV0');A.free()
bpy.ops.object.mode_set(mode='EDIT');bpy.ops.mesh.select_mode(type='FACE');bpy.ops.mesh.select_all(action='DESELECT');bm=bmesh.from_edit_mesh(me)
for f in bm.faces:
 if f.material_index==0:f.select_set(True)
bmesh.update_edit_mesh(me);bpy.ops.uv.average_islands_scale();bpy.ops.uv.pack_islands(udim_source='CLOSEST_UDIM',rotate=False,scale=True,merge_overlap=False,margin_method='FRACTION',margin=.00390625);bpy.ops.object.mode_set(mode='OBJECT')

# One continuous physical-length longitudinal body per gum, plus2tinycaps.
uv=np.empty(len(me.loops)*2,np.float32);me.uv_layers['UV0'].data.foreach_get('uv',uv);uv=uv.reshape(-1,2);gums=[]
for p in [p for p in parts if p['material_slot']==1]:
 c=coords[p['vertex_start']:p['vertex_start']+p['vertex_count']];rings=c[:241*40].reshape(241,40,3);centers=rings.mean(1);u=np.r_[0,np.cumsum(np.linalg.norm(np.diff(centers,axis=0),axis=1))];dist=np.linalg.norm(np.roll(rings,-1,axis=1)-rings,axis=2);gums.append((p,u,dist))
scale=.96/max(g[1][-1]for g in gums);gumcharts=[]
for gi,(p,u,dist)in enumerate(gums):
 st=p['vertex_start'];en=st+p['vertex_count'];circ=dist.sum(1);begin=.5-u[-1]*scale/2;cy=.15 if gi==0 else .46
 radial_order=np.r_[np.arange(20,40),np.arange(0,20)];cross=np.c_[np.zeros(241),np.cumsum(dist[:,radial_order],axis=1)]
 capcenters=[(.20+.2*(gi*2+cap),.82)for cap in(0,1)];capradius=.012
 for s,c in zip(starts,counts):
  ids=lv[s:s+c]-st
  if not 0<=ids[0]<en-st:continue
  if np.any(ids>=241*40):
   cap=int(ids.max()-241*40);center=np.array(capcenters[cap]);uv[s:s+c]=[center if v>=241*40 else center+capradius*np.array([math.cos(2*np.pi*(int(v)%40)/40),math.sin(2*np.pi*(int(v)%40)/40)])for v in ids]
  else:
   ring=ids//40;j=(ids%40-20)%40
   if j.max()-j.min()>20:j=np.where(j==0,40,j)
   uv[s:s+c]=np.column_stack([begin+u[ring]*scale,cy+(cross[ring,j]-circ[ring]/2)*scale])
 gumcharts.append({'part':p['mesh'],'body':'One continuouslongitudinalchart, lingualseamradial20','body_physical_length':float(u[-1]),'uv_scale_per_sourceunit':float(scale),'caps':capcenters,'cap_uv_radius':capradius,'cap_density_policy':'Hiddenverytinyreturncaps have minimum49pxradius at4K forcoverage; intentionallyseparateprioritygroup'})
me.uv_layers['UV0'].data.foreach_set('uv',uv.ravel());me.update();assert uv.min()>=0 and uv.max()<=1

# Exact immutable geometry contract, including every absolute morph sample.
for k in keys:
 a=np.empty(n*3,np.float32);k.data.foreach_get('co',a);assert np.array_equal(a.reshape(-1,3),old[k.name]),k.name
me.calc_loop_triangles();tri=np.array([t.loops[:]for t in me.loop_triangles],np.int32);trimat=np.array([t.material_index for t in me.loop_triangles],np.int32)
oldpacket=np.load(SRC/'texture_mesh_packet.npz');packet={k:oldpacket[k]for k in oldpacket.files};packet['loop_uv']=uv;packet['loop_vertex_indices']=lv;packet['triangle_loop_indices']=tri;packet['triangle_material_indices']=trimat;np.savez_compressed(OUT/'texture_mesh_packet.npz',**packet)
np.savez_compressed(OUT/'source_world_poses_cm.npz',**{str({'Basis':1.,'Pose_000':0.,'Pose_025':.25,'Pose_050':.5,'Pose_075':.75}[k]):v*100 for k,v in old.items()})
uvreport={'policy':'Unique0–1UV0 per material, NOmirror/repeatstacking; noSmartProject','texture_sets':['MC_Enamel','MC_Gum'],'resolution':4096,'padding_target_px':16,'pack_margin_fraction':.00390625,'desired_pair_gap_px':32,'crown_seams':records,'unwrap_report':unwrap,'gum_charts':gumcharts,'root_tip_orientation':'Mainbody root→tip mapsUVdown. Caps areseparatehiddenprioritygroup.','geometry_morph_positions':'Bitexact f7a613…v08 all5samples; onlyseams/UV/metadatachanged'}
(OUT/'painter_uv_handoff.json').write_text(json.dumps(uvreport,indent=2));meta.update({'parent_source':str(source),'parent_source_sha256':sha,'source':str(OUT/'MC_DAY03_UnrealMorphSource.blend'),'status':'v08bcoherentseamUVcandidate; geometryunchanged; independentUVacceptancepending','painter_uv_handoff':'painter_uv_handoff.json'});bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'MC_DAY03_UnrealMorphSource.blend'));meta['source_sha256']=hashlib.sha256((OUT/'MC_DAY03_UnrealMorphSource.blend').read_bytes()).hexdigest();(OUT/'unreal_mouth_handoff.json').write_text(json.dumps(meta,indent=2))
bpy.ops.object.select_all(action='DESELECT');o.select_set(True);rig=bpy.data.objects['MC_MenuMouth_Rig'];rig.select_set(True);bpy.context.view_layer.objects.active=o;bpy.ops.export_scene.fbx(filepath=str(OUT/'SK_DAY03.fbx'),use_selection=True,object_types={'MESH','ARMATURE'},use_mesh_modifiers=False,add_leaf_bones=False,use_armature_deform_only=True,bake_anim=False,path_mode='STRIP',axis_forward='-Y',axis_up='Z',apply_unit_scale=False,apply_scale_options='FBX_SCALE_NONE',mesh_smooth_type='FACE',use_tspace=False)
helpers=runpy.run_path(str(Path(__file__).with_name('day03_painter_uv_v08.py')));helpers['export_static_obj'](o,parts,OUT)
receipt={'files':{name:{'bytes':(OUT/name).stat().st_size,'sha256':hashlib.sha256((OUT/name).read_bytes()).hexdigest()}for name in ['MC_DAY03_UnrealMorphSource.blend','SK_DAY03.fbx','MC_DAY03_Painter_Open.obj','texture_mesh_packet.npz','source_world_poses_cm.npz','painter_uv_handoff.json','unreal_mouth_handoff.json']},'geometry_morph_exact':True,'status':'UVcandidate awaitslayout/checker/distortion/density/gap independentreview BEFOREPainterrelease'};(OUT/'producer_handoff_receipt.json').write_text(json.dumps(receipt,indent=2))
print('SEAM_UV_CANDIDATE_EXPORTED',json.dumps({'out':str(OUT),'unwrap':{k:v for k,v in unwrap.items()if k!='per_island'},'source_sha':meta['source_sha256']}))
