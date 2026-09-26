"""Render the face targets and a short coarticulation loop from TeethFaceLab.blend."""
import bpy, math
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'Saved/MouthPreview';OUT.mkdir(parents=True,exist_ok=True)
scene=bpy.context.scene;obj=bpy.data.objects['SK_TeethFace'];keys=obj.data.shape_keys
keys.animation_data_clear()
scene.render.threads_mode='FIXED';scene.render.threads=12
scene.cycles.samples=12;scene.render.resolution_x=600;scene.render.resolution_y=600
for target in keys.key_blocks:
    if target.name.startswith('Pupil_'):continue
    for key in keys.key_blocks[1:]:key.value=float(key==target)
    bpy.context.view_layer.update();scene.render.filepath=str(OUT/(target.name+'.png'));bpy.ops.render.render(write_still=True)
scene.cycles.samples=8;scene.render.resolution_x=480;scene.render.resolution_y=480
sequence=['Mouth_MBP','Mouth_A','Mouth_L','Mouth_E','Mouth_TH','Mouth_O','Mouth_U','Mouth_I','Mouth_Smile','Mouth_MBP']
frames=OUT/'Frames';frames.mkdir(exist_ok=True)
for index,(a,b) in enumerate(zip(sequence,sequence[1:])):
    for step in range(8):
        t=step/7;t=t*t*(3-2*t)
        for key in keys.key_blocks[1:]:key.value=(1-t if key.name==a else 0)+(t if key.name==b else 0)
        bpy.context.view_layer.update();scene.render.filepath=str(frames/f'{index*8+step:03d}.png');bpy.ops.render.render(write_still=True)
print('MC_MOUTH_PREVIEW_PASS',flush=True)
