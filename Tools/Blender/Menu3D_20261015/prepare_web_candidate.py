"""Link an internal GLB candidate to the local review route only.

The default menu-assets.json and final Exports are never changed. URLs are
relative to the task asset root served by Assembly/Web/server.py.
"""
import argparse
import json
import os
from pathlib import Path
from urllib.parse import quote

ROOT = Path(r'E:\DEVGAME\MessControl\ArtSource\UI\MainMenu3D\20261015_refB_v1').resolve()
WEB = ROOT / 'Assembly/Web'
parser = argparse.ArgumentParser()
parser.add_argument('--candidate-manifest', required=True)
opt = parser.parse_args()
path = Path(opt.candidate_manifest).resolve()
if not path.is_relative_to(ROOT / 'Assembly/CandidateExports'):
    raise RuntimeError('Expected a task-owned internal candidate manifest')
candidate = json.loads(path.read_text(encoding='utf8'))
if not candidate['source_unchanged']:
    raise RuntimeError('Candidate export modified its source')
entry = candidate['entry'].copy()

def local_url(value):
    target = Path(value).resolve()
    if not target.is_relative_to(ROOT) or not target.is_file():
        raise RuntimeError(f'Missing/outside task asset: {target}')
    return quote(Path(os.path.relpath(target, WEB)).as_posix(), safe='/')

entry['url'] = local_url(entry['url'])
entry['look'] = local_url(entry['look'])
for state in entry.get('textureStates', []):
    state['closed'] = local_url(state['closed'])
    state['open'] = local_url(state['open'])
manifest = {
    'schema': 'messcontrol.menu3d.v1',
    'status': 'internal-candidate-review',
    'heroDay': entry['id'],
    'candidateSource': local_url(path),
    'days': [entry],
}
destination = WEB / 'menu-assets.internal.json'
destination.write_text(json.dumps(manifest, indent=2), encoding='utf8')
print(destination)
print('http://127.0.0.1:8178/Assembly/Web/?review=1')
