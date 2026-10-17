"""Render the single saved V7 cast at actual native spell timing.

Use pose_grid() in live Blender. main() is an isolated read-only CLI render.
"""
import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path
import bpy
from mathutils import Vector

ROOT=Path(__file__).resolve().parents[2]
SOURCE=ROOT/'ArtSource/NutAnimationStyle/Wizard/NutWizard_Performance_v7.blend'
OUT=ROOT/'Artifacts/WizardPerformanceV7'
VIEWS={'Front':(0,-7,1.3),'ThreeQuarter':(3,-7,1.45),'Side':(-7,-.25,1.2)}
POSES=[('Ready',1),('Scoop',19),('Loaded',44),('Release',56),('Follow',65),('Settled',101)]


def setup():
    OUT.mkdir(parents=True,exist_ok=True)
    scene=bpy.context.scene
    rig=bpy.data.objects['rig']
    mesh=bpy.data.objects['SM_NutWizard']
    rig.animation_data.action=bpy.data.actions['MC_Wizard_Cast']
    rig.animation_data.action_slot=rig.animation_data.action.slots[0]
    rig.animation_data.use_nla=False
    for t in rig.animation_data.nla_tracks:t.mute=True
    for o in scene.objects:
        if o.type=='MESH' and o!=mesh and o.name!='MC_ReviewFloor':o.hide_render=True
    scene.render.engine='BLENDER_WORKBENCH'
    scene.display.render_aa='8'
    scene.display.shading.light='STUDIO'
    scene.display.shading.studio_light='paint.sl'
    scene.display.shading.color_type='TEXTURE'
    scene.display.shading.show_shadows=True
    scene.display.shading.show_cavity=True
    scene.display.shading.cavity_type='BOTH'
    scene.display.shading.show_specular_highlight=True
    scene.display.shading.background_type='WORLD'
    scene.world.color=(.055,.065,.08)
    scene.view_settings.view_transform='Standard'
    scene.render.resolution_x=480;scene.render.resolution_y=540
    scene.render.resolution_percentage=100
    scene.render.image_settings.file_format='PNG'
    scene.render.image_settings.color_mode='RGB'
    scene.render.image_settings.compression=10
    scene.render.film_transparent=False
    scene.camera.data.type='ORTHO'
    scene.camera.data.ortho_scale=2.15
    return scene,rig


def camera(view):
    cam=bpy.context.scene.camera
    focus=Vector((-.035,-.24,.73))
    cam.location=focus+Vector(VIEWS[view])
    cam.rotation_euler=(focus-cam.location).to_track_quat('-Z','Y').to_euler()


def pose_grid():
    scene,rig=setup()
    original=scene.frame_current
    files=[]
    for view in VIEWS:
        camera(view)
        for name,frame in POSES:
            scene.frame_set(frame)
            scene.render.filepath=str(OUT/f'{view}_{name}.png')
            bpy.ops.render.render(write_still=True)
            files.append(scene.render.filepath)
    scene.frame_set(original)
    camera('ThreeQuarter')
    return {'files':files,'directory':str(OUT)}


def source_fraction(seconds):
    return .55*seconds/.9 if seconds<.9 else .55+.45*(seconds-.9)/.9


def main():
    bpy.ops.wm.open_mainfile(filepath=str(SOURCE))
    before=hashlib.sha256(SOURCE.read_bytes()).hexdigest()
    scene,rig=setup()
    manifest=[]
    for view in VIEWS:
        camera(view)
        folder=OUT/view
        folder.mkdir(exist_ok=True)
        for index in range(54):
            seconds=index/30
            frame=1+100*source_fraction(seconds)
            scene.frame_set(int(frame),subframe=frame-int(frame))
            scene.render.filepath=str(folder/f'frame_{index:04d}.png')
            bpy.ops.render.render(write_still=True)
            manifest.append({'view':view,'index':index,'source_frame':frame,'native_seconds':seconds})
        print('V7_VIEW_READY',view,flush=True)
    assert before==hashlib.sha256(SOURCE.read_bytes()).hexdigest()
    (OUT/'RenderManifest.json').write_text(json.dumps({'source':str(SOURCE),'sha256':before,'frames':manifest,'fps':30,'native_tell':.9,'native_recovery':.9},indent=2),encoding='utf8')
    print('V7_RENDER_COMPLETE',str(OUT),flush=True)


if __name__=='__main__':main()
