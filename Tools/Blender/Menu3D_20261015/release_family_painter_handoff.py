"""Freeze a reviewed per-day Painter texture handoff without touching Unreal."""
import argparse
import hashlib
import json
from pathlib import Path
from PIL import Image


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--handoff', type=Path, required=True)
    parser.add_argument('--day', required=True)
    args = parser.parse_args()
    out = args.handoff.resolve() / 'Textures'
    binding_path = out / 'shader_binding.json'
    binding = json.loads(binding_path.read_text())
    review = out / 'painter_viewport_qa.json'
    if not json.loads(review.read_text())['actual_visual_review_performed']:
        raise RuntimeError('Actual Painter visual inspection must precede release.')
    for maps in binding['map_metadata'].values():
        for metadata in maps.values():
            path = Path(metadata['path'])
            if sha(path) != metadata['sha256'] or Image.open(path).size != (4096, 4096):
                raise RuntimeError(f'Changed physical map: {path}')
    saved = json.loads((out / 'painter_saved_project.json').read_text())
    spp = Path(saved.get('path', saved.get('project')))
    if sha(spp) != saved['sha256']:
        raise RuntimeError('Editable Painter project changed after the saved receipt.')
    binding.update(day=args.day, status='FROZEN_PAINTER_RELEASE_NATIVE_UNREAL_VISUAL_PENDING',
                   selected_pbr_revision='01', selected_mesh_map_revision='01',
                   editable_project={'path': str(spp), 'sha256': sha(spp), 'bytes': spp.stat().st_size},
                   viewport_review=str(review), staged_mtl_normalization=str(out / 'staged_mtl_normalization.json'))
    binding['additional_authored_masks'] = {
        name: {'ThinTissueMask': {'path': str(out / 'AnatomyMasks' / (name + '_ThinTissueMask.png')),
                                 'sha256': sha(out / 'AnatomyMasks' / (name + '_ThinTissueMask.png')),
                                 'srgb': False, 'semantics': 'Artist anatomy mask, not metric thickness'}}
        for name in ['MC_Enamel', 'MC_Gum']}
    binding_path.write_text(json.dumps(binding, indent=2))
    release = {'status': 'FROZEN_RELEASED_TO_ROOT', 'day': args.day,
               'binding': str(binding_path), 'binding_sha256': sha(binding_path),
               'editable_project': binding['editable_project'],
               'native_visual_approval': 'PENDING_ROOT_UNREAL_GATE',
               'mutations_after_release': 'Use an explicit new revision, never silently change selected maps.'}
    (out / 'release_status.json').write_text(json.dumps(release, indent=2))
    print(json.dumps(release, indent=2))


if __name__ == '__main__':
    main()
