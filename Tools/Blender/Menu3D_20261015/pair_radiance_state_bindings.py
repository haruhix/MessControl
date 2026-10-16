"""Package observed endpoint radiance bindings without duplicating textures."""
from pathlib import Path
import argparse
import copy
import hashlib
import json


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--open', required=True)
    parser.add_argument('--closed', required=True)
    parser.add_argument('--out', required=True)
    args = parser.parse_args()
    open_path, closed_path, out = map(lambda value: Path(value).resolve(), (args.open, args.closed, args.out))
    if out.exists():
        raise RuntimeError('Preserve existing binding; fresh output required')
    opened, closed = [json.loads(path.read_text(encoding='utf8')) for path in (open_path, closed_path)]
    expected = {'MC_Enamel', 'MC_Gum', 'MC_Interior', 'MC_Tongue'}
    if set(opened['materials']) != expected:
        raise RuntimeError('Open radiance requires four unique UV0 materials')
    result = copy.deepcopy(closed)
    for material in expected:
        if material not in result['materials']:
            result['materials'][material] = copy.deepcopy(opened['materials'][material])
            result['materials'][material]['closed_state_policy'] = 'OPEN_RADIANCE_CARRY_FORWARD: closed cavity hidden/unobserved; no closed-source claim.'
    for material, values in result['materials'].items():
        if 'radiance_not_albedo' not in values or 'base_color' in values:
            raise RuntimeError('Dedicated radiance-only channels required: ' + material)
    result['status'] = 'PAIRED_ENDPOINT_BAKED_RADIANCE_NOT_ALBEDO_PENDING_NATIVE_REVIEW'
    result['endpoint_bindings'] = {'open': str(open_path), 'open_sha256': sha(open_path),
                                   'closed_observed': str(closed_path), 'closed_sha256': sha(closed_path)}
    result['limits'].append('Closed Interior/Tongue are unobserved: their Open radiance carries forward on the same real surfaces; geometry occlusion hides the cavity.')
    out.write_text(json.dumps(result, indent=2), encoding='utf8')
    print(json.dumps({'binding': str(out), 'materials': sorted(result['materials']), 'images_duplicated': False}, indent=2))


if __name__ == '__main__':
    main()
