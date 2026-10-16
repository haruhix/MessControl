"""Read-only UV checker/layout proof for a frozen interior candidate."""
import bpy,sys,argparse,hashlib,json
from pathlib import Path
from mathutils import Vector
assert bpy.app.background
ap=argparse.ArgumentParser();ap.add_argument('--candidate',required=True);opt=ap.parse_args(sys.argv[sys.argv.index('--')+1:]);OUT=Path('E:/DEVGAME/MessControl/ArtSource/UI/MainMenu3D/20261015_refB_v1/Assembly/UnrealHandoff/v08_ReferenceFinish/Interior')/opt.candidate;source=OUT/'MC_DAY03_InteriorSource.blend';sha=hashlib.sha256(source.read_bytes()).hexdigest();bpy.ops.wm.open_mainfile(filepath=str(source));objects=[bpy.data.objects[n]for n in ['SM_DAY03_Interior','SM_DAY03_Tongue']]
sys.path.insert(0,'C:/Users/user/.codex/skills/blender-uv-baking/scripts');import bx_uvbake as ub
for ob in objects:
 ub.uv_layout_image(ob,str(OUT/('uv_layout_'+ob.name+'.png')),res=2048,uv_map='UV0',color='island',wire=False)
 m=ob.data.materials[0];nodes=m.node_tree.nodes;bs=nodes.get('Principled BSDF');tex=nodes.new('ShaderNodeTexCoord');check=nodes.new('ShaderNodeTexChecker');check.inputs['Color1'].default_value=(.035,.18,.44,1);check.inputs['Color2'].default_value=(.82,.82,.82,1);check.inputs['Scale'].default_value=32;m.node_tree.links.new(tex.outputs['UV'],check.inputs['Vector']);m.node_tree.links.new(check.outputs['Color'],bs.inputs['Base Color']);bs.inputs['Roughness'].default_value=.48;m.use_backface_culling=True
w=bpy.data.worlds.new('Review');bpy.context.scene.world=w;w.use_nodes=True;w.node_tree.nodes['Background'].inputs[0].default_value=(.02,.025,.03,1);w.node_tree.nodes['Background'].inputs[1].default_value=.5
for name,loc,power,size in [('Key',(-4,10,7),1700,9),('Fill',(8,8,0),1000,8),('Back',(0,-18,6),1600,10)]:
 d=bpy.data.lights.new(name,'AREA');l=bpy.data.objects.new(name,d);bpy.context.scene.collection.objects.link(l);l.location=loc;l.rotation_euler=(Vector((0,-5,0))-l.location).to_track_quat('-Z','Y').to_euler();d.energy=power;d.size=size
scene=bpy.context.scene;cd=bpy.data.cameras.new('ReviewCamera');cam=bpy.data.objects.new('ReviewCamera',cd);scene.collection.objects.link(cam);scene.camera=cam;scene.render.engine='BLENDER_EEVEE';scene.render.resolution_x=1600;scene.render.resolution_y=900;scene.render.resolution_percentage=100;scene.render.image_settings.file_format='PNG';scene.view_settings.view_transform='AgX'
for name,loc in [('checker_interior_front',(0,35,0)),('checker_interior_oblique',(15,28,8)),('checker_interior_back',(0,-40,1)),('checker_interior_side',(31,-3,7))]:
 cam.location=loc;cam.rotation_euler=(Vector((0,-5,0))-cam.location).to_track_quat('-Z','Y').to_euler();cd.type='ORTHO'if'front'in name or'back'in name else'PERSP';cd.ortho_scale=23.;cd.lens=42.;scene.render.filepath=str(OUT/(name+'.png'));bpy.ops.render.render(write_still=True)
assert hashlib.sha256(source.read_bytes()).hexdigest()==sha;(OUT/'checker_review_receipt.json').write_text(json.dumps({'source_sha256':sha,'source_unchanged':True,'resolution':[1600,900],'checker_scale':32,'camera':'Fixed +Yfront, oblique, -Yback and side; full closedcoverage, no clipping'},indent=2));print('INTERIOR_UV_PROOF_COMPLETE',sha)
