"""Cross-pose observed radiance completion on registered, actual mesh UV0.

Only Open-unknown LEFT posterior texels change. Closed-qualified texels seed
the same part; remaining holes are smooth continuation. L01 has no overlap:
its donor-posterior style is explicitly inferred, never target-pose truth.
"""
from pathlib import Path
import argparse
import copy
import hashlib
import json

import cv2
import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[3] / 'ArtSource/UI/MainMenu3D/20261015_refB_v1'
N = 2048
cv2.setNumThreads(2)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def load(path):
    return json.loads(Path(path).read_text(encoding='utf8'))


def mask(path):
    return np.asarray(Image.open(path)) > 0


def decode(raw):
    x = raw.astype(np.float32)/65535
    return np.where(x <= .04045, x/12.92, ((x+.055)/1.055)**2.4)


def encode(linear):
    x = np.clip(linear, 0, 1)
    return np.uint16(np.rint(np.where(x <= .0031308, x*12.92, 1.055*np.maximum(x, 0)**(1/2.4)-.055)*65535))


def bounds(rect):
    u0, v0, u1, v1 = rect
    return max(0, int(np.floor(u0*N))), max(0, int(np.floor((1-v1)*N))), min(N, int(np.ceil(u1*N))), min(N, int(np.ceil((1-v0)*N)))


def solve(values, known, domain, initial):
    """Coarse-to-fine same-part harmonic continuation, exact known boundary."""
    kernel = np.array([[0, 1, 0], [1, 0, 1], [0, 1, 0]], np.float32)
    field = None
    for divisor, count in ((8, 500), (4, 300), (2, 240), (1, 220)):
        size = (max(2, int(np.ceil(domain.shape[1]/divisor))), max(2, int(np.ceil(domain.shape[0]/divisor))))
        if divisor == 1:
            sd, sk, observed = domain, known, values
        else:
            sd = cv2.resize(domain.astype(np.float32), size, interpolation=cv2.INTER_AREA) > .05
            coverage = cv2.resize(known.astype(np.float32), size, interpolation=cv2.INTER_AREA)
            sk = coverage > .65
            observed = cv2.resize(values*known[..., None], size, interpolation=cv2.INTER_AREA)/np.maximum(coverage[..., None], 1e-6)
        field = cv2.resize(initial if field is None else field, size, interpolation=cv2.INTER_LINEAR)
        field[sk] = observed[sk]
        denominator = cv2.filter2D(sd.astype(np.float32), -1, kernel, borderType=cv2.BORDER_CONSTANT)
        unknown = sd & ~sk
        for _ in range(count):
            average = cv2.filter2D(field*sd[..., None], -1, kernel, borderType=cv2.BORDER_CONSTANT)/np.maximum(denominator[..., None], 1e-6)
            field[unknown] = average[unknown]
    field[known] = values[known]
    return field


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--open', required=True)
    parser.add_argument('--closed', required=True)
    parser.add_argument('--out', required=True)
    args = parser.parse_args()
    open_path, closed_path, out = map(lambda x: Path(x).resolve(), (args.open, args.closed, args.out))
    if not out.is_relative_to(ROOT / 'Textures/Revisions') or (out.exists() and any(out.iterdir())):
        raise RuntimeError('Fresh task-owned revision required')
    out.mkdir(parents=True, exist_ok=True)
    opened, closed = load(open_path), load(closed_path)
    source_path = Path(opened['materials']['MC_Enamel']['radiance_not_albedo'])
    closed_image = Path(closed['materials']['MC_Enamel']['radiance_not_albedo'])
    original_raw = cv2.imread(str(source_path), cv2.IMREAD_UNCHANGED)[..., ::-1]
    old = decode(original_raw)
    closed_rgb = decode(cv2.imread(str(closed_image), cv2.IMREAD_UNCHANGED)[..., ::-1])
    ok = mask(source_path.parent / 'MC_Enamel_ObservedRadianceConfidence.png')
    visible = mask(source_path.parent / 'MC_Enamel_ActualOpenVisibleSupport.png')
    ck = mask(closed_image.parent / 'MC_Enamel_ClosedSourceKnown.png')
    if not all(a.shape == (N, N) for a in (ok, visible, ck)):
        raise RuntimeError('Registered 2K unique UV0 resources required')
    parts = {p['part']: p for p in load(ROOT / 'Jaws/ContourHero_v13/Full32/uv_manifest.json')['parts']}
    changed = np.zeros((N, N), bool)
    direct = changed.copy()
    continuation = changed.copy()
    inferred_style = changed.copy()
    revised = old.copy()
    records = []
    donor_fields = []
    for donor in ('Upper_Posterior_L02', 'Upper_Posterior_L03'):
        xa, ya, xb, yb = bounds(parts[donor]['front_uv_rect'])
        q = ck[ya:yb, xa:xb]
        values = closed_rgb[ya:yb, xa:xb]
        if q.sum() < 64:
            raise RuntimeError('Insufficient observed donor palette')
        initial = np.broadcast_to(np.median(values[q], axis=0), values.shape).copy()
        field = solve(values, q, np.ones(q.shape, bool), initial)
        # Smooth form/color is borrowed; a bounded, observed aperiodic RGB fine
        # field remains RADIANCE only, never physical height/normal inference.
        low = cv2.GaussianBlur(field, (0, 0), 3.2)
        fine = np.clip(field-cv2.GaussianBlur(field, (0, 0), .65), -.006, .006)
        donor_fields.append(np.clip(low+fine*.5, 0, 1))
    for name in ('Upper_Posterior_L01', 'Upper_Posterior_L02', 'Upper_Posterior_L03', 'Upper_Posterior_L04'):
        xa, ya, xb, yb = bounds(parts[name]['front_uv_rect'])
        region = np.s_[ya:yb, xa:xb]
        domain, known_open = visible[region], ok[region]
        unknown = domain & ~known_open
        if not unknown.any():
            continue
        field_open = old[region]
        known_closed = cv2.erode(ck[region].astype(np.uint8), np.ones((3, 3), np.uint8)).astype(bool) & unknown
        distance = cv2.distanceTransform((~known_open).astype(np.uint8), cv2.DIST_L2, 5)
        t = np.clip(distance/14., 0, 1)
        feather = t*t*(3-2*t)
        if known_closed.any():
            constraints = field_open.copy()
            mixed = field_open*(1-feather[..., None])+closed_rgb[region]*feather[..., None]
            constraints[known_closed] = mixed[known_closed]
            completed = solve(constraints, known_open | known_closed, domain, field_open)
            direct[region][known_closed] = True
            continuation[region][unknown & ~known_closed] = True
            provenance = 'Same-part CLOSED-qualified observations on registered UV0; target Open lighting inferred. Remaining holes use same-part harmonic continuation.'
        else:
            if name != 'Upper_Posterior_L01':
                raise RuntimeError('Unexpected zero Closed overlap: ' + name)
            size = (xb-xa, yb-ya)
            donor = (cv2.resize(donor_fields[0], size, interpolation=cv2.INTER_LINEAR)*.45 +
                     cv2.resize(donor_fields[1], size, interpolation=cv2.INTER_LINEAR)*.55)
            # Same cervical-to-incisal direction in the registered front cells.
            completed = field_open*(1-feather[..., None])+donor*feather[..., None]
            inferred_style[region][unknown] = True
            provenance = 'INFERRED L01 posterior-enamel continuation from observed Closed L02/L03; UV-cell cervical/incisal alignment, smooth form plus bounded source-derived RGB grain. No L01 Closed truth exists.'
        revised[region][unknown] = completed[unknown]
        changed[region][unknown] = True
        records.append({'part': name, 'Open_unknown_visible_texels': int(unknown.sum()), 'Closed_qualified_seed_texels': int(known_closed.sum()),
                        'Open_known_texels_preserved': int(known_open.sum()), 'provenance': provenance,
                        'target_pose_lighting': 'INFERRED, not a measured Open observation'})
    raw = encode(revised)
    # Hard byte-preservation gate: even float encode roundoff is not permitted
    # to alter any observed Open texel or any unrequested region/material.
    raw[~changed] = original_raw[~changed]
    raw[ok] = original_raw[ok]
    assert np.array_equal(raw[ok], original_raw[ok])
    assert np.array_equal(raw[~changed], original_raw[~changed])
    material_folder = out / 'MC_Enamel'
    material_folder.mkdir()
    path = material_folder / 'MC_Enamel_Open_Radiance_NOT_ALBEDO_CrossPoseCompleted_16bit.png'
    if not cv2.imwrite(str(path), raw[..., ::-1]):
        raise RuntimeError('Could not save new radiance atlas')
    for label, value in [('Changed_OpenUnknown_Only', changed), ('CrossPose_ObservedClosedSeed', direct),
                          ('SamePart_HarmonicContinuation_Inferred', continuation), ('L01_AdjacentStyle_Inferred', inferred_style)]:
        Image.fromarray(np.uint8(value)*255).save(material_folder / f'{label}.png')
    result = copy.deepcopy(opened)
    result['status'] = 'OPEN_BAKED_RADIANCE_CROSS_POSE_POSTERIOR_COMPLETION_PENDING_ACTUAL_REVIEW'
    result['materials']['MC_Enamel']['radiance_not_albedo'] = str(path)
    result['materials']['MC_Enamel']['provenance'] = 'All observed Open texels preserved byte-exact. Closed observations seed only target-unknown same-part UV0; target-pose lighting inferred. L01 is explicitly neighboring posterior style inference.'
    result['limits'].append('Opaque-tab hidden posterior completion is cross-pose/inferred appearance, not a 1:1 observed Open match.')
    binding_path = out / 'material_binding_open_Radiance_NOT_ALBEDO_CrossPose.json'
    binding_path.write_text(json.dumps(result, indent=2), encoding='utf8')
    # First-hit CPU proof uses EXACT same actual packet/camera as the source
    # branch; this checks registration, not native shader or full acceptance.
    transfer = load(open_path.parent / 'open_radiance_transfer_receipt.json')
    packet = np.load(open_path.parent / 'actual_open_camera_first_hit_UV0.npz')
    uv, owner = packet['UV0'], packet['object_id']
    sx, sy = (uv[..., 0]*N-.5).astype(np.float32), ((1-uv[..., 1])*N-.5).astype(np.float32)
    rgb = decode(raw)
    sample = cv2.remap(rgb, sx, sy, cv2.INTER_LINEAR)
    prior = np.asarray(Image.open(open_path.parent / 'open_first_hit_UV0_Radiance_NOT_ALBEDO_CPU.png').convert('RGB'))
    object_ids = [transfer['objects'].index(parts[name]['object']) for name in ('Upper_Posterior_L01', 'Upper_Posterior_L02', 'Upper_Posterior_L03', 'Upper_Posterior_L04')]
    selected = np.isin(owner, object_ids)
    proof = prior.copy()
    proof[selected] = np.uint8(np.rint(encode(sample)[selected]/257.))
    Image.fromarray(proof).save(out / 'open_first_hit_UV0_CrossPosePosterior_CPU.png')
    report = {'status': result['status'], 'open_binding': str(open_path), 'open_binding_sha256': sha(open_path),
        'closed_binding': str(closed_path), 'closed_binding_sha256': sha(closed_path), 'geometry_packet_sha256': transfer['packet_sha256'],
        'Open_known_texels_byte_exact': True, 'all_unrequested_texels_byte_exact': True, 'other_material_paths_unchanged': True,
        'parts': records, 'changed_texels': int(changed.sum()), 'Closed_cross_pose_observed_seed_texels': int(direct.sum()),
        'DCC_Painter_GPU_used': False, 'binding': str(binding_path),
        'limits': ['Cross-pose appearance constrains color/style; Open lighting in the hidden area is inferred.',
                   'No target-pose 1:1 claim for regions absent from the source.', 'Native/frame and pose-transition review required.']}
    (out / 'cross_pose_posterior_completion_receipt.json').write_text(json.dumps(report, indent=2), encoding='utf8')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
