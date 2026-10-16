"""Independent source packet/fresh FBX UV-corner identity (CPU, read only)."""
import argparse,hashlib,json
from pathlib import Path
import cv2
import numpy as np

p=argparse.ArgumentParser();p.add_argument('--source',required=True);p.add_argument('--fresh',required=True);p.add_argument('--packet',required=True);p.add_argument('--output',required=True);a=p.parse_args()
s=np.load(a.source);f=np.load(a.fresh);packet=np.load(a.packet)
source_uv=s['uv'];fresh_uv=f['uv'];assert np.isfinite(source_uv).all() and np.isfinite(fresh_uv).all()
index=cv2.flann_Index(s['co'].astype(np.float32),dict(algorithm=1,trees=4));nearest,dist2=index.knnSearch(f['co'].astype(np.float32),1,params=dict(checks=128));mapping=nearest[:,0]
def corner_records(data,canonical):
 co=canonical[data['loopverts']];uv=data['uv'];materials=np.repeat(data['material'],data['counts'])
 # Canonical source coordinates remove only verified FBX float position error.
 values=np.column_stack((np.rint(co*1e5).astype(np.int64),np.rint(uv*1e7).astype(np.int64),materials))
 return np.unique(values,axis=0)
expected=corner_records(s,s['co']);actual=corner_records(f,s['co'][mapping]);equal=expected.shape==actual.shape and np.array_equal(expected,actual)
report={'independent':True,'read_only':True,'source_extraction':a.source,'fresh_extraction':a.fresh,'packet':a.packet,'packet_sha256':hashlib.sha256(Path(a.packet).read_bytes()).hexdigest(),'fresh_nearest_source_max_units':float(np.sqrt(dist2.max())),'unique_source_position_uv_material_corners':len(expected),'unique_fresh_position_uv_material_corners':len(actual),'uv_quantization':1e-7,'coordinate_canonicalization_grid_source_units':1e-5,'gates':{'source_uv_bitexact_to_packet':np.array_equal(source_uv,packet['loop_uv']),'fresh_positions_within_1e_minus_3_cm':float(np.sqrt(dist2.max()))*100<=.001,'fresh_uv_material_corner_sets_equal':equal},'limitations':['Checks unique position/UV/material corner associations; triangulation and duplicate loop multiplicity may differ after FBX','Geometry, pose symmetry and rig are validated separately']}
report['uv_export_identity_pass']=all(report['gates'].values());out=Path(a.output);out.parent.mkdir(parents=True,exist_ok=True);out.write_text(json.dumps(report,indent=2));print(json.dumps(report))
