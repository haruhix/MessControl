"""Owned source-consistent chest packet; preserves the authored albedo bytewise.

Run with workspace Python, not editor Python. The existing imported brown atlas
has a different layout, so none of its normal/ORM pixels are reused. No authored
detail normal is available for the cyan atlas: a neutral tangent normal retains
the mesh's authored vertex normals. AO is white (the albedo already has painted
form shading); material IDs derive only metal/roughness from cyan vs gold paint.
"""
import hashlib
import json
import shutil
from pathlib import Path
import numpy as np
from PIL import Image

ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'ArtSource/ChestGameplay_20261022'
assert OUT.drive.upper()=='E:'
OUT.mkdir(parents=True,exist_ok=True)
source=ROOT/'ArtSource/ChestLockpick/Props/Chest_BaseColor.png'
albedo=OUT/'T_ChestGameplay_BaseColor.png'
shutil.copy2(source,albedo)
rgb=np.asarray(Image.open(source).convert('RGB').resize((512,512),Image.Resampling.LANCZOS),dtype=np.float32)/255.
gold=np.clip((rgb[:,:,0]-rgb[:,:,2]+.035)/.20,0.,1.)
gold=gold*gold*(3.-2.*gold)
orm=np.stack([np.ones_like(gold),.50-.18*gold,.04+.61*gold],axis=2)
Image.fromarray(np.uint8(np.rint(orm*255)),'RGB').save(OUT/'T_ChestGameplay_ORM.png')
Image.new('RGB',(16,16),(128,128,255)).save(OUT/'T_ChestGameplay_Normal.png')
report=dict(source=str(source),source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
    albedo_byte_identical=source.read_bytes()==albedo.read_bytes(),
    intent=dict(normal='Neutral tangent-space normal; no invented authored detail',
        AO='White: no foreign atlas and no duplicate baked occlusion',
        roughness='Cyan paint .50, brass/gold .32',metallic='Cyan paint .04, brass/gold .65'),
    outputs={p.name:dict(sha256=hashlib.sha256(p.read_bytes()).hexdigest(),size=list(Image.open(p).size))
        for p in OUT.glob('*.png')})
(OUT/'Manifest.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(OUT/'Manifest.json')
