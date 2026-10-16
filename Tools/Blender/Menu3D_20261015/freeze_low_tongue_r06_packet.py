"""Freeze only the validated producer packet; no application edits or rebuild."""
from pathlib import Path
import hashlib
import json
import numpy as np

P = Path('E:/DEVGAME/MessControl/ArtSource/UI/MainMenu3D/20261015_refB_v1/Assembly/UnrealHandoff/v08_ReferenceFinish/Interior/UV_r06_Refine03_Candidate01')


def read(n):
    return json.loads((P/n).read_text())


def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


assert not (P/'release_manifest.json').exists()
geometry = read('QA_Independent/source_fresh_static_geometry.json')
uv = read('QA_Independent/uv_validation.json')
identity = read('QA_Independent/export_identity_and_priority.json')
raw = read('QA_Independent/raw_fbx_units.json')
repair = read('uv_repair_report.json')
assert geometry['geometry_export_pass'] and all(uv['gates'].values())
assert identity['uv_export_normal_identity_pass'] and raw['pass']
assert read('visual_gate.json')['pass']
assert uv['charts_per_part'] == {'SM_DAY03_Interior':3,'SM_DAY03_Tongue':2}
dorsal = next(c for c in identity['charts'] if c['surface'] == 'dorsal')
assert dorsal['kappa_p95'] <= 1.3 and dorsal['density_intrachart_spread'] <= 1.3 and dorsal['angle_error_degrees_p95'] <= 15
manifest = read('interior_handoff.json')
manifest['status'] = 'FROZEN_NEXT_NATIVE_CANDIDATE: geometry/UV/export gates PASS; tongue Painter rebake and actual native review pending'
manifest['fresh_import_validation'] = {'geometry': 'QA_Independent/source_fresh_static_geometry.json','UV_normal_corner_identity':'QA_Independent/export_identity_and_priority.json','raw_units':'QA_Independent/raw_fbx_units.json'}
(P/'interior_handoff.json').write_text(json.dumps(manifest,indent=2))
files = ['MC_DAY03_InteriorSource.blend','SM_DAY03_Interior.fbx','SM_DAY03_Tongue.fbx','MC_DAY03_Interior_Painter_Open.obj','MC_DAY03_Interior_Painter_Open.mtl','texture_mesh_packet.npz','dorsal_uv_transfer.npz','interior_handoff.json']
hashes = {n:sha(P/n) for n in files}
old = read('producer_handoff_receipt.json')
assert all(sha(Path(manifest['unchanged_cavity_parent'])/name) == digest for name,digest in manifest['frozen_r05_hashes'].items())
receipt = {'status':'FROZEN_EXPORT_GATE_PASS_PENDING_TONGUE_PBR_REBAKE','files':hashes,
           'approved_geometry_exact':True,'frozen_parents_unchanged':True,'cavity_fbx_byteexact':True,
           'cavity_parent_sha256':hashes['SM_DAY03_Interior.fbx'],'fresh_import_geometry_uv_normals_pass':True,
           'texture_rebake_required':True}
(P/'producer_handoff_receipt.json').write_text(json.dumps(receipt,indent=2))
transfer = np.load(P/'dorsal_uv_transfer.npz')
mask = transfer['dorsal_loop_mask']
positions = transfer['vertices'][transfer['loop_vertex_indices']][mask]
uvs = transfer['new_loop_uv'][mask]
axes = {'dorsal_U_vs_source_X_correlation':float(np.corrcoef(uvs[:,0],positions[:,0])[0,1]),
        'dorsal_V_vs_source_minusY_correlation':float(np.corrcoef(uvs[:,1],-positions[:,1])[0,1]),
        'policy':'Orientation matched to r05 by proper rigid UV rotation; no reflection or nonuniform UV squeeze. Ventral/cavity UV exact.'}
release = {
    'status':'FROZEN_NEXT_NATIVE_CANDIDATE_TONGUE_PBR_REBAKE_REQUIRED',
    'approval_scope':'Root selected Refine03 form for UV/export/native candidate. This release is not final 1:1/look approval.',
    'packet_root':str(P),'files':hashes,'producer_receipt_sha256':sha(P/'producer_handoff_receipt.json'),
    'packet_content_sha256':hashlib.sha256(json.dumps(hashes,sort_keys=True,separators=(',',':')).encode()).hexdigest(),
    'parent_approved_shape_source':manifest['parent_source'],'parent_approved_shape_sha256':manifest['parent_source_sha256'],
    'cavity_unchanged':{'parent':manifest['unchanged_cavity_parent'],'geometry_uv_byteexact':True,'fbx_byteexact':True,'fbx_sha256':hashes['SM_DAY03_Interior.fbx'],'PBR_maps_can_reuse':'Cavity unchanged; preserve existing cavity UV and maps. Only tongue authoring/reprojection/rebake required.'},
    'texture_sets':[{'index':0,'material':'MC_Interior','object':'SM_DAY03_Interior','vertex_count':16386,'triangles':32768,'UV_charts':3},
                    {'index':1,'material':'MC_Tongue','object':'SM_DAY03_Tongue','vertex_count':9090,'triangles':18176,'UV_charts':2}],
    'tongue_uv':dorsal,'uv_axes':axes,
    'checks':{'fresh_import_geometry':geometry['geometry_export_pass'],'geometry_same_as_approved_shape':geometry['gates']['uv_only_parent_geometry_bitexact'],'UV_raster_gates':uv['gates'],'OBJ_FBX_corner_UV_material_normal_identity':identity['uv_export_normal_identity_pass'],'raw_FBX_unit_node_parse':raw['pass'],'actual_checker_layout_review':True},
    'units':raw['contract'],
    'character_anchor_source':manifest['character_contact_anchor'],'fresh_anchor_error_cm':abs(geometry['character_anchor']['height_error_source_units'])*100,
    'backend':{'preferred':'Blender MCP','read_only_connection_check_tool':'mcp__blender__get_blendfile_summary_datablocks','live_result':'Cannot connect to Blender at127.0.0.1:9876; MCP add-on server unavailable','fallback':'Explicitly reported to root; isolated D:/BLENDER_TEST/blender.exe5.1.0 --background --factory-startup --python-exit-code1. Unrelated GUI and Painter/Unreal were not touched.'},
    'validation_provenance':'Producer used a separate fresh Blender process for import and independent corner/UV/unit methods. Directory QA_Independent follows existing project helper naming; this does not claim a different human/agent reviewed it.',
    'deferred':['Painter MCP tongue paint reprojection and normal/AO/thickness/curvature rebake','Root actual Unreal import/camera/9pose/character motion review','Final reference look approval']
}
(P/'release_manifest.json').write_text(json.dumps(release,indent=2))
print(json.dumps({'release_manifest':str(P/'release_manifest.json'),'release_manifest_sha256':sha(P/'release_manifest.json'),'packet_content_sha256':release['packet_content_sha256'],'files':hashes,'uv_axes':axes,'fresh_anchor_error_cm':release['fresh_anchor_error_cm']}))
