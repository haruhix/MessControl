"""Render each authored accessory in isolation without changing the source blend."""
import bpy,json,math
from pathlib import Path
from mathutils import Vector
ROOT=Path(__file__).resolve().parents[3];OUT=ROOT/'Saved/HeadCosmetics20261024/SourcePreviews';OUT.mkdir(parents=True,exist_ok=True)
names=['Cube.001','glass_1','Plane','SM_Character.001','Sphere','Sphere.001','Sphere.002'];source={n:bpy.data.objects[n] for n in names}
scene=bpy.data.scenes.new('HeadCosmeticsReadOnlyPreview');bpy.context.window.scene=scene
scene.render.engine='CYCLES';scene.cycles.samples=12;scene.cycles.device='CPU';scene.render.resolution_x=540;scene.render.resolution_y=540;scene.render.resolution_percentage=100
scene.world=bpy.data.worlds.new('HeadPreviewWorld');scene.world.use_nodes=True;scene.world.node_tree.nodes['Background'].inputs['Color'].default_value=(.07,.09,.10,1);scene.world.node_tree.nodes['Background'].inputs['Strength'].default_value=.45
def material(name,color):
    m=bpy.data.materials.new(name);m.use_nodes=True;p=m.node_tree.nodes.get('Principled BSDF');p.inputs['Base Color'].default_value=(*color,1);p.inputs['Roughness'].default_value=.4;return m
neutral=material('PreviewUnassignedMaterial',(.12,.55,.60));dark=material('PreviewExistingBlack',(.008,.008,.008))
for n,o in source.items():
    scene.collection.objects.link(o);o.hide_render=True
    if not o.data.materials:o.data.materials.append(neutral)
    if n=='SM_Character.001':o.data.materials.clear();o.data.materials.append(neutral)
for name,loc,power,size in [('Key',(-2,-3,4),400,3),('Fill',(3,-1,2),150,3),('Rim',(0,3,3),250,2)]:
    d=bpy.data.lights.new(name,'AREA');d.energy=power;d.shape='DISK';d.size=size;o=bpy.data.objects.new(name,d);scene.collection.objects.link(o);o.location=loc
    o.rotation_euler=(Vector((0,0,1))-o.location).to_track_quat('-Z','Y').to_euler()
d=bpy.data.cameras.new('HeadPreviewCamera');cam=bpy.data.objects.new('HeadPreviewCamera',d);scene.collection.objects.link(cam);scene.camera=cam;d.type='ORTHO'
for index,(n,o) in enumerate(source.items()):
    o.hide_render=False;ps=[o.matrix_world@v.co for v in o.data.vertices];lo=Vector([min(p[i] for p in ps) for i in range(3)]);hi=Vector([max(p[i] for p in ps) for i in range(3)]);center=(lo+hi)*.5
    cam.location=center+Vector((2,-3,1.6));cam.rotation_euler=(center-cam.location).to_track_quat('-Z','Y').to_euler();d.ortho_scale=max(hi-lo)*1.45;scene.render.filepath=str(OUT/f'{index:02d}.png');bpy.ops.render.render(write_still=True);o.hide_render=True
print('HEAD_PREVIEWS_READY',str(OUT))
