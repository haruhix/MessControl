"""Render the lip/enamel junction closely enough to review texture and geometry."""
import bpy
from pathlib import Path
from mathutils import Vector
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'Saved/MouthCloseup';OUT.mkdir(exist_ok=True)
obj=bpy.data.objects['SK_TeethFace'];keys=obj.data.shape_keys;keys.animation_data_clear()
scene=bpy.context.scene;scene.render.threads_mode='FIXED';scene.render.threads=12;scene.cycles.samples=32
scene.render.resolution_x=1000;scene.render.resolution_y=450
focus=obj.matrix_world@Vector((0,-29,58.5));scene.camera.location=focus+Vector((.015,-1,.035))
scene.camera.rotation_euler=(focus-scene.camera.location).to_track_quat('-Z','Y').to_euler();scene.camera.data.ortho_scale=.4
for name in ['Basis','Mouth_MBP','Mouth_A','Mouth_O','Mouth_Smile','Mouth_Frown']:
    for key in keys.key_blocks[1:]:key.value=float(key.name==name)
    bpy.context.view_layer.update();scene.render.filepath=str(OUT/(name+'.png'));bpy.ops.render.render(write_still=True)
print('MC_MOUTH_CLOSEUP_PASS')
