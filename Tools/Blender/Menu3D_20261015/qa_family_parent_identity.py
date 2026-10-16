"""Read accepted parent shape keys and compare independently extracted frozen poses."""
import bpy,sys,json,hashlib
from pathlib import Path
import numpy as np
assert bpy.app.background
b=Path(sys.argv[sys.argv.index('--')+1])
for day,rev in [('DAY01','UV_v02_GumReturns'),('DAY02','UV_v01')]:
 receipt=json.loads((b/day/rev/'frozen_handoff_receipt.json').read_text());parent=Path(receipt['lead_shape_source']['path']);bpy.ops.wm.open_mainfile(filepath=str(parent));meshes=[o for o in bpy.data.objects if o.type=='MESH'and o.data.shape_keys];assert len(meshes)==1;ob=meshes[0];expected=np.load(b/'QA_Independent'/day/'source_geometry.npz');matrix=np.asarray(ob.matrix_world,dtype=float);rows={}
 for k in ob.data.shape_keys.key_blocks:
  v=np.empty(len(k.data)*3,np.float32);k.data.foreach_get('co',v);world=v.reshape(-1,3).astype(float)@matrix[:3,:3].T+matrix[:3,3];rows[k.name]={'bitexact':np.array_equal(world,expected[k.name]),'max_absolute_error_source_units':float(abs(world-expected[k.name]).max())}
 report={'independent':True,'read_only':True,'parent':str(parent),'parent_sha256':hashlib.sha256(parent.read_bytes()).hexdigest(),'parent_hash_matches_frozen_receipt':hashlib.sha256(parent.read_bytes()).hexdigest()==receipt['lead_shape_source']['sha256'],'poses':rows,'all5absoluteposes_bitexact':len(rows)==5 and all(x['bitexact']for x in rows.values())};(b/'QA_Independent'/day/'parent_shape_identity.json').write_text(json.dumps(report,indent=2));print(json.dumps({'day':day,**report}))
