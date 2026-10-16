"""CPU-only receiver partition for a bounded source-fit reflection test."""
import copy
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3] / 'ArtSource/UI/MainMenu3D/20261015_refB_v1'
OUT = ROOT / 'Assembly/Review/ReflectionProbes_v22'
OUT.mkdir(parents=True, exist_ok=True)
basis = ROOT / 'Assembly/Review/ReflectionProbes_v21/B_portrait_Zplus2/look_profile.json'
manifest_path = ROOT / 'Jaws/ContourHero_v13/Full32/uv_manifest.json'
profile = json.loads(basis.read_text())
parts = json.loads(manifest_path.read_text())['parts']
middle = [p['object'] for p in parts[:6] if 'Fang' not in p['part']]
fangs = [p['object'] for p in parts[:6] if 'Fang' in p['part']]
others = [p['object'] for p in parts if p['object'] not in middle + fangs]
if len(middle) != 4 or len(fangs) != 2 or len(others) != 26:
    raise RuntimeError('Unexpected Full32 semantic receiver inventory')
baseline = next(p for p in profile['lights'] if p['name'] == 'Menu_Crown_ReflectionCard')
baseline['receiver_objects'] = others
baseline['receiver_materials'] = ['MC_Gum']
for name, position, receivers in [
    ('Menu_MiddleFour_ReflectionCard', [4., 0., 10.], middle),
    ('Menu_TwoFangs_ReflectionCard', [3., -5., 10.], fangs),
]:
    card = copy.deepcopy(baseline)
    card['name'] = name
    card['position'] = position
    card['target'] = [position[0] - 5., position[1] + 7., position[2] - 4.5]
    card['receiver_objects'] = receivers
    card['receiver_materials'] = []
    profile['lights'].append(card)
profile['status'] = 'CPU_PREPARED_RECEIVER_SPECIFIC_REFLECTION_TEST_NOT_RENDERED_OR_ACCEPTED'
profile['notes'] = ('Middle four and two fangs have separate exact-name glossy receivers. '
                    'All other enamel plus Gum retain B+2 baseline. No double glossy coverage; '
                    'all three cards have the same B orientation,0.7x8 area and466.6667W energy.')
(OUT / 'source_receiver_split.json').write_text(json.dumps(profile, indent=2), encoding='utf8')
coverage = {}
for part in parts:
    name = part['object']
    matched = [p['name'] for p in profile['lights'] if p.get('diffuse_factor', 1) == 0
               and name in p.get('receiver_objects', [])]
    if len(matched) != 1:
        raise RuntimeError('Overlapping or missing enamel glossy receiver: ' + name)
    coverage[name] = matched
receipt = {'status': 'CPU_PREPARED_NOT_RENDERED', 'basis': str(basis),
           'basis_sha256': hashlib.sha256(basis.read_bytes()).hexdigest(),
           'uv_manifest': str(manifest_path),
           'uv_manifest_sha256': hashlib.sha256(manifest_path.read_bytes()).hexdigest(),
           'middle_four': middle, 'fangs': fangs, 'baseline_other_enamel': others,
           'logical_light_count': len(profile['lights']),
           'glossy_light_count_per_enamel_object': coverage,
           'normal_orientation_offset': [-5, 7, -4.5],
           'emitter_radiance_w_sr_m2': 26.525823848649225,
           'invariants': ['No DCC execution in this preparation', 'No mesh changes',
                          'Diffuse profile unchanged', 'Gum and other enamel retain baseline glossy card']}
(OUT / 'receiver_profile_preparation.json').write_text(json.dumps(receipt, indent=2), encoding='utf8')
print(json.dumps({'profile': str(OUT / 'source_receiver_split.json'),
                  'middle': middle, 'fangs': fangs, 'coverage': '32 enamel objects exactly one glossy card each'}, indent=2))
