"""Render pupil sizes on the current face without changing its saved actions."""
import bpy
from pathlib import Path
from mathutils import Vector
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'Saved/PupilPreview';OUT.mkdir(exist_ok=True,parents=True)
obj=bpy.data.objects['SK_TeethFace'];keys=obj.data.shape_keys;keys.animation_data_clear()
scene=bpy.context.scene;scene.render.resolution_x=600;scene.render.resolution_y=600
scene.render.threads_mode='FIXED';scene.render.threads=12;scene.cycles.samples=16
focus=Vector((0,-.17,.79));scene.camera.location=(.25,-2.8,.90)
scene.camera.rotation_euler=(focus-scene.camera.location).to_track_quat('-Z','Y').to_euler();scene.camera.data.ortho_scale=.94
for label,dilate,contract in [('Calm',0,0),('Danger',1,0),('Focus',0,.5)]:
    for key in keys.key_blocks[1:]:key.value=0
    keys.key_blocks['Pupil_Dilate'].value=dilate;keys.key_blocks['Pupil_Contract'].value=contract
    bpy.context.view_layer.update();scene.render.filepath=str(OUT/(label+'.png'));bpy.ops.render.render(write_still=True)
for key in keys.key_blocks[1:]:key.value=0
print('MC_PUPIL_PREVIEW_PASS')
