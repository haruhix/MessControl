"""Offline ephemeral wrist-bounds comparison at release; does not save blend."""
import bpy,math,json
from mathutils import Quaternion,Vector
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
rig=bpy.data.objects['rig'];scene=bpy.context.scene
action=bpy.data.actions['MC_Wizard_Cast'];rig.animation_data.action=action
if action.slots:rig.animation_data.action_slot=action.slots[0]
rig.animation_data.use_nla=False;scene.frame_set(55);bpy.context.view_layer.update()
rig.animation_data.action=None
scene.render.engine='BLENDER_WORKBENCH';scene.render.resolution_x=1200;scene.render.resolution_y=800
scene.render.resolution_percentage=100;scene.display.shading.light='STUDIO';scene.display.shading.studio_light='paint.sl'
camera=scene.camera;camera.data.type='ORTHO';camera.data.ortho_scale=3.6
report={}
for side in ('r','l'):
    hand=rig.pose.bones['c_hand_fk.'+side]
    desired=hand.matrix.to_quaternion();position=hand.matrix.translation.copy()
    hand.matrix_basis.identity();bpy.context.view_layer.update()
    neutral=hand.matrix.to_quaternion()
    relative=neutral.inverted()@desired;relative.normalize()
    swing,twist=relative.to_swing_twist('Y')
    if swing.w<0:swing.negate()
    angle=swing.angle;limit=math.radians(35)
    if angle>limit:swing=Quaternion().slerp(swing,limit/angle)
    twist_limited=max(-math.radians(45),min(math.radians(45),twist))
    q=neutral@swing@Quaternion(Vector((0,1,0)),twist_limited)
    matrix=q.to_matrix().to_4x4();matrix.translation=position;hand.matrix=matrix
    bpy.context.view_layer.update()
    report[side]=dict(original_swing_deg=math.degrees(angle),original_twist_deg=math.degrees(twist),
        limited_swing_deg=min(35,math.degrees(angle)),limited_twist_deg=math.degrees(twist_limited),
        wrist_delta_m=(hand.matrix.translation-position).length)
for view,pos in (('Front',(0,-4,1.8)),('Side',(4,-.25,1.25))):
    camera.location=pos;camera.rotation_euler=(Vector((0,-.15,.75))-camera.location).to_track_quat('-Z','Y').to_euler()
    scene.render.filepath=str(ROOT/'Saved/Codex'/f'WizardArm_WristLimited55_{view}.png')
    bpy.ops.render.render(write_still=True)
(ROOT/'Saved/Codex/WizardWristLimitsV7Probe.json').write_text(json.dumps(report,indent=2),encoding='utf8')
print(json.dumps(report),flush=True)
