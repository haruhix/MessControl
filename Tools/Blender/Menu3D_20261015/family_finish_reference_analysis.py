"""Read-only DAY01/02 source analysis for an isolated reference-form revision."""
from pathlib import Path
import hashlib
import json
import numpy as np
from PIL import Image

BASE = Path(__file__).resolve().parents[3] / 'ArtSource/UI/MainMenu3D/20261015_refB_v1'
SRC = BASE / 'Assembly/UnrealHandoff/FunctionalBlocking_DAY01_DAY02_v01'
OUT = BASE / 'Assembly/UnrealHandoff/v09_FamilyReferenceForms'
OUT.mkdir(parents=True, exist_ok=True)
ref = BASE / 'References/B_FullWidth_Approved.png'
legacy = json.loads((SRC/'functional_blocking_family_handoff.json').read_text())
poses = np.load(SRC/'source_world_poses_BlenderUnits.npz')
transforms = {}
for variant in legacy['variants']:
    day = variant['day']
    transforms[day] = {}
    for amount in (1., 0., .25, .5, .75):
        row_record = {}
        for row in ('Upper', 'Lower'):
            parts = [p for p in variant['parts'] if f'_{row}_' in p['mesh']]
            ids = np.concatenate([np.arange(p['vertex_start'], p['vertex_start']+p['vertex_count']) for p in parts])
            b, a = poses[day+'__1.0'][ids].astype(float), poses[day+'__'+str(amount)][ids].astype(float)
            bc, ac = b.mean(0), a.mean(0)
            u, _, vt = np.linalg.svd((b-bc).T@(a-ac))
            r = u@vt
            error = np.linalg.norm((b-bc)@r+ac-a, axis=1).max()
            assert np.linalg.det(r) > .99999 and error < 3e-5
            row_record[row] = dict(rotation=r.tolist(), translation=(ac-bc@r).tolist(),
                                   max_rigid_fit_error_source_units=float(error))
        transforms[day][str(amount)] = row_record
(OUT/'legacy_row_transforms.json').write_text(json.dumps(transforms, indent=2))
manifest = {
    'status': 'REFERENCE_FORMS_IN_PROGRESS_NO_UV_OR_FINAL_EXPORT',
    'reference': {'path': str(ref), 'sha256': hashlib.sha256(ref.read_bytes()).hexdigest(),
                  'size': list(Image.open(ref).size), 'classification': 'AI concept, two compositions; not calibrated orthographic or 16:9 views'},
    'source_priority': ['Later user requests: symmetry, paired midlines, readable lower row and real native 3D opening',
                        'Canonical concept visible primary silhouette and dental style',
                        'Existing accepted native rigid row motion and uniform scale/axes'],
    'day01': {'observed': ['Ten broad visible upper crowns in closed view', 'Two broad central incisors, rounded shoulders/corners, moderate cervical taper',
                           'Front crowns visibly convex, warm ivory, seated in glossy scalloped coral gingiva'],
              'inferred': ['Lower/open/lingual/occlusal anatomy is not provided by this concept',
                           'Posterior U-arch continuation has true depth and turns behind the five visible hemiarch crowns'],
              'proposal': '16 upper +16 lower mirrored crowns; five dominant visible upper pairs and three inferred posterior pairs',
              'legacy': '20 teeth total, random asymmetric primitives; retained only for rigid row transforms'},
    'day02': {'observed': ['Two interlocking rows of broad pointed teeth', 'Convex shoulders and facial bodies; tips curve and terminate softly, not planar pyramids',
                           'Soft scalloped gingiva wrapping around the roots'],
              'inferred': ['Paired central seams override the AI concept odd-number central upper blade',
                           'Two posterior pairs per row turn behind six front pairs to give side depth'],
              'proposal': '16 upper +16 lower mirrored crowns; six dominant front pairs plus two inferred posterior pairs',
              'legacy': '21 blades, including one central upper blade; must not retain this center defect'},
    'contract': {'native_front': '+Y', 'up': '+Z', 'scale_policy': 'Source units unchanged; FBX metadata converts to cm, no mesh X stretch',
                 'materials': ['MC_Enamel', 'MC_Gum'], 'rig': 'one root, Basis open +Pose_000/Pose_025/Pose_050/Pose_075',
                 'shape_review': ['fixed front closed', 'fixed front open', 'oblique closed/open', 'center and root-seat crops'],
                 'final_uv': 'Deferred until root shape review; seam-driven coherent hero surface, separate lingual/root caps, atlas/checker/stretch/density gates',
                 'final_export': 'Withheld until shape review; fresh FBX validation and nine pose invariants are later gates'},
    'MCP': 'get_objects_summary failed: cannot connect to Blender 127.0.0.1:9876; isolated headless Blender 5.1.0 fallback',
    'legacy_source_sha256': {v['day']: hashlib.sha256(Path(v['blend']).read_bytes()).hexdigest() for v in legacy['variants']},
}
(OUT/'reference_manifest.json').write_text(json.dumps(manifest, indent=2))
(OUT/'work_state.md').write_text('Stage: source/reference analysis complete; primary/secondary form proposal next.\n\n'
    'No UE, Painter, Git, common source, v08 or GUI edits. Legacy source only supplies native rigid row motion.\n'
    'Geometry and readable source front/oblique clay proofs precede UV/FBX.\n')
print(json.dumps({'output': str(OUT), 'reference_sha256': manifest['reference']['sha256'],
                  'max_rigid_fit_error': max(r['max_rigid_fit_error_source_units'] for d in transforms.values() for p in d.values() for r in p.values())}, indent=2))
