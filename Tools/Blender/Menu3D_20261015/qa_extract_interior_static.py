"""Read-only saved source/parent/fresh static FBX extraction and identity."""
import bpy,sys,json,hashlib
from pathlib import Path
import numpy as np
from mathutils import Vector
from mathutils.kdtree import KDTree
from mathutils.bvhtree import BVHTree
assert bpy.app.background
p=Path(sys.argv[sys.argv.index('--')+1]);out=p/'QA_Independent';out.mkdir(exist_ok=True);manifest=json.loads((p/'interior_handoff.json').read_text());names=['SM_DAY03_Interior','SM_DAY03_Tongue'];report={'independent':True,'read_only':True,'source':str(p),'parts':{},'gates':{}}
def fetch(seq,prop,n,dtype):
 a=np.empty(len(seq)*n,dtype);seq.foreach_get(prop,a);return a.reshape(-1,n) if n>1 else a
def extract(o):
 m=o.data;m.calc_loop_triangles();matrix=np.asarray(o.matrix_world,dtype=float);linear=matrix[:3,:3];co=fetch(m.vertices,'co',3,np.float32);world=(co@linear.T+matrix[:3,3]).astype(np.float32);norm=fetch(m.corner_normals,'vector',3,np.float32)@np.linalg.inv(linear);norm/=np.maximum(np.linalg.norm(norm,axis=1)[:,None],1e-30)
 return {'co':world,'uv':fetch(m.uv_layers.active.data,'uv',2,np.float32),'loopverts':fetch(m.loops,'vertex_index',1,np.int32),'loopedges':fetch(m.loops,'edge_index',1,np.int32),'starts':fetch(m.polygons,'loop_start',1,np.int32),'counts':fetch(m.polygons,'loop_total',1,np.int32),'material':fetch(m.polygons,'material_index',1,np.int32),'triloops':fetch(m.loop_triangles,'loops',3,np.int32),'tripoly':fetch(m.loop_triangles,'polygon_index',1,np.int32),'loopnormals':norm.astype(np.float32)}
def save_extract(label,data,material_names):
 np.savez_compressed(out/(label+'.npz'),**data);(out/(label+'.json')).write_text(json.dumps({'independent':True,'read_only':True,'source':str(p),'material_names':material_names,'source_sha256':report['source_sha256']},indent=2))
bpy.ops.wm.open_mainfile(filepath=str(p/'MC_DAY03_InteriorSource.blend'));report['source_sha256']=hashlib.sha256((p/'MC_DAY03_InteriorSource.blend').read_bytes()).hexdigest();source={};combined={k:[]for k in ['co','uv','loopverts','loopedges','starts','counts','material','triloops','tripoly','loopnormals']};vo=lo=po=eo=0
for index,name in enumerate(names):
 o=bpy.data.objects[name];d=extract(o);source[name]=d;m=o.data;tri=d['co'][d['loopverts'][d['triloops']]];cross=np.cross(tri[:,1]-tri[:,0],tri[:,2]-tri[:,0]);volume=float(np.sum(np.einsum('ij,ij->i',tri[:,0],cross))/6);edges=np.concatenate((d['loopverts'][:,None],np.roll(d['loopverts'],-1)[:,None]),axis=1);nxt=np.arange(len(d['loopverts']))+1;nxt[d['starts']+d['counts']-1]=d['starts'];edges=np.sort(np.stack((d['loopverts'],d['loopverts'][nxt]),1),axis=1);_,edgecounts=np.unique(edges,axis=0,return_counts=True)
 report['parts'][name]={'vertices':len(m.vertices),'triangles':len(m.loop_triangles),'matrix_world':np.asarray(o.matrix_world).tolist(),'scale':list(o.scale),'bbox':[d['co'].min(0).tolist(),d['co'].max(0).tolist()],'closed_manifold_edges':bool(np.all(edgecounts==2)),'non_two_face_edges':int((edgecounts!=2).sum()),'signed_volume':volume,'all_faces_smooth':all(f.use_smooth for f in m.polygons),'uv_layers':[l.name for l in m.uv_layers],'shape_keys':m.shape_keys is not None,'modifiers':[x.type for x in o.modifiers]}
 save_extract('source_'+name,d,[s.name for s in m.materials])
 for k in combined:
  val=d[k].copy()
  if k=='loopverts':val+=vo
  elif k in('starts','triloops'):val+=lo
  elif k=='loopedges':val+=eo
  elif k=='tripoly':val+=po
  elif k=='material':val[:]=index
  combined[k].append(val)
 vo+=len(d['co']);lo+=len(d['uv']);po+=len(d['counts']);eo+=len(m.edges)
 if name=='SM_DAY03_Tongue':
  bvh=BVHTree.FromPolygons([Vector(x)for x in d['co']],[tuple(f.vertices)for f in m.polygons]);anchor=np.array(manifest['character_contact_anchor']);hit=bvh.ray_cast(Vector((anchor[0],anchor[1],10)),Vector((0,0,-1)),100);report['character_anchor']={'source':anchor.tolist(),'tongue_top_hit':list(hit[0])if hit[0]else None,'height_error_source_units':float(hit[0].z-anchor[2])if hit[0]else None}
joined={k:np.concatenate(v)for k,v in combined.items()};save_extract('source_combined',joined,['MC_Interior','MC_Tongue']);packet=np.load(p/'texture_mesh_packet.npz');report['gates']['source_packet_positions_bitexact']=np.array_equal(joined['co'],packet['vertices']);report['gates']['source_packet_uv_bitexact']=np.array_equal(joined['uv'],packet['loop_uv']);report['gates']['source_packet_topology_bitexact']=all(np.array_equal(joined[k],packet[q])for k,q in [('loopverts','loop_vertex_indices'),('triloops','triangle_loop_indices'),('material','polygon_material_indices')])
bpy.ops.wm.open_mainfile(filepath=manifest['parent_source'])
report['gates']['uv_only_parent_geometry_bitexact']=all(np.array_equal(extract(bpy.data.objects[n])['co'],source[n]['co'])for n in names)
for name in names:
 bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.fbx(filepath=str(p/(name+'.fbx')),use_anim=False);meshes=[o for o in bpy.data.objects if o.type=='MESH'];assert len(meshes)==1;d=extract(meshes[0]);save_extract('fresh_'+name,d,[s.name for s in meshes[0].data.materials]);kd=KDTree(len(source[name]['co']))
 for i,x in enumerate(source[name]['co']):kd.insert(Vector(x),i)
 kd.balance();dist=[kd.find(Vector(x))[2]for x in d['co']];report['parts'][name]['fresh_fbx']={'vertices':len(d['co']),'max_position_error_source_units':max(dist),'matrix_world':np.asarray(meshes[0].matrix_world).tolist(),'uv_finite':bool(np.isfinite(d['uv']).all()),'armatures':sum(o.type=='ARMATURE'for o in bpy.data.objects)};report['gates']['fresh_'+name+'_position_1e_minus_3_cm']=max(dist)*100<=.001
report['gates']['two_closed_manifold_positive_volume']=all(v['closed_manifold_edges']and v['signed_volume']>0 for v in report['parts'].values());report['gates']['source_identity_origins_scale']=all(np.array_equal(np.array(v['matrix_world']),np.eye(4))for v in report['parts'].values());report['gates']['anchor_within_1e_minus_3_source_units']=report['character_anchor']['height_error_source_units']is not None and abs(report['character_anchor']['height_error_source_units'])<.001;report['geometry_export_pass']=all(report['gates'].values());(out/'source_fresh_static_geometry.json').write_text(json.dumps(report,indent=2));print(json.dumps({'report':str(out/'source_fresh_static_geometry.json'),'gates':report['gates'],'anchor':report['character_anchor']}))
