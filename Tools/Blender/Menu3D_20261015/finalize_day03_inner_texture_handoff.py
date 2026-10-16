"""Verify Painter exports and publish an explicit physical-map Unreal handoff."""
import argparse
import hashlib
import json
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[3]
HANDOFF = ROOT / 'ArtSource/UI/MainMenu3D/20261015_refB_v1/Assembly/UnrealHandoff/v08_ReferenceFinish/Interior/UV_Organic_r05_Candidate01'
OUT = HANDOFF / 'Textures'


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    global HANDOFF, OUT
    p = argparse.ArgumentParser()
    p.add_argument('--revision', default='01')
    p.add_argument('--mesh-map-revision', default='01')
    p.add_argument('--handoff', type=Path, default=HANDOFF)
    args = p.parse_args()
    HANDOFF = args.handoff.resolve(); OUT = HANDOFF / 'Textures'
    pbr = OUT / ('PainterPBR_' + args.revision)
    baked = OUT / ('PainterMeshMaps_' + args.mesh_map_revision)
    binding = {'schema': 'messcontrol.menu3d.physical_shader_binding.v1',
               'status': 'EXPORTED_PHYSICAL_MAPS_NATIVE_VISUAL_REVIEW_PENDING',
               'normal_format': 'DirectX', 'texture_resolution': 4096,
               'mesh_packet_sha256': sha(HANDOFF / 'texture_mesh_packet.npz'),
               'static_obj_sha256': sha(HANDOFF / 'MC_DAY03_Interior_Painter_Open.obj'),
               'native_fbx_sha256': {p.name: sha(p) for p in HANDOFF.glob('SM_DAY03_*.fbx')},
               'texture_sets': {}, 'map_metadata': {},
               'thickness_semantics': 'Black thin / white thick, inward-ray bake normalized per texture set; not metric thickness.',
               'thickness_reference': 'https://experienceleague.adobe.com/en/docs/substance-3d/bakers/bakers-settings/thickness-map-from-mesh',
               'lighting_baked_into_basecolor': False,
               'validation_scope': 'Files, UV source identity, channel ranges and tangent-normal statistics. Actual Unreal look and pose review belongs to the lead.'}
    report = {'schema': 'messcontrol.menu3d.texture_qa.v1', 'sets': {}, 'warnings': []}
    sheet = Image.new('RGB', (5 * 340, 2 * 390), '#17191d')
    draw = ImageDraw.Draw(sheet)
    for row, name in enumerate(['MC_Interior', 'MC_Tongue']):
        files = {
            'BaseColorTexture': pbr / (name + '_Base_color_1001.png'),
            'RoughnessTexture': pbr / (name + '_Specular_roughness_1001.png'),
            'NormalTexture': pbr / (name + '_Normal_DirectX_1001.png'),
            'AOTexture': pbr / (name + '_Mixed_AO_1001.png'),
            'ThicknessTexture': baked / (name + '_thickness.1001.png')}
        binding['texture_sets'][name] = {}
        binding['map_metadata'][name] = {}
        report['sets'][name] = {}
        coverage = np.asarray(Image.open(OUT / 'AnatomyMasks' / (name + '_Coverage.png'))) > 0
        for column, (role, path) in enumerate(files.items()):
            if not path.is_file():
                raise RuntimeError(f'Missing Painter export: {path}')
            image = Image.open(path)
            if image.size != (4096, 4096):
                raise RuntimeError(f'Wrong dimensions {image.size}: {path}')
            array = np.asarray(image.convert('RGB') if role in ['BaseColorTexture', 'NormalTexture'] else image.convert('L'), dtype=np.float32) / 255
            surface = array[coverage]
            stats = {'minimum': np.min(surface, axis=0).tolist(), 'maximum': np.max(surface, axis=0).tolist(),
                     'mean': np.mean(surface, axis=0, dtype=np.float64).tolist(),
                     'p05_p50_p95': np.percentile(surface, [5, 50, 95], axis=0).tolist(),
                     'standard_deviation': np.std(surface, axis=0, dtype=np.float64).tolist()}
            if role == 'NormalTexture':
                vectors = surface * 2 - 1
                length = np.linalg.norm(vectors, axis=1)
                angles = np.degrees(np.arccos(np.clip(vectors[:, 2] / np.maximum(length, 1e-8), -1, 1)))
                stats['normal_length_p01_p50_p99'] = np.percentile(length, [1, 50, 99]).tolist()
                stats['tangent_deflection_degrees_p50_p95_p99'] = np.percentile(angles, [50, 95, 99]).tolist()
                if np.percentile(angles, 99) > (12 if name == 'MC_Interior' else 18):
                    report['warnings'].append(f'{name} normal detail may be too strong; inspect grazing-light native render.')
            binding['texture_sets'][name][role] = str(path.resolve())
            binding['map_metadata'][name][role] = {
                'path': str(path.resolve()), 'sha256': sha(path), 'bytes': path.stat().st_size,
                'dimensions': list(image.size), 'srgb': role == 'BaseColorTexture',
                'compression': 'TC_NORMALMAP' if role == 'NormalTexture' else 'TC_DEFAULT' if role == 'BaseColorTexture' else 'TC_MASKS',
                'flip_green_channel': False if role == 'NormalTexture' else None}
            report['sets'][name][role] = stats
            preview = image.convert('RGB'); preview.thumbnail((328, 328), Image.Resampling.LANCZOS)
            x = column * 340 + 6; y = row * 390 + 48
            draw.text((x, row * 390 + 10), name + ' / ' + role.replace('Texture', ''), fill='#f4f0e6')
            sheet.paste(preview, (x, y))
    spp = OUT / 'MC_DAY03_Interior_Organic_r05.spp'
    if spp.is_file():
        binding['editable_project'] = {'path': str(spp.resolve()), 'sha256': sha(spp), 'bytes': spp.stat().st_size}
    probe = {'schema': 'painter_basecolor_semantics.v1',
             'api_documentation': 'Installed substance_painter/colormanagement.py Color: unspecified space is sRGB for managed channels and Raw otherwise.',
             'extra_gamma_conversion_applied': False, 'sets': {}}
    for name, intended in [('MC_Interior', [.55, .08, .12]), ('MC_Tongue', [.74, .12, .18])]:
        source = np.asarray(intended, np.float64)
        expected = np.rint(source * 255).astype(np.uint8)
        path = OUT / 'ColorProbe_BaseOnly' / (name + '_Base_color_1001.png')
        pixels = np.asarray(Image.open(path).convert('RGB'))
        error = np.abs(pixels.astype(np.int16) - expected).max()
        if error > 1:
            raise RuntimeError(f'Base-color export gamma probe failed for {name}: max RGB8 error {error}')
        probe['sets'][name] = {'intended_srgb': intended,
                             'expected_linear': np.where(source <= .04045, source / 12.92, ((source + .055) / 1.055) ** 2.4).tolist(),
                             'expected_rgb8': expected.tolist(), 'observed_mean_rgb8': pixels.mean(axis=(0, 1), dtype=np.float64).tolist(),
                             'maximum_rgb8_error': int(error), 'probe_sha256': sha(path)}
    (OUT / 'basecolor_color_semantics.json').write_text(json.dumps(probe, indent=2), encoding='utf8')
    binding['basecolor_color_semantics'] = str((OUT / 'basecolor_color_semantics.json').resolve())
    binding['texture_qa'] = str((OUT / 'texture_qa.json').resolve())
    binding['additional_authored_masks'] = {name: {role: {'path': str((OUT / 'AnatomyMasks' / (name + '_' + role + '.png')).resolve()), 'sha256': sha(OUT / 'AnatomyMasks' / (name + '_' + role + '.png')), 'srgb': False, 'semantics': 'Artist-authored anatomy support, not metric thickness'} for role in ['ThinTissueMask', 'ArtistDensityMask']} for name in ['MC_Interior', 'MC_Tongue']}
    (OUT / 'shader_binding.json').write_text(json.dumps(binding, indent=2), encoding='utf8')
    (OUT / 'texture_qa.json').write_text(json.dumps(report, indent=2), encoding='utf8')
    sheet.save(OUT / 'texture_atlas_contact_sheet.png')
    print(json.dumps({'binding': str((OUT / 'shader_binding.json').resolve()), 'warnings': report['warnings']}))


if __name__ == '__main__':
    main()
