"""Actual Blender matte projections of tagged source-locked objects.

Read task-owned scene only, replace presentation in memory, never save source.
"""
import argparse
import json
from pathlib import Path
import sys
import bpy
from mathutils import Vector

a=sys.argv[sys.argv.index('--')+1:]if'--'in sys.argv else[]
p=argparse.ArgumentParser();p.add_argument('--blend',required=True);p.add_argument('--out',required=True)
p.add_argument('--amount',type=float,default=1.);opt=p.parse_args(a)
out=Path(opt.out);out.mkdir(parents=True,exist_ok=True)
bpy.ops.wm.open_mainfile(filepath=str(Path(opt.blend).resolve()))
scene=bpy.context.scene
for c in bpy.data.collections:
    c.hide_render=False;c.hide_viewport=False
for ob in scene.objects:
    ob.hide_set(False);ob.hide_viewport=False;ob.hide_render=True
root=bpy.data.objects.get('MC_JawsRoot')
if root:
    root.animation_data_clear();root['OpenAmount']=opt.amount;root.update_tag()
scene.frame_set(1);bpy.context.view_layer.update()
targets=[o for o in scene.objects if o.type=='MESH' and o.get('SourcePart') and o.get('SourceMask')]
if not targets:raise RuntimeError('No SourcePart/SourceMask tagged mesh; refusing invented correspondence')
mat=bpy.data.materials.new('PROJECTION_WHITE');mat.use_nodes=True
nodes=mat.node_tree.nodes;nodes.clear();e=nodes.new('ShaderNodeEmission');e.inputs['Color'].default_value=(1,1,1,1);e.inputs['Strength'].default_value=1
o=nodes.new('ShaderNodeOutputMaterial');mat.node_tree.links.new(e.outputs[0],o.inputs['Surface'])
for ob in targets:
    ob.data.materials.clear();ob.data.materials.append(mat)
world=bpy.data.worlds.new('PROJECTION_BLACK');world.use_nodes=True;world.node_tree.nodes.get('Background').inputs[0].default_value=(0,0,0,1);scene.world=world
d=bpy.data.cameras.new('ProjectionCanonical');cam=bpy.data.objects.new(d.name,d);scene.collection.objects.link(cam)
cam.location=(0,-30,0);cam.rotation_euler=(Vector((0,0,0))-cam.location).to_track_quat('-Z','Y').to_euler();d.type='ORTHO';d.ortho_scale=16;scene.camera=cam
scene.render.engine='BLENDER_EEVEE';scene.render.resolution_x=963;scene.render.resolution_y=410;scene.render.resolution_percentage=100
scene.render.threads_mode='FIXED';scene.render.threads=2;scene.render.film_transparent=False
scene.view_settings.view_transform='Standard';scene.view_settings.look='None';scene.view_settings.exposure=0;scene.view_settings.gamma=1
scene.render.image_settings.file_format='PNG';records=[]
for ob in targets:
    ob.hide_render=False;bpy.context.view_layer.update()
    path=out/(str(ob['SourcePart'])+'.png');scene.render.filepath=str(path);bpy.ops.render.render(write_still=True)
    records.append({'part':str(ob['SourcePart']),'object':ob.name,'reference_mask':str(ob['SourceMask']),'render_mask':str(path),'ambiguous':bool(ob.get('SourceAmbiguous',False)), 'occlusion_mask':str(ob.get('SourceOcclusionMask',''))})
    ob.hide_render=True
(out/'projection_manifest.json').write_text(json.dumps({'source':opt.blend,'camera':'W16,X/Zsource-locked,negativeY','resolution':[963,410],'open_amount':opt.amount,'records':records},indent=2),encoding='utf8')
print('PROJECTION_GATE_READY',json.dumps({'targets':len(records),'manifest':str(out/'projection_manifest.json')}))
