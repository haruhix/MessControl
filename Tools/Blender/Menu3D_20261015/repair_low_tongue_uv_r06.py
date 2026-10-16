"""UV-only repair of approved Refine03 dorsal surface; no export at this stage."""
import bpy
import hashlib
import json
import sys
from pathlib import Path
import numpy as np

BASE = Path('E:/DEVGAME/MessControl/ArtSource/UI/MainMenu3D/20261015_refB_v1/Assembly/UnrealHandoff/v08_ReferenceFinish/Interior')
PARENT = BASE/'Organic_r06_LowTongue/Refine03'
OUT = BASE/'UV_r06_Refine03_Candidate01'
SOURCE = PARENT/'MC_DAY03_InteriorSource.blend'
sys.path.insert(0, str(Path(__file__).parent))
import refine_day03_low_tongue_r06 as shape
sys.path.insert(0, 'C:/Users/user/.codex/skills/blender-uv-baking/scripts')
import bx_uvbake as ub


def main():
    assert bpy.app.background and not OUT.exists()
    OUT.mkdir(parents=True)
    parent_sha = shape.sha(SOURCE)
    bpy.ops.wm.open_mainfile(filepath=str(SOURCE))
    cavity = bpy.data.objects['SM_DAY03_Interior']
    tongue = bpy.data.objects['SM_DAY03_Tongue']
    before = {o.name: shape.geometry_digest(o) for o in (cavity, tongue)}
    co = shape.coords(tongue).copy()
    original_uv = shape.uvcoords(tongue).copy()
    A = ub._Arrays(tongue, 'UV0')
    labels, _ = ub._uv_islands(A)
    charts = np.unique(labels)
    assert len(charts) == 2
    per_chart_z = {int(label): float(np.median(co[A.lv[(labels == label)[A.lf]], 2])) for label in charts}
    dorsal_label = max(per_chart_z, key=per_chart_z.get)
    face_mask = labels == dorsal_label
    loop_mask = face_mask[A.lf]
    old_dorsal_min = original_uv[loop_mask].min(0)
    old_dorsal_max = original_uv[loop_mask].max(0)
    A.free()
    ub.seams_from_uv_islands(tongue, uv_map='UV0')
    ub._set_pins(tongue.data, np.zeros(len(original_uv), bool), 'UV0')
    result = ub.unwrap(tongue, method='MINIMUM_STRETCH', uv_map='UV0',
                       faces=np.flatnonzero(face_mask), iterations=80)
    assert not result['folding_islands'] and not any(result['unsolved_faces'].values()), result
    uv = shape.uvcoords(tongue).copy()
    assert np.array_equal(original_uv[~loop_mask], uv[~loop_mask]), 'Ventral UV changed'
    # Rigid rotation only: align the new island to the old orientation by the
    # same loop correspondences; uniform scale fits the old reserved footprint.
    src = uv[loop_mask].astype(float)
    dst = original_uv[loop_mask].astype(float)
    src -= src.mean(0)
    dst -= dst.mean(0)
    U, singular, Vt = np.linalg.svd(src.T @ dst)
    rotate = U @ Vt
    if np.linalg.det(rotate) < 0:
        U[:, -1] *= -1
        rotate = U @ Vt
    points = src @ rotate
    center = (points.min(0)+points.max(0))*.5
    size = points.max(0)-points.min(0)
    target_size = old_dorsal_max-old_dorsal_min
    scale = float(np.min(target_size/size))
    points = (points-center)*scale+(old_dorsal_min+old_dorsal_max)*.5
    uv[loop_mask] = points
    ub._set_uv(tongue.data, uv, 'UV0')
    assert np.array_equal(shape.coords(tongue), co)
    assert shape.geometry_digest(cavity) == before[cavity.name]
    assert shape.geometry_digest(tongue)['co'] == before[tongue.name]['co']
    assert np.array_equal(shape.uvcoords(tongue)[~loop_mask], original_uv[~loop_mask])
    qa = ub.uv_qa(tongue, tex_res=4096, uv_map='UV0', analysis_res=2048)
    assert qa['islands'] == 2 and qa['zero_area_faces'] == 0
    assert qa['overlap_px_pct'] == 0 and qa['overlapping_island_pairs'] == 0 and not qa['self_overlapping_islands']
    assert qa['flipped_faces'] == 0 and qa['faces_crossing_tiles'] == 0 and qa['tiles'] == [1001]
    assert qa['angle_err_p95_deg'] <= 15
    assert shape.sha(SOURCE) == parent_sha
    source_out = OUT/'MC_DAY03_InteriorSource.blend'
    bpy.ops.wm.save_as_mainfile(filepath=str(source_out))
    np.savez_compressed(OUT/'dorsal_uv_transfer.npz',
                        loop_vertex_indices=np.array([l.vertex_index for l in tongue.data.loops], np.int32),
                        old_loop_uv=original_uv, new_loop_uv=shape.uvcoords(tongue),
                        dorsal_loop_mask=loop_mask, vertices=co)
    report = {'status': 'UV_CANDIDATE_ONLY: checker/layout inspection before exports',
              'parent_source': str(SOURCE), 'parent_source_sha256': parent_sha,
              'source': str(source_out), 'source_sha256': shape.sha(source_out),
              'geometry_bitexact_to_approved_refine03': True, 'cavity_geometry_and_uv_bitexact_r05': True,
              'ventral_uv_bitexact_r05': True, 'logical_charts': ['dorsal', 'ventral'],
              'method': 'MinimumStretch80 only dorsal faces, same equatorial seam; proper rotation and uniform scale into old dorsal reserved rectangle. No extra seams/packing/geometry edits.',
              'unwrap': result, 'tongue_uv_qa': qa,
              'before': before, 'after': {o.name: shape.geometry_digest(o) for o in (cavity, tongue)},
              'cavity': shape.audit(cavity), 'tongue': shape.audit(tongue),
              'character_contact_anchor': shape.ANCHOR.tolist(),
              'texture_reprojection': 'dorsal_uv_transfer.npz maps exact same vertex/loop topology. Cavity and ventral UV unchanged. New tongue normal/AO/thickness/curvature bake required; reproject dorsal paint to changed UV.'}
    (OUT/'uv_repair_report.json').write_text(json.dumps(report, indent=2))
    print('R06_DORSAL_UV_REPAIR', json.dumps(report))


if __name__ == '__main__':
    main()
