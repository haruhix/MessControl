"""UV-only candidate/export packet for the accepted organic interior shape.

Requires an explicit accepted geometry candidate argument. Does not release
Painter or modify Unreal; layout/checkers/independent identity gate follows.
"""
import bpy,bmesh,numpy as np,argparse,sys,json,math,hashlib
from pathlib import Path
assert bpy.app.background
ap=argparse.ArgumentParser();ap.add_argument('--shape',required=True);ap.add_argument('--candidate',required=True);opt=ap.parse_args(sys.argv[sys.argv.index('--')+1:]);BASE=Path('E:/DEVGAME/MessControl/ArtSource/UI/MainMenu3D/20261015_refB_v1/Assembly/UnrealHandoff/v08_ReferenceFinish/Interior');SRC=BASE/opt.shape;OUT=BASE/opt.candidate;OUT.mkdir(parents=True,exist_ok=True);source=SRC/'MC_DAY03_InteriorSource.blend';sha=hashlib.sha256(source.read_bytes()).hexdigest();bpy.ops.wm.open_mainfile(filepath=str(source));objects=[bpy.data.objects[n]for n in ['SM_DAY03_Interior','SM_DAY03_Tongue']]
sys.path.insert(0,'C:/Users/user/.codex/skills/blender-uv-baking/scripts');import bx_uvbake as ub
records=[];old={};offset=0;allco=[];allfaces=[];alluv=[];allmat=[];parts=[];semantic={k:[]for k in ['cavity_depth_weight','buccal_arch_weight','palate_weight','palatal_rugae_weight','floor_weight','tongue_dorsal_weight','median_sulcus_weight','anterior_tip_weight']}
for slot,ob in enumerate(objects):
 me=ob.data;co=np.empty(len(me.vertices)*3,np.float32);me.vertices.foreach_get('co',co);co=co.reshape(-1,3);old[ob.name]=co.copy();ub.seams_from_uv_islands(ob,uv_map='UV0');r=ub.unwrap(ob,method='MINIMUM_STRETCH',uv_map='UV0',iterations=60);assert not r['folding_islands']and not any(r['unsolved_faces'].values()),r
 A=ub._Arrays(ob,'UV0');labels,_=ub._uv_islands(A);assert len(np.unique(labels))==(3 if slot==0 else 2);uv=A.uv.copy();chartrecords=[]
 for label in np.unique(labels):
  fm=labels==label;lm=fm[A.lf];points=uv[lm];center=points.mean(0);xx=co[A.lv[lm],0];direction=np.mean((points-center)*(xx-xx.mean())[:,None],axis=0);ang=-np.arctan2(direction[1],direction[0]);cs,sn=np.cos(ang),np.sin(ang);pts=(points-center)@np.array([[cs,sn],[-sn,cs]])
  if slot==0:
   z=co[A.lv[lm],2];is_inner=np.all(A.lv[lm]<8193);is_back=np.all(A.lv[lm]>=8193);semanticname='inner_cavity'if is_inner else'hidden_back_return'if is_back else'hidden_rim';priority=1. if is_inner else.35 if is_back else.45
   if np.mean(pts[:,1]*(z-z.mean()))<0:pts[:,1]*=-1
  else:
   z=co[A.lv[lm],2];is_top=np.median(z)>-3.43;semanticname='dorsal'if is_top else'ventral';priority=1. if is_top else.5;yy=co[A.lv[lm],1]
   if np.mean(pts[:,1]*(yy-yy.mean()))>0:pts[:,1]*=-1
  uv[lm]=pts+center;chartrecords.append({'island':int(label),'surface':semanticname,'relative_density':priority,'faces':np.flatnonzero(fm).tolist()})
 ub._set_uv(me,uv,'UV0');A.free()
 with ub._edit_mode([ob],'UV0'):
  bpy.ops.uv.average_islands_scale()
 uv=ub._get_uv(me,'UV0');A=ub._Arrays(ob,'UV0')
 for c in chartrecords:
  lm=np.isin(A.lf,c['faces']);center=uv[lm].mean(0);uv[lm]=center+(uv[lm]-center)*c['relative_density']
 ub._set_uv(me,uv,'UV0');A.free()
 with ub._edit_mode([ob],'UV0'):
  bpy.ops.uv.pack_islands(rotate=False,scale=True,merge_overlap=False,margin_method='FRACTION',margin=.00390625)
 uv=ub._get_uv(me,'UV0');after=np.empty(len(me.vertices)*3,np.float32);me.vertices.foreach_get('co',after);assert np.array_equal(after.reshape(-1,3),old[ob.name]);me.calc_loop_triangles();records.append({'mesh':ob.name,'unwrap':r,'charts':[{k:v for k,v in c.items()if k!='faces'}for c in chartrecords],'geometry_bitexact':True})
 bpy.ops.object.select_all(action='DESELECT');ob.select_set(True);bpy.context.view_layer.objects.active=ob;bpy.ops.export_scene.fbx(filepath=str(OUT/(ob.name+'.fbx')),use_selection=True,object_types={'MESH'},use_mesh_modifiers=False,bake_anim=False,path_mode='STRIP',axis_forward='-Y',axis_up='Z',apply_unit_scale=False,apply_scale_options='FBX_SCALE_NONE',mesh_smooth_type='FACE',use_tspace=False)
 parts.append({'mesh':ob.name,'vertex_start':offset,'vertex_count':len(co),'material_slot':slot});allco.extend(co.tolist())
 for face in me.polygons:allfaces.append([int(v)+offset for v in face.vertices]);alluv.append([uv[li].tolist()for li in face.loop_indices]);allmat.append(slot)
 x,y,z=co.T;zero=np.zeros(len(co),np.float32)
 if slot==0:
  weights={'cavity_depth_weight':np.clip((-y-3.2)/10.85,0,1),'buccal_arch_weight':np.exp(-((abs(x)-(5.35+.115*(z+.3)**2))/1.12)**2-((z+.1)/2.7)**4),'palate_weight':np.clip((z-.8)/2,0,1)*np.exp(-(x/5.3)**6),'palatal_rugae_weight':sum(np.exp(-((z-(zz-.024*x*x))/.145)**2-(x/4.6)**6)for zz in [1.85,2.25,2.65,3.05]),'floor_weight':np.exp(-(x/5.)**4-((z+2.65)/.95)**2)}
 else:
  dorsal=(z>-3.43).astype(np.float32);weights={'tongue_dorsal_weight':dorsal,'median_sulcus_weight':np.exp(-(x/.66)**2)*dorsal,'anterior_tip_weight':np.clip((y+5.05)/4.4,0,1)*dorsal}
 for name in semantic:semantic[name].extend(np.clip(weights.get(name,zero),0,1).tolist())
 offset+=len(co)
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'MC_DAY03_InteriorSource.blend'))
# Combined static Painter mesh with identical UVs, smooth loop normals and two
# sets. It is not saved into the native source or exported into separate FBXs.
me=bpy.data.meshes.new('Interior_Painter_Surface');me.from_pydata(allco,[],allfaces);me.materials.clear()
for ob in objects:me.materials.append(ob.data.materials[0])
uvlayer=me.uv_layers.new(name='UV0')
for f,uvs,slot in zip(me.polygons,alluv,allmat):
 f.material_index=slot;f.use_smooth=True
 for li,val in zip(f.loop_indices,uvs):uvlayer.data[li].uv=val
me.update();me.calc_loop_triangles();co=np.array(allco,np.float32);lv=np.array([l.vertex_index for l in me.loops],np.int32);uv=np.array([l.uv[:]for l in me.uv_layers['UV0'].data],np.float32);triloop=np.array([t.loops[:]for t in me.loop_triangles],np.int32);trimat=np.array([t.material_index for t in me.loop_triangles],np.int32);polymat=np.array(allmat,np.int32);normal=np.array([n.vector[:]for n in me.corner_normals],np.float32)
for alias,oldname in [('rugae_crest_weight','palatal_rugae_weight'),('tongue_sulcus_weight','median_sulcus_weight'),('tongue_tip_weight','anterior_tip_weight')]:semantic[alias]=semantic[oldname]
hidden=np.zeros(len(co),np.float32);hidden[8193:16386]=1;semantic['hidden_return_weight']=hidden.tolist()
np.savez_compressed(OUT/'texture_mesh_packet.npz',vertices=co,loop_vertex_indices=lv,loop_uv=uv,triangle_loop_indices=triloop,triangle_material_indices=trimat,polygon_material_indices=polymat,**{k:np.array(v,np.float32)for k,v in semantic.items()})
objname='MC_DAY03_Interior_Painter_Open';mtlnames=['MC_Interior','MC_Tongue']
with(OUT/(objname+'.obj')).open('w',newline='\n')as f:
 f.write('# Matching nativeUV0 with explicit per-loop normals\nmtllib '+objname+'.mtl\n')
 for c in co:f.write('v %.8f %.8f %.8f\n'%tuple(c))
 for c in uv:f.write('vt %.8f %.8f\n'%tuple(c))
 for c in normal:f.write('vn %.8f %.8f %.8f\n'%tuple(c))
 f.write('s 1\n');last=-1
 for face in me.polygons:
  slot=face.material_index
  if slot!=last:f.write('g '+objects[slot].name+'\nusemtl '+mtlnames[slot]+'\n');last=slot
  f.write('f '+' '.join(str(int(lv[i])+1)+'/'+str(i+1)+'/'+str(i+1)for i in face.loop_indices)+'\n')
(OUT/(objname+'.mtl')).write_text('newmtl MC_Interior\nKd .3 .1 .07\nnewmtl MC_Tongue\nKd .4 .15 .1\n')
shape_meta=json.loads((SRC/'interior_handoff.json').read_text());report={'parent_source':str(source),'parent_source_sha256':sha,'source':str(OUT/'MC_DAY03_InteriorSource.blend'),'source_sha256':hashlib.sha256((OUT/'MC_DAY03_InteriorSource.blend').read_bytes()).hexdigest(),'status':'UV candidate; noPainter/nativepromotion beforeactualUVlayout/checker/QAidentity','materials':mtlnames,'parts':parts,'UV_records':records,'geometry_bitexact':True,'anatomy_weights':list(semantic),'texture_resolution':4096,'pixel_gap_target':32,'origins':'Identity +Yfacing; metadataCM pipeline; noextra100','character_contact_anchor':shape_meta['character_contact_anchor'],'assembly_contract':{'native_actor_uniform_scale':.5376540384,'child_relative_scale':[.90,1,1],'open_full_height_world_cm':370,'closed_full_height_world_cm':246.666667,'interior_local_mask_half_source_open':370/(2.6666666666666665*.5376540384)/100,'mask_center_local_z':0,'mask_formula':'abs(TransformPosition(WorldPosition to Local).Z), threshold CardHalfHeight; geometry source units divided by100 from UE-local cm','runtime_character_lower_readability':'Lead owns actual native camera, 9-pose fit and existing-character animation. Geometry is bitexact to accepted r05.'}};(OUT/'interior_handoff.json').write_text(json.dumps(report,indent=2));files=['MC_DAY03_InteriorSource.blend','SM_DAY03_Interior.fbx','SM_DAY03_Tongue.fbx',objname+'.obj','texture_mesh_packet.npz','interior_handoff.json'];receipt={'files':{n:hashlib.sha256((OUT/n).read_bytes()).hexdigest()for n in files},'geometry_exact':True};(OUT/'producer_handoff_receipt.json').write_text(json.dumps(receipt,indent=2));assert hashlib.sha256(source.read_bytes()).hexdigest()==sha;print('ORGANIC_INTERIOR_UV_CANDIDATE',json.dumps(receipt))
