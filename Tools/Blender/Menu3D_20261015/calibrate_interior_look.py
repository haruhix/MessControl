"""Bounded actual-render color/light experiment on a saved isolated candidate.

Emission controls diagnose display mapping only. They are not delivered shaders.
"""
import argparse
import json
from pathlib import Path
import sys

import bpy
from mathutils import Vector

ROOT=Path(r'E:\DEVGAME\MessControl\ArtSource\UI\MainMenu3D\20261015_refB_v1')
p=argparse.ArgumentParser()
p.add_argument('--binding')
p.add_argument('--scene',default='Assembly/MC_Menu3D_Candidate_InteriorPBR_isolated.blend')
p.add_argument('--out',default='InteriorCalibration01')
args=p.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
out=ROOT/'Assembly/Review'/args.out
out.mkdir(parents=True,exist_ok=True)
bpy.ops.wm.open_mainfile(filepath=str(ROOT/args.scene))
scene=bpy.context.scene
scene.cycles.samples=32
scene.render.resolution_x=963
scene.render.resolution_y=410
scene.render.resolution_percentage=100
scene.render.image_settings.file_format='PNG'
prefs=bpy.context.preferences.addons['cycles'].preferences
prefs.compute_device_type='OPTIX';prefs.get_devices()
for device in prefs.devices:device.use=device.type=='OPTIX'
scene.cycles.device='GPU'
materials=[bpy.data.materials[n] for n in ('MC_Interior','MC_Tongue')]
if args.binding:
    bindings=json.loads(Path(args.binding).read_text())['materials']
    for mat in materials:
        for node in mat.node_tree.nodes:
            if node.type!='TEX_IMAGE' or not node.image:
                continue
            old=node.image.name.lower()
            # Match existing shader image roles, not material or layer-name guesses.
            key=next((k for k in ('base_color','roughness','normal_opengl','wetness','subsurface')
                      if Path(bindings[mat.name][k]).name==node.label),None)
            if key:
                im=bpy.data.images.load(bindings[mat.name][key],check_existing=True)
                im.colorspace_settings.name='sRGB' if key=='base_color' else 'Non-Color'
                node.image=im


def render(name,view,look='None',exposure=0):
    scene.view_settings.view_transform=view
    scene.view_settings.look=look
    scene.view_settings.exposure=exposure
    scene.render.filepath=str(out/f'{name}.png')
    bpy.ops.render.render(write_still=True)
    bpy.data.images['Render Result'].save_render(str(out/f'{name}.png'),scene=scene)


original_outputs=[]
for mat in materials:
    nt=mat.node_tree
    bs=next(n for n in nt.nodes if n.type=='BSDF_PRINCIPLED')
    output=next(n for n in nt.nodes if n.type=='OUTPUT_MATERIAL')
    original_outputs.append((nt,output,output.inputs['Surface'].links[0].from_socket))
    emission=nt.nodes.new('ShaderNodeEmission')
    emission.label='DIAGNOSTIC ONLY: display-transform source appearance control'
    nt.links.new(bs.inputs['Base Color'].links[0].from_socket,emission.inputs['Color'])
    nt.links.new(emission.outputs[0],output.inputs['Surface'])
render('control_emission_standard','Standard')
render('control_emission_originalAgX','AgX','AgX - Medium High Contrast',-.6)
for nt,output,socket in original_outputs:
    nt.links.new(socket,output.inputs['Surface'])

render('physical_originallights_standard','Standard')
render('physical_originallights_agx_none','AgX')
for obj in list(scene.objects):
    if obj.type=='LIGHT':bpy.data.objects.remove(obj,do_unlink=True)


def area(name,position,target,energy,size,size_y,specular=1):
    data=bpy.data.lights.new(name,'AREA')
    data.energy=energy;data.shape='RECTANGLE';data.size=size;data.size_y=size_y
    data.color=(1.,1.,1.);data.specular_factor=specular
    obj=bpy.data.objects.new(name,data);scene.collection.objects.link(obj)
    obj.location=position
    obj.rotation_euler=(Vector(target)-obj.location).to_track_quat('-Z','Y').to_euler()


# Neutral camera-side illumination is the controlled starting point. The source
# has a brighter enamel foreground and darker recess. Dedicated tissue response
# is adjusted only after this light/material separation experiment.
area('CAL_Key',(-4,-7,6),(0,1,0),1000,3,6)
area('CAL_Fill',(6,-7,1),(0,2,0),400,8,8,.15)
area('CAL_Highlight',(-1,-5,7),(0,0,1),500,.7,5)
area('CAL_InteriorFill',(0,-3,-.5),(0,6,-.5),55,9,6,0)
bg=scene.world.node_tree.nodes.get('Background')
bg.inputs[0].default_value=(.04,.04,.04,1)
bg.inputs[1].default_value=.15
render('physical_neutral_standard','Standard')
render('physical_neutral_agx_none','AgX')
render('physical_neutral_khronos','Khronos PBR Neutral')
scene.view_settings.view_transform='Standard';scene.view_settings.look='None';scene.view_settings.exposure=0
bpy.ops.wm.save_as_mainfile(filepath=str(out/'Interior_neutral_calibration.blend'))
(out/'manifest.json').write_text(json.dumps({'status':'DIAGNOSTIC_ONLY_NOT_APPEARANCE_ACCEPTED',
    'emission_controls':'Display mapping experiment only; actual final candidate remains Principled PBR',
    'source_scene':'MC_Menu3D_Candidate_InteriorPBR_isolated.blend',
    'images':[p.name for p in out.glob('*.png')]},indent=2))
print('CALIBRATION_COMPLETE',out)
