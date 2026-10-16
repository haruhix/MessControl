"""Explicit loss-bounded format conversion for Unreal's sRGB color sampler.

Keep 16-bit source atlases readonly. Unreal color sampling for the native
menu uses uncompressed eight-bit sRGB, matching the original reference.
"""
import hashlib
import json
from pathlib import Path
import cv2
import numpy as np

root = Path('E:/DEVGAME/MessControl/ArtSource/UI/MainMenu3D/20261015_refB_v1')
src = root/'Textures/Revisions/v42_SymmetricLower_Radiance_CPU'
out = root/'Assembly/UnrealHandoff/v04_SeatedSymmetricNative/Textures_sRGB8'
out.mkdir(parents=True,exist_ok=True)
receipt = []
for pose in ('open','closed'):
    binding = json.loads((src/f'material_binding_{pose}_SymmetricLower_Radiance_NOT_ALBEDO.json').read_text(encoding='utf8'))
    for material, fields in binding['materials'].items():
        source = Path(fields['radiance_not_albedo'])
        data = cv2.imread(str(source),cv2.IMREAD_UNCHANGED)
        assert data is not None
        converted = ((data.astype(np.uint32)+128)//257).astype(np.uint8) if data.dtype == np.uint16 else data
        assert converted.dtype == np.uint8
        dest = out/f'{pose}_{material}_Radiance_sRGB8.png'
        assert cv2.imwrite(str(dest),converted)
        fields['radiance_not_albedo'] = str(dest)
        receipt.append({'source':str(source),'sha256_source':hashlib.sha256(source.read_bytes()).hexdigest(),
            'native':str(dest),'sha256_native':hashlib.sha256(dest.read_bytes()).hexdigest(),
            'conversion':'round uint16/257 to uint8; at most .5/255 encoded-channel quantization error'})
    (out/f'{pose}_binding.json').write_text(json.dumps(binding,indent=2),encoding='utf8')
(out/'format_conversion_receipt.json').write_text(json.dumps(receipt,indent=2),encoding='utf8')
print('NATIVE_SRGB8_READY',len(receipt))
