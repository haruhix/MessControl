"""Make an editable, lit review scene from TeethAnimationLab.blend and render action poses."""
import bpy, math
from pathlib import Path
from mathutils import Vector, Matrix, Quaternion
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'Saved/AnimationPreview';OUT.mkdir(parents=True,exist_ok=True)
scene=bpy.context.scene
rig=next(o for o in scene.objects if o.type=='ARMATURE')
rig.animation_data.action=bpy.data.actions['A_Teeth_Hello']
for obj in list(scene.objects):
    if obj.type in {'CAMERA','LIGHT'} or obj.name.startswith('ReviewFloor'):
        bpy.data.objects.remove(obj,do_unlink=True)
for action in list(bpy.data.actions):
    if action.name.startswith(('Face_','Speech_')):bpy.data.actions.remove(action)
scene.render.engine='CYCLES';scene.cycles.samples=16
scene.render.resolution_x=640;scene.render.resolution_y=640;scene.render.resolution_percentage=100
scene.world=bpy.data.worlds.new('StudioWorld');scene.world.use_nodes=True
scene.world.node_tree.nodes['Background'].inputs[0].default_value=(.08,.105,.13,1)
scene.world.node_tree.nodes['Background'].inputs[1].default_value=.5
bpy.ops.object.camera_add(location=(2,-3.7,1.7));camera=bpy.context.object;camera.name='ReviewCamera'
camera.rotation_euler=(Vector((0,0,.57))-camera.location).to_track_quat('-Z','Y').to_euler()
camera.data.type='ORTHO';camera.data.ortho_scale=3.3;scene.camera=camera
for name,pos,energy,size in [('Key',(1,-3,4),400,4),('Fill',(-3,-1,2),200,3),('Rim',(0,3,3),450,2)]:
    bpy.ops.object.light_add(type='AREA',location=pos);light=bpy.context.object;light.name=name;light.data.energy=energy;light.data.shape='DISK';light.data.size=size
    light.rotation_euler=(Vector((0,0,.6))-light.location).to_track_quat('-Z','Y').to_euler()
bpy.ops.mesh.primitive_plane_add(size=200,location=(0,0,-.045));floor=bpy.context.object;floor.name='ReviewFloor'
mat=bpy.data.materials.get('StudioFloor') or bpy.data.materials.new('StudioFloor');mat.use_nodes=True
mat.node_tree.nodes['Principled BSDF'].inputs['Base Color'].default_value=(.035,.055,.07,1)
mat.node_tree.nodes['Principled BSDF'].inputs['Roughness'].default_value=.8
floor.data.materials.append(mat)
# Matching facial studies on the exported rig. All offsets are in rig centimetres.
for name,jaw,smile,brows,tilt,squint,roundness in [
    ('Face_Happy',0,1,.25,0,.18,0),('Face_Angry',0,-.25,0,1,.25,0),
    ('Face_Sad',0,-.65,-.15,-.65,0,0),('Face_Surprise',.65,0,1,0,0,.5),
    ('Face_Pain',.5,-.4,0,.8,.8,0),('Speech_Round',.65,0,0,0,0,1)]:
    rig.animation_data.action=None
    for p in rig.pose.bones:p.matrix_basis=Matrix.Identity(4)
    action=bpy.data.actions.new(name);action.use_fake_user=True;rig.animation_data.action=action
    for frame,weight in [(1,0),(12,1),(32,1),(44,0)]:
        for p in rig.pose.bones:p.matrix_basis=Matrix.Identity(4)
        def offset(n,v):
            p=rig.pose.bones.get(n)
            if p:p.location=p.bone.matrix_local.to_3x3().inverted() @ (Vector(v)*weight)
        offset('c_jawbone_x',(0,0,-2.8*jaw))
        offset('c_lips_top_x',(0,0,-1.8*max(0,smile)))
        offset('c_lips_bot_x',(0,0,1.2*max(0,smile)))
        for side,sign in [('l',1),('r',-1)]:
            offset('c_lips_smile_'+side,(sign*(smile*3-roundness*1.6),0,smile*3.5))
            offset('c_eyebrow_full_'+side,(0,0,brows*1.7))
            offset('c_eyebrow_01_'+side,(0,0,-tilt*2.5))
            offset('c_eyebrow_03_'+side,(0,0,tilt*1.2))
            for part,degrees in [('top',48),('bot',-28)]:
                p=rig.pose.bones['c_eyelid_'+part+'_'+side];basis=p.bone.matrix_local.to_quaternion()
                p.rotation_quaternion=basis.inverted() @ Quaternion((1,0,0),math.radians(degrees*squint*weight)) @ basis
        for p in rig.pose.bones:
            for prop in ('location','rotation_quaternion','scale'):p.keyframe_insert(prop,frame=frame,group=p.name)
    if name in ('Face_Happy','Face_Pain','Speech_Round'):
        scene.frame_set(20);bpy.context.view_layer.update()
        focus=Vector((0,-.1,.66));camera.location=focus+Vector((.65,-3,1));camera.rotation_euler=(focus-camera.location).to_track_quat('-Z','Y').to_euler();camera.data.ortho_scale=1.3
        scene.render.filepath=str(OUT/(name+'.png'));bpy.ops.render.render(write_still=True)
camera.data.ortho_scale=3.3
for action_name in ('A_Teeth_Hello','A_Teeth_Highfive'):
    rig.animation_data.action=bpy.data.actions[action_name]
    for frame in (1,20,35):
        scene.frame_set(frame);bpy.context.view_layer.update()
        focus=rig.matrix_world @ rig.pose.bones['head_x'].matrix.translation
        camera.location=focus+Vector((2,-3.7,1.1));camera.rotation_euler=(focus-camera.location).to_track_quat('-Z','Y').to_euler()
        scene.render.filepath=str(OUT/(action_name+'_'+str(frame)+'.png'))
        bpy.ops.render.render(write_still=True)
rig.animation_data.action=bpy.data.actions['A_Teeth_Hello']
scene.frame_start=1;scene.frame_end=50;scene.frame_set(20);bpy.context.view_layer.update()
focus=rig.matrix_world @ rig.pose.bones['head_x'].matrix.translation
camera.location=focus+Vector((2,-3.7,1.1));camera.rotation_euler=(focus-camera.location).to_track_quat('-Z','Y').to_euler();camera.data.ortho_scale=2.1
bpy.ops.object.select_all(action='DESELECT');rig.select_set(True);bpy.context.view_layer.objects.active=rig
for action in bpy.data.actions:
    if action.name.startswith(('A_Teeth_','Face_','Speech_')):action.asset_mark()
for screen in bpy.data.screens:
    for area in screen.areas:
        if area.type=='VIEW_3D':
            area.spaces.active.region_3d.view_location=focus
            area.spaces.active.region_3d.view_distance=3.2
            area.spaces.active.shading.color_type='TEXTURE'
scene['README']='Action Editor: A_Teeth_Hello / A_Teeth_Highfive (animator clips), Face_Happy / Angry / Sad / Surprise / Pain, Speech_Round (procedural facial studies, not phoneme recognition). Source animator file is unchanged.'
bpy.ops.file.pack_all()
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'ArtSource/CharacterAnimation/TeethAnimationLab.blend'))
print('MC_ANIMATION_PREVIEW_PASS',flush=True)
