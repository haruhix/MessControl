"""One actual render removing the cavity fill intersecting Closed crown bulges."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import bpy

p=argparse.ArgumentParser()
p.add_argument('--candidate',required=True)
p.add_argument('--out',required=True)
opt=p.parse_args(sys.argv[sys.argv.index('--')+1:])
source=Path(opt.candidate).resolve();before=hashlib.sha256(source.read_bytes()).hexdigest()
bpy.ops.wm.open_mainfile(filepath=str(source))
scene=bpy.context.scene
root=bpy.data.objects['MC_JawsRoot'];root.animation_data_clear()
root['OpenAmount']=0.;root.update_tag();scene.frame_set(42)
scene.camera.data.ortho_scale=14.2
scene.render.resolution_x=963;scene.render.resolution_y=257
bpy.data.collections['MenuInterior_Geometry'].hide_render=True
bpy.data.collections['Menu_ExistingCharacter'].hide_render=True
light=bpy.data.objects['Menu_Interior_NeutralFill']
light_position=list(light.location)
bpy.data.objects.remove(light,do_unlink=True)
bpy.context.view_layer.update()
scene.cycles.samples=96
out=Path(opt.out).resolve();out.mkdir(parents=True,exist_ok=True)
scene.render.filepath=str(out/'Closed_InteriorEmitterRemoved.png')
bpy.ops.render.render(write_still=True)
(out/'probe_receipt.json').write_text(json.dumps({'source':str(source),'source_sha256':before,
 'source_unchanged':before==hashlib.sha256(source.read_bytes()).hexdigest(),
 'only_change':'Menu_Interior_NeutralFill emitter removed from temporary scene',
 'removed_light_position':light_position,'pose':0,'render':scene.render.filepath},indent=2),encoding='utf8')
print('CLOSED_EMITTER_PROBE_READY',scene.render.filepath,flush=True)
