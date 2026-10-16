"""Validate the frozen Painter handoff before preparing the native import request.

This host-side helper does not write Unreal packages or modify source art.
"""
import hashlib
import json
from pathlib import Path

from PIL import Image


def main():
    root = Path(__file__).resolve().parents[3]
    candidate = 'Candidates/Final96_GumMinStretch_LocalFangRepair'
    handoff = root / 'ArtSource/UI/MainMenu3D/20261015_refB_v1/Assembly/UnrealHandoff/v08b_SeamUV' / candidate
    manifest_path = handoff / 'Textures/shader_binding.json'
    manifest = json.loads(manifest_path.read_text(encoding='utf8'))
    assert manifest['normal_format'] == 'DirectX'
    assert manifest['texture_resolution'] == 4096
    expected_fbx = '41f93635dbf1b44513e0df8f8066f4b9a4f9a724641f112ae1a6132bc8e805f1'
    assert manifest['native_fbx_sha256'] == expected_fbx
    assert hashlib.sha256((handoff / 'SK_DAY03.fbx').read_bytes()).hexdigest() == expected_fbx
    request = {
        'revision': 'v08b_SeamUV', 'candidate': candidate,
        'normal_convention': 'DirectX', 'resolution': 4096,
        'mesh': '/Game/UI/MainMenu3D/Meshes/SK_DAY03_Reference_v08b',
        'fbx_sha256': expected_fbx,
        'manifest_sha256': hashlib.sha256(manifest_path.read_bytes()).hexdigest(),
        'texture_sets': {},
    }
    for set_name, maps in manifest['map_metadata'].items():
        assert set_name in ('MC_Enamel', 'MC_Gum')
        entries = {}
        for parameter, info in maps.items():
            assert parameter in ('BaseColorTexture', 'RoughnessTexture', 'NormalTexture', 'AOTexture', 'ThicknessTexture')
            source = Path(info['path']).resolve()
            assert source.is_relative_to(handoff.resolve())
            assert manifest['texture_sets'][set_name][parameter] == str(source)
            digest = hashlib.sha256(source.read_bytes()).hexdigest()
            assert digest == info['sha256'], source
            with Image.open(source) as im:
                size = list(im.size)
                assert size == [4096, 4096] == info['dimensions']
            assert info['srgb'] == (parameter == 'BaseColorTexture')
            if parameter == 'NormalTexture':
                assert info['compression'] == 'TC_NORMALMAP' and info['flip_green_channel'] is False
            entries[parameter] = {'path': str(source), 'sha256': digest, 'size': size}
        assert len(entries) == 5
        request['texture_sets'][set_name] = {'maps': entries}
    assert len(request['texture_sets']) == 2
    target = root / 'Tools/Unreal/Menu3D/Evidence/v08_painter_binding_request.json'
    target.write_text(json.dumps(request, indent=2), encoding='utf8')
    print(json.dumps({'status': 'SOURCE_MANIFEST_HASHES_AND_PNG_DIMENSIONS_VERIFIED',
                      'request': str(target), 'map_count': 10,
                      'manifest_sha256': request['manifest_sha256']}))


if __name__ == '__main__':
    main()
