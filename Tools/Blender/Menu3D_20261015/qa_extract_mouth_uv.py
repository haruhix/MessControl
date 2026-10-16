"""Read-only source UV extraction for independent CPU overlap/coverage checks."""
import argparse, hashlib, json, sys
from pathlib import Path
import bpy
import numpy as np
assert bpy.app.background
p=argparse.ArgumentParser();p.add_argument('--asset',required=True);p.add_argument('--output',required=True)
a=p.parse_args(sys.argv[sys.argv.index('--')+1:]);src=Path(a.asset);out=Path(a.output);out.parent.mkdir(parents=True,exist_ok=True)
if src.suffix.lower()=='.blend':bpy.ops.wm.open_mainfile(filepath=str(src))
else:
 bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.fbx(filepath=str(src),use_anim=False)
meshes=[o for o in bpy.data.objects if o.type=='MESH'];assert len(meshes)==1,'Expected one joined mouth mesh';o=meshes[0];m=o.data;m.calc_loop_triangles();uv_layer=m.uv_layers.get('UV0') or m.uv_layers.active;assert uv_layer is not None
def get(seq,prop,n,dtype):
 r=np.empty(len(seq)*n,dtype);seq.foreach_get(prop,r);return r.reshape(-1,n) if n>1 else r
co=get(m.shape_keys.key_blocks['Basis'].data,'co',3,np.float32)
matrix=np.asarray(o.matrix_world,dtype=np.float64);co=(co@matrix[:3,:3].T+matrix[:3,3]).astype(np.float32)
np.savez_compressed(out,co=co,uv=get(uv_layer.data,'uv',2,np.float32),
 loopverts=get(m.loops,'vertex_index',1,np.int32),loopedges=get(m.loops,'edge_index',1,np.int32),
 starts=get(m.polygons,'loop_start',1,np.int32),counts=get(m.polygons,'loop_total',1,np.int32),
 material=get(m.polygons,'material_index',1,np.int32),
 triloops=get(m.loop_triangles,'loops',3,np.int32),tripoly=get(m.loop_triangles,'polygon_index',1,np.int32))
meta={'source':str(src),'source_sha256':hashlib.sha256(src.read_bytes()).hexdigest(),'material_names':[s.name for s in m.materials],
      'uv_name':uv_layer.name,'vertices':len(m.vertices),'polygons':len(m.polygons),'triangles':len(m.loop_triangles),
      'independent':True,'read_only':True}
out.with_suffix('.json').write_text(json.dumps(meta,indent=2),encoding='utf-8');print(json.dumps(meta))
