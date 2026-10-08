"""Arrange only the newly launched Blender window for editable animation review."""
import bpy
window=bpy.context.window
review_scene=bpy.data.scenes.get('MC_Tank_Abilities_v2_Review')
if window and review_scene:
    window.scene=review_scene
    review_scene.frame_set(review_scene.frame_start)
workspace=bpy.data.workspaces.get('Animation')
if workspace and window:window.workspace=workspace
rig=bpy.data.objects.get('MCV2_ReviewRig') if review_scene else bpy.data.objects.get('rig')
if rig:
    for obj in bpy.context.scene.objects:obj.select_set(False)
    rig.hide_set(False);rig.select_set(True);bpy.context.view_layer.objects.active=rig
for area in window.screen.areas if window else []:
    if area.type=='VIEW_3D':
        area.spaces.active.region_3d.view_perspective='CAMERA'
        area.spaces.active.shading.type='SOLID'
        area.spaces.active.overlay.show_overlays=False
    elif area.type=='DOPESHEET_EDITOR':
        area.spaces.active.mode='ACTION'
print('NUT_STYLE_UI_READY',bpy.data.filepath,flush=True)
