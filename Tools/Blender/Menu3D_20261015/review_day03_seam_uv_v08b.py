"""Read-only checker/projection review of the frozen v08b source."""
import bpy,json,hashlib,argparse,sys
from pathlib import Path
from mathutils import Vector
assert bpy.app.background
parser=argparse.ArgumentParser();parser.add_argument('--candidate');args=parser.parse_args(sys.argv[sys.argv.index('--')+1:]if'--'in sys.argv else[])
OUT=Path('E:/DEVGAME/MessControl/ArtSource/UI/MainMenu3D/20261015_refB_v1/Assembly/UnrealHandoff/v08b_SeamUV')
if args.candidate:OUT=OUT/'Candidates'/args.candidate
source=OUT/'MC_DAY03_UnrealMorphSource.blend';sha=hashlib.sha256(source.read_bytes()).hexdigest();bpy.ops.wm.open_mainfile(filepath=str(source));o=bpy.data.objects['SK_DAY03'];scene=bpy.context.scene
for k in o.data.shape_keys.key_blocks:k.value=0.
for m in o.data.materials:
 if not m.use_nodes:m.use_nodes=True
 nodes=m.node_tree.nodes;bs=nodes.get('Principled BSDF');checker=nodes.new('ShaderNodeTexChecker');checker.inputs['Color1'].default_value=(.04,.17,.44,1);checker.inputs['Color2'].default_value=(.85,.85,.85,1);checker.inputs['Scale'].default_value=32;tex=nodes.new('ShaderNodeTexCoord');m.node_tree.links.new(tex.outputs['UV'],checker.inputs['Vector']);m.node_tree.links.new(checker.outputs['Color'],bs.inputs['Base Color']);bs.inputs['Roughness'].default_value=.5
w=bpy.data.worlds.new('GeometryReview');scene.world=w;w.use_nodes=True;w.node_tree.nodes['Background'].inputs[0].default_value=(.02,.025,.03,1);w.node_tree.nodes['Background'].inputs[1].default_value=.55
for name,loc,power,size in [('Front',(0,12,8),1600,10),('Left',(-9,8,3),1000,8),('Right',(9,8,3),1000,8),('Rim',(0,-7,8),1800,8)]:
 d=bpy.data.lights.new('GEO-Review'+name,'AREA');l=bpy.data.objects.new('GEO-Review'+name,d);scene.collection.objects.link(l);l.location=loc;l.rotation_euler=(-l.location).to_track_quat('-Z','Y').to_euler();d.energy=power;d.shape='DISK';d.size=size
cd=bpy.data.cameras.new('GEO-ReviewCamera');cam=bpy.data.objects.new('GEO-ReviewCamera',cd);scene.collection.objects.link(cam);scene.camera=cam;scene.render.engine='BLENDER_EEVEE';scene.render.resolution_x=1600;scene.render.resolution_y=900;scene.render.resolution_percentage=100;scene.render.image_settings.file_format='PNG';scene.view_settings.view_transform='AgX';scene.render.film_transparent=False
for name,loc in [('checker_front_open',(0,35,0)),('checker_oblique_open',(12,30,7)),('checker_back_open',(0,-35,0)),('checker_side_open',(30,12,5))]:
 cam.location=loc;cam.rotation_euler=(-cam.location).to_track_quat('-Z','Y').to_euler();cd.type='ORTHO'if'front'in name or'back'in name else'PERSP';cd.ortho_scale=19.5;cd.lens=49.5;scene.render.filepath=str(OUT/(name+'.png'));bpy.ops.render.render(write_still=True)
assert hashlib.sha256(source.read_bytes()).hexdigest()==sha
(OUT/'checker_review_receipt.json').write_text(json.dumps({'source_sha256':sha,'camera':'Diagnostic fixedneutralfront/back/oblique/side','source_unchanged':True,'resolution':[1600,900],'UVchecker_scale':32},indent=2));print('SEAM_CHECKER_REVIEW_COMPLETE',sha)
