"""Nine freshly posed performances on the untouched production Wizard ARP rig.

An isolated DCC revision. No action from an earlier version supplies motion.
The existing ARP rig, rest matrices, meshes and weights remain unchanged.
"""
import ast
import hashlib
import json
import math
from pathlib import Path

import bpy
from mathutils import Euler, Matrix, Quaternion, Vector

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'ArtSource/NutAnimationStyle/Wizard'
SOURCE = OUT / 'NutWizard_Casts_v5.blend'
OUTPUT = OUT / 'NutWizard_Fresh_v6.blend'
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
source_sha = digest(SOURCE)
bpy.ops.wm.open_mainfile(filepath=str(SOURCE))
scene = bpy.context.scene
rig = bpy.data.objects['rig']
mesh = bpy.data.objects['SM_NutWizard']
rig.animation_data.use_nla = False
for track in rig.animation_data.nla_tracks:
    track.mute = True
rest = {b.name: b.matrix_local.copy() for b in rig.data.bones}
original_props = {b.name: {k: b[k] for k in b.keys() if isinstance(b[k], (bool, int, float))} for b in rig.pose.bones}
controls = [b for b in rig.pose.bones if b.name.startswith('c_')]

def signature():
    return [(b.name, b.parent.name if b.parent else None, b.use_deform, list(map(list, b.matrix_local))) for b in rig.data.bones]

def mesh_signature():
    return hashlib.sha256(json.dumps(dict(vertices=[list(v.co) for v in mesh.data.vertices],
        faces=[(list(p.vertices), p.material_index) for p in mesh.data.polygons],
        weights=[[(g.group, g.weight) for g in v.groups] for v in mesh.data.vertices],
        materials=[m.name if m else None for m in mesh.data.materials]), sort_keys=True).encode()).hexdigest()

rest_before, mesh_before = signature(), mesh_signature()

# Only the low-level rig reset/key insertion functions are reused. No previous
# poses, timing or animation curves are read to construct the new performances.
tree = ast.parse((ROOT / 'Tools/Blender/author_nut_wizard_style.py').read_text(encoding='utf8'))
exec(compile(ast.Module(body=[n for n in tree.body if isinstance(n, ast.FunctionDef)
    and n.name in {'bind', 'reset', 'set_matrix', 'key'}], type_ignores=[]), 'wizard_rig_access', 'exec'))

def curves(action):
    return [f for layer in action.layers for strip in layer.strips for bag in strip.channelbags for f in bag.fcurves]

def action_hash(action):
    return hashlib.sha256(json.dumps([(f.data_path, f.array_index,
        [(list(k.co), list(k.handle_left), list(k.handle_right), k.interpolation) for k in f.keyframe_points])
        for f in curves(action)], sort_keys=True).encode()).hexdigest()

old_hashes = {a.name: action_hash(a) for a in bpy.data.actions}
backups = {}
for label in ('Idle', 'Walk', 'Melee', 'Cast', 'HeavyCast', 'Summon', 'Rain', 'Hit', 'Death'):
    old = bpy.data.actions.get('MC_Wizard_' + label)
    if old:
        name = 'MC_PreFreshV6_Wizard_' + label
        assert not bpy.data.actions.get(name)
        backups[old.name] = name
        old.name = name

# Small, asymmetrical neutral. Palms share a comfortable inward/front family;
# wrist orientation changes use SLERP and never flip the palm through 180deg.
BASE = dict(root=(0, 0, -.075), pelvis=(0, 0, 0), body=(-2, 0, -5), head=(-5, 0, 6),
    r=(-.40, -.25, .63), l=(.34, -.32, .84),
    rn=(.85, -.32, .40), ln=(-.85, -.32, .40), rf=(0, -1, -.25), lf=(0, -1, -.25),
    fr=(-.235, .19, .156448588), fl=(.235, .13, .156448588),
    frr=(0, 0, -8), flr=(0, 0, 8), rg=.19, lg=.14)
def pose(**changes):
    return {**BASE, **changes}

reset()
set_matrix('c_root_master.x', rest['c_root_master.x'].translation + Vector(BASE['root']))
bpy.context.view_layer.update()
hips = {n: rig.pose.bones[n].matrix.copy() for n in ('c_thigh_b.l', 'c_thigh_b.r')}

def palm_rotation(normal, direction):
    z = Vector(normal).normalized()
    y = Vector(direction)
    y = (y - z * y.dot(z)).normalized()
    x = y.cross(z).normalized()
    return Matrix(((x.x, y.x, z.x), (x.y, y.y, z.y), (x.z, y.z, z.z))).to_quaternion()

def blend(first, second, amount):
    p = {k: tuple(a + (b-a)*amount for a, b in zip(first[k], second[k])) if isinstance(first[k], tuple)
        else first[k] + (second[k]-first[k])*amount for k in first}
    for side in ('r', 'l'):
        q0 = palm_rotation(first[side+'n'], first[side+'f'])
        q1 = palm_rotation(second[side+'n'], second[side+'f'])
        if q0.dot(q1) < 0:
            q1.negate()
        q = q0.slerp(q1, amount)
        p[side+'n'], p[side+'f'] = tuple(q @ Vector((0,0,1))), tuple(q @ Vector((0,1,0)))
    return p

def smooth(x):
    x = max(0, min(1, x))
    return x*x*x*(x*(x*6-15)+10)

def performance(knots, t):
    for (start, first), (end, second) in zip(knots, knots[1:]):
        if t <= end:
            return blend(first, second, smooth((t-start)/(end-start)))
    return dict(knots[-1][1])

def apply(p):
    reset()
    set_matrix('c_root_master.x', rest['c_root_master.x'].translation + Vector(p['root']), p['body'])
    bpy.context.view_layer.update()
    for name, anchor in hips.items():
        m = anchor.copy()
        m.translation += Vector(p['pelvis'])
        rig.pose.bones[name].matrix = m
    bpy.context.view_layer.update()
    set_matrix('c_head.x', degrees=p['head'], current=True)
    for side in ('r', 'l'):
        b = rig.pose.bones['c_shoulder.'+side]
        m = b.matrix.copy()
        sign = -1 if side == 'r' else 1
        toward = (Vector(p[side]) - m.translation).normalized()
        direction = (toward*.83 + Vector((sign,0,0))*.17).normalized()
        current = (m.to_3x3() @ Vector((0,1,0))).normalized()
        q = current.rotation_difference(direction).to_matrix().to_4x4() @ m
        q.translation = m.translation
        b.matrix = q
    bpy.context.view_layer.update()
    for side in ('r', 'l'):
        sign = -1 if side == 'r' else 1
        q = palm_rotation(p[side+'n'], p[side+'f']).to_matrix().to_4x4()
        q.translation = Vector(p[side])
        rig.pose.bones['c_hand_ik.'+side].matrix = q
        set_matrix('c_foot_ik.'+side, p['f'+side], p['f'+side+'r'])
        set_matrix('c_arms_pole.'+side, (sign*.84, -.02, .60))
        set_matrix('c_leg_pole.'+side, (sign*.26, -.38, .30))
        rig.pose.bones['c_hand_fk.'+side]['fingers_grasp'] = 0.0
    rig.update_tag()
    bpy.context.view_layer.update()
    # The mesh's original rest fingers form a wide V. Bring both long digit
    # bases to a quiet nearly parallel family rather than hiding the V in fists.
    for side in ('r','l'):
        rotation=palm_rotation(p[side+'n'],p[side+'f'])
        y=rotation @ Vector((0,1,0));n=rotation @ Vector((0,0,1));x=y.cross(n)
        for digit,offset in (('pinky',-.065),('ring',.065)):
            bone=rig.pose.bones['c_'+digit+'1.'+side]
            m=bone.matrix.copy();current=(m.to_3x3() @ Vector((0,1,0))).normalized()
            target=(y+x*offset).normalized()
            q=current.rotation_difference(target).to_matrix().to_4x4() @ m
            q.translation=m.translation;bone.matrix=q
    bpy.context.view_layer.update()
    for side in ('r','l'):
        rotation=palm_rotation(p[side+'n'],p[side+'f'])
        y=rotation @ Vector((0,1,0));n=rotation @ Vector((0,0,1));x=y.cross(n)
        for digit in ('pinky','ring','thumb'):
            for joint,factor in ((1,.30),(2,.60),(3,.45)):
                bone=rig.pose.bones['c_'+digit+str(joint)+'.'+side]
                axis=bone.matrix.to_quaternion().inverted() @ x
                rotation=Quaternion(axis,math.radians(p[side+'g']*40*factor)).to_matrix().to_4x4()
                bone.matrix_basis=bone.matrix_basis @ rotation
    bpy.context.view_layer.update()

IDLE = pose()
def idle(t):
    a = 2*math.pi*t
    b = math.sin(a)
    p = pose(root=(.008*b, 0, -.075+.009*math.sin(a+.2)), pelvis=(.003*b,0,0),
        body=(-2-1.5*b,0,-5+2*b), head=(-5+2.3*math.sin(a+.4),0,6+8*math.sin(a-.2)),
        r=(-.40-.011*b,-.25,.63+.010*b), l=(.34+.010*b,-.32,.84-.008*b),
        rg=.19+.035*math.sin(a+.8), lg=.14+.025*math.sin(a-.4))
    return p

GATHER = pose(root=(-.025, .015, -.12), pelvis=(-.015,0,-.025), body=(8,0,-12), head=(-10,0,10),
    r=(-.23,-.44,.74), l=(.23,-.43,.71), rn=(.75,-.18,.65), ln=(-.75,-.18,.65), rf=(-.1,-.8,.6),lf=(.1,-.8,.6),rg=.28,lg=.24)
COIL = pose(root=(-.035,.025,-.14), pelvis=(-.020,0,-.035), body=(-22,8,-35),head=(6,-2,10),
    r=(-.53,-.16,1.03),l=(.25,-.48,.74),rn=(.75,-.35,.55),ln=(-.65,-.35,.75),rf=(-.1,-.6,.8),lf=(.1,-.7,.7),rg=.30,lg=.21)
THROW = pose(root=(.030,-.075,-.10),pelvis=(.018,-.015,-.015),body=(28,-12,35),head=(-5,3,-15),
    r=(-.35,-.78,.93),l=(.35,-.27,.57),rn=(.28,-.92,.28),ln=(-.7,-.25,.65),rf=(-.25,0,1),rg=.035,lg=.12)
FOLLOW = pose(**{**THROW,'root':(.040,-.090,-.115),'body':(33,-14,40),'head':(-10,5,-19),
    'r':(-.45,-.70,1.07),'l':(.37,-.24,.60)})
FIRE_BACK = pose(root=(-.006,0,-.10),body=(-5,0,-10),head=(3,0,8),r=(-.48,-.29,.87),l=(.29,-.39,.84),rg=.12)
def fire(t):
    return performance([(0,IDLE),(.19,GATHER),(.27,GATHER),(.41,COIL),(.48,COIL),
        (.55,THROW),(.64,FOLLOW),(.82,FIRE_BACK),(1,IDLE)],t)

SUM_LOW = pose(root=(0,-.025,-.265),pelvis=(0,-.01,-.12),body=(30,0,-6),head=(-24,0,5),
    r=(-.38,-.54,.30),l=(.39,-.51,.34),rn=(.7,-.2,.65),ln=(-.7,-.2,.65),rf=(-.2,-1,.15),lf=(.2,-1,.15),rg=.20,lg=.18)
SUM_GRIP = pose(**{**SUM_LOW,'root':(0,-.025,-.25),'body':(26,0,-10),
    'r':(-.30,-.58,.45),'l':(.31,-.55,.48),'rf':(-.1,-.55,.8),'lf':(.1,-.55,.8),'rg':.30,'lg':.30})
SUM_RISE = pose(root=(0,-.045,-.055),pelvis=(0,-.012,.008),body=(-13,0,9),head=(-14,0,-8),
    r=(-.43,-.38,1.14),l=(.42,-.42,1.23),rf=(-.1,-.6,.8),lf=(.1,-.6,.8),rg=.055,lg=.07)
SUM_COMMAND = pose(root=(.016,-.02,-.085),pelvis=(.008,0,0),body=(-5,0,17),head=(-8,0,-15),
    r=(-.34,-.62,1.08),l=(.32,-.28,.76),rn=(.3,-.9,.4),ln=(-.85,-.3,.35),rf=(-.1,-.2,1),rg=.09,lg=.18)
def summon(t):
    return performance([(0,IDLE),(.22,SUM_LOW),(.30,SUM_LOW),(.41,SUM_GRIP),(.46,SUM_GRIP),
        (.55,SUM_RISE),(.63,pose(**{**SUM_RISE,'root':(0,-.05,-.035),'body':(-17,0,12)})),
        (.76,SUM_COMMAND),(.84,SUM_COMMAND),(1,IDLE)],t)

TELE_GATHER = pose(root=(0,.01,-.14),pelvis=(0,0,-.02),body=(12,0,-13),head=(-18,0,12),
    r=(-.25,-.40,.73),l=(.24,-.39,.78),rg=.27,lg=.28)
TELE_FOLD = pose(root=(0,.025,-.285),pelvis=(0,0,-.11),body=(31,0,-20),head=(-24,0,18),
    r=(-.19,-.40,.68),l=(.19,-.39,.73),rn=(.9,-.2,.25),ln=(-.9,-.2,.25),rg=.34,lg=.34)
TELE_OPEN = pose(root=(-.01,-.03,-.035),pelvis=(-.01,0,.01),body=(-17,0,20),head=(-10,0,-18),
    r=(-.57,-.25,1.19),l=(.42,-.30,.98),rn=(.65,-.5,.55),ln=(-.75,-.4,.5),rf=(-.2,-.7,.7),lf=(.1,-.7,.7),rg=.05,lg=.08)
TELE_SWAGGER = pose(root=(.014,0,-.075),body=(-8,0,13),head=(-8,0,-12),
    r=(-.46,-.22,.89),l=(.33,-.34,1.04),rg=.16,lg=.11)
def teleport(t):
    return performance([(0,IDLE),(.23,TELE_GATHER),(.37,TELE_FOLD),(.49,TELE_FOLD),
        (.55,pose(**{**TELE_FOLD,'root':(0,.027,-.30),'head':(-28,0,20)})),(.67,TELE_OPEN),
        (.75,TELE_OPEN),(.88,TELE_SWAGGER),(1,IDLE)],t)

RAIN_LOW = pose(root=(0,-.015,-.22),pelvis=(0,0,-.08),body=(23,0,-6),head=(-18,0,7),
    r=(-.29,-.48,.50),l=(.29,-.48,.53),rg=.24,lg=.22)
RAIN_CROWN = pose(root=(0,-.01,-.045),body=(-13,0,2),head=(-23,0,-2),
    r=(-.32,-.29,1.58),l=(.31,-.31,1.45),rn=(.55,-.25,-.8),ln=(-.7,-.4,.6),
    rf=(0,-1,.1),rg=.06,lg=.08)
def rain_loop(u):
    a=2*math.pi*u
    # One palm presses the sky downward while the lower hand supports. Vertical
    # pressure replaces circles; the exact cycle repeats at55/85%.
    return pose(root=(.014*math.sin(a),-.018,-.055+.008*math.sin(a)),pelvis=(.006*math.sin(a),0,0),
        body=(-10+3*math.sin(a),0,7+7*math.sin(a)),head=(-20+3*math.sin(a+.5),0,-6-7*math.sin(a)),
        r=(-.32,-.32,1.59+.050*math.cos(a)),
        l=(.30,-.33,.72+.025*math.sin(a)),
        rn=(.55,-.25,-.8),ln=(-.7,-.3,.7),rf=(0,-1,.1),
        rg=.10+.025*math.sin(a),lg=.12-.025*math.sin(a))
def rain(t):
    if t<.55:
        return performance([(0,IDLE),(.19,RAIN_LOW),(.27,RAIN_LOW),(.43,RAIN_CROWN),(.47,RAIN_CROWN),(.55,rain_loop(0))],t)
    if t<=.85:
        return rain_loop((t-.55)/.30)
    return performance([(.85,rain_loop(1)),(.94,pose(root=(0,0,-.09),head=(-9,0,8))),(1,IDLE)],t)

SLAP_OFFEND = pose(root=(-.03,.02,-.12),pelvis=(-.015,0,-.02),body=(-11,0,17),head=(10,0,-24),
    r=(-.29,-.39,1.01),l=(.40,-.29,.79),rn=(.75,-.35,.55),rg=.14,lg=.19)
SLAP_LOAD = pose(root=(-.035,.024,-.145),pelvis=(-.02,0,-.03),body=(-13,0,31),head=(13,0,-31),
    r=(-.18,-.34,1.06),l=(.38,-.27,.76),rn=(.85,-.3,.4),rf=(-.1,-.6,.8),rg=.06,lg=.17)
SLAP_CONTACT = pose(root=(.025,-.04,-.085),pelvis=(.015,-.01,0),body=(12,0,-22),head=(-9,0,16),
    r=(-.52,-.64,.96),l=(.40,-.20,.78),rn=(.9,-.3,.1),rf=(-.12,-.1,1),rg=.02,lg=.18)
SLAP_FOLLOW = pose(**{**SLAP_CONTACT,'body':(16,0,-28),'head':(-13,0,22),'r':(-.58,-.54,.91)})
SLAP_DISMISS = pose(root=(0,0,-.10),body=(-4,0,11),head=(-10,0,-16),r=(-.43,-.27,.82),l=(.35,-.32,.94),rg=.12)
def melee(t):
    return performance([(0,IDLE),(.18,SLAP_OFFEND),(.35,SLAP_LOAD),(.48,SLAP_LOAD),(.55,SLAP_CONTACT),
        (.61,SLAP_FOLLOW),(.77,SLAP_DISMISS),(.84,SLAP_DISMISS),(1,IDLE)],t)

FLINCH = pose(root=(.035,.055,-.14),pelvis=(.018,.01,-.035),body=(-22,0,-18),head=(24,0,22),
    r=(-.46,-.22,.86),l=(.40,-.20,1.02),rg=.25,lg=.24)
CORRECT = pose(root=(-.018,-.01,-.095),pelvis=(-.01,0,-.01),body=(7,0,10),head=(-9,0,-12),
    r=(-.38,-.29,.78),l=(.34,-.31,.95),rg=.20,lg=.18)
def hit(t):
    return performance([(0,IDLE),(.13,FLINCH),(.25,FLINCH),(.56,CORRECT),(.75,pose(head=(-8,0,-5))),(1,IDLE)],t)

DEFY = pose(root=(-.02,.02,-.10),pelvis=(-.012,0,-.015),body=(-12,0,-16),head=(5,0,22),
    r=(-.38,-.30,.76),l=(.38,-.48,1.17),ln=(-.45,-.75,.5),rg=.17,lg=.08)
BUCKLE = pose(root=(.035,-.02,-.31),pelvis=(.02,-.015,-.18),body=(22,0,16),head=(-18,0,-10),
    r=(-.42,-.40,.48),l=(.38,-.45,.63),rg=.13,lg=.15)
FALL = pose(root=(.06,-.075,-.22),pelvis=(.04,-.04,-.24),body=(62,0,24),head=(4,0,-5),
    r=(-.48,-.48,.24),l=(.34,-.50,.32),fl=(.40,-.025,.156448588),flr=(0,0,20),
    rf=(-.2,-1,.15),lf=(.2,-1,.15),rg=.08,lg=.11)
DEAD = pose(root=(.065,-.10,-.215),pelvis=(.035,-.035,-.23),body=(69,0,26),head=(10,0,-6),
    r=(-.49,-.45,.22),l=(.37,-.42,.24),fr=(-.31,.12,.156448588),fl=(.52,-.10,.156448588),flr=(0,0,32),
    rf=(-.2,-1,.15),lf=(.2,-1,.15),rg=.08,lg=.09)
def death(t):
    return performance([(0,IDLE),(.16,DEFY),(.31,DEFY),(.48,BUCKLE),(.58,BUCKLE),(.73,FALL),
        (.81,DEAD),(.86,pose(**{**DEAD,'root':(.065,-.10,-.197),'body':(66,0,25)})),(.91,DEAD),(1,DEAD)],t)

def walk(t):
    # Grounded target-facing backsteps. Each foot has a linear stance and a
    # lifted return arc; both endpoints and velocities match the cycle seam.
    def foot(phase, x):
        phase%=1
        if phase<.60:
            u=phase/.60
            return (x,.29-.24*u,.156448588),(0,0,-8 if x<0 else 8)
        u=(phase-.60)/.40
        return (x,.05+.24*smooth(u),.156448588+.092*math.sin(math.pi*u)**2),(-12*math.sin(math.pi*u),0,-8 if x<0 else 8)
    fr,frr=foot(t,-.235);fl,flr=foot(t+.5,.235)
    a=2*math.pi*t
    return pose(root=(.018*math.sin(a),0,-.065+.012*math.cos(2*a)),pelvis=(.014*math.sin(a),0,.005*math.cos(2*a)),
        body=(-3,0,-3+3*math.sin(a)),head=(-5,0,3-3*math.sin(a)),fr=fr,fl=fl,frr=frr,flr=flr,
        r=(-.41,-.27,.73+.015*math.sin(a)),l=(.35,-.32,.92-.012*math.sin(a)),rg=.17,lg=.15)

LIB = {
    'Idle':(idle,121,True,'Quiet haughty stance, loose asymmetric hands, tiny fingers, delayed scanning gaze.'),
    'Walk':(walk,49,True,'Grounded deliberate backsteps; stable forward gaze and small caster-ready arm response.'),
    'Melee':(melee,101,False,'Offended torso coil, one contemptuous backhand at55%, dismissive recoil and composure.'),
    'Cast':(fire,101,False,'Compact belly cradle, drawn-back casting arm, coiled body, open right-palm flick and soft settle.'),
    'HeavyCast':(teleport,101,False,'Cloak-like belly gather and head duck, tight vanish fold at55%, asymmetric grand arrival.'),
    'Summon':(summon,101,False,'Deep low ground gather, gripped portal pull with rising body, one imperious command hand.'),
    'Rain':(rain,101,False,'Low gather, high crown, asymmetric conductor loop55–85% with exact repeating seam.'),
    'Hit':(hit,13,False,'Whole-body quick flinch, contained over-correction, offended regain of composure in0.4s.'),
    'Death':(death,101,False,'Last proud defiance, loss of balance and knees, collapsed finished dead pose with final stillness.'),
}

def continuous_branches(action):
    for curve in curves(action):
        if curve.data_path.endswith('rotation_euler'):
            previous=None
            for p in curve.keyframe_points:
                value=float(p.co.y)
                if previous is not None:
                    value += 2*math.pi*round((previous-value)/(2*math.pi))
                delta=value-p.co.y
                p.co.y=value;p.handle_left.y+=delta;p.handle_right.y+=delta
                previous=value
    groups={}
    for curve in curves(action):
        if curve.data_path.endswith('rotation_quaternion'):
            groups.setdefault(curve.data_path,{})[curve.array_index]=curve
    for group in groups.values():
        previous=None
        for index in range(len(group[0].keyframe_points)):
            points=[group[c].keyframe_points[index] for c in range(4)]
            values=[float(p.co.y) for p in points]
            if previous and sum(a*b for a,b in zip(previous,values))<0:
                for p in points:
                    p.co.y=-p.co.y;p.handle_left.y=-p.handle_left.y;p.handle_right.y=-p.handle_right.y
                values=[-x for x in values]
            previous=values

records=[]
def mesh_floor_now():
    deps=bpy.context.evaluated_depsgraph_get();ev=mesh.evaluated_get(deps);data=ev.to_mesh()
    try:return min((ev.matrix_world @ v.co).z for v in data.vertices)
    finally:ev.to_mesh_clear()

POSE_KEYS={
    'Cast':(1,20,28,42,49,56,65,83,101),'Summon':(1,23,31,42,47,56,64,77,85,101),
    'HeavyCast':(1,24,38,50,56,68,76,89,101),'Rain':(1,20,28,44,48,56,86,95,101),
    'Melee':(1,19,36,49,56,62,78,85,101),'Death':(1,17,32,49,59,74,82,87,92,101),
}
for label,(function,end,loop,note) in LIB.items():
    action=bpy.data.actions.new('MC_Wizard_'+label)
    action.use_fake_user=True
    step=4 if label=='Idle' else 2 if label=='Walk' else 1 if label in ('Hit','Death') else 3
    sample_frames=sorted({1,end,*range(1,end+1,step),*POSE_KEYS.get(label,())})
    for frame in sample_frames:
        p=function((frame-1)/(end-1))
        apply(p)
        if label=='Death':
            # Correct every authored falling pose against real deformed mesh
            # geometry. Feet remain planted and the exported root stays fixed.
            for correction in range(2):
                minimum=mesh_floor_now()
                if minimum>=-.0005:
                    break
                lift=-minimum+.003
                for name in ('root','pelvis','r','l'):
                    p[name]=(p[name][0],p[name][1],p[name][2]+lift)
                apply(p)
        rig.animation_data.action=action
        key(frame)
    bind(action)
    continuous_branches(action)
    for curve in curves(action):
        discrete=curve.data_path.endswith('["ik_fk_switch"]') or curve.data_path.endswith('["pole_parent"]')
        for point in curve.keyframe_points:
            point.interpolation='CONSTANT' if discrete else 'BEZIER'
            point.handle_left_type=point.handle_right_type='AUTO_CLAMPED'
        curve.update()
    action.asset_mark();action.asset_data.description=note
    action['game_slot']=label;action['loop']=loop;action['gestures_version']=6
    action['fresh_from_scratch']=True;action['MCSpellGesture']=1.0
    action['release_fraction']=.55 if label in ('Cast','HeavyCast','Melee','Summon','Rain') else 0.0
    if label=='Rain':
        action['channel_loop_start']=.55;action['channel_loop_end']=.85;action['channel_loop_period_seconds']=1.1
    records.append(dict(label=label,action=action.name,start=1,end=end,fps=30,loop=loop,
        duration_seconds=(end-1)/30,authored_key_frames=sample_frames,description=note))
    print('WIZARD_FRESH_V6_BAKED',label,flush=True)
    # Make the early source reviewable immediately. Later checkpoints contain
    # the same finished actions plus each newly authored slot.
    if label in ('Idle','Cast','Rain'):
        scene.render.fps=30
        bpy.ops.wm.save_as_mainfile(filepath=str(OUTPUT))

scene.render.fps=30
scene.frame_start,scene.frame_end=1,121
bind(bpy.data.actions['MC_Wizard_Idle']);scene.frame_set(1)
scene['MC_WizardStyleRevision']='V6 all nine performances freshly posed from rig rest; no reused animation motion.'
bpy.ops.wm.save_as_mainfile(filepath=str(OUTPUT))

# Focused source diagnostics: key poses, finite matrices, floor/contact, exact
# seams. Fast accents can exceed arbitrary angle thresholds; report them.
def floor():
    deps=bpy.context.evaluated_depsgraph_get();ev=mesh.evaluated_get(deps);data=ev.to_mesh()
    try:return min((ev.matrix_world @ v.co).z for v in data.vertices)
    finally:ev.to_mesh_clear()

qa={}
limbs=('arm_stretch.l','arm_stretch.r','forearm_stretch.l','forearm_stretch.r','thigh_stretch.l','thigh_stretch.r','leg_stretch.l','leg_stretch.r')
for rec in records:
    bind(bpy.data.actions[rec['action']]);end=rec['end']
    frames=list(range(1,end+1)) if rec['label']=='Death' else sorted({1,end,*[1+round((end-1)*t) for t in (.15,.22,.30,.41,.48,.55,.63,.73,.81,.85,.91)]})
    minz=100.;ratios=[1.,1.];feet={n:[] for n in ('foot.l','foot.r')}
    for frame in frames:
        scene.frame_set(frame);bpy.context.view_layer.update()
        assert all(math.isfinite(v) for b in rig.pose.bones for row in b.matrix for v in row), (rec['label'],frame)
        minz=min(minz,floor())
        for name in limbs:
            b=rig.pose.bones[name];ratio=(b.tail-b.head).length/rig.data.bones[name].length
            ratios[0]=min(ratios[0],ratio);ratios[1]=max(ratios[1],ratio)
        for name in feet:feet[name].append(rig.pose.bones[name].head.copy())
    drift={name:max((p-points[0]).length for p in points) for name,points in feet.items()}
    seam=None
    if rec['loop'] or rec['label']=='Rain':
        start,finish=(56,86) if rec['label']=='Rain' else (1,end)
        scene.frame_set(start);bpy.context.view_layer.update()
        first={b.name:b.matrix.copy() for b in rig.pose.bones if b.bone.use_deform}
        scene.frame_set(finish);bpy.context.view_layer.update()
        seam=max(abs(v-rig.pose.bones[name].matrix[i][j]) for name,m in first.items() for i,row in enumerate(m) for j,v in enumerate(row))
        assert seam<.0001,(rec['label'],'seam',seam)
    qa[rec['label']]=dict(frames_checked=frames,finite=True,mesh_min_z_m=minz,
        limb_length_ratio_range=ratios,foot_drift_m=drift,loop_seam_max_matrix_difference=seam)
    assert minz>=-.003,(rec['label'],'floor penetration',minz)
    assert .945<ratios[0] and ratios[1]<1.055,(rec['label'],'limb stretch',ratios)
    print('WIZARD_FRESH_V6_QA',rec['label'],minz,ratios,flush=True)

assert signature()==rest_before
assert mesh_signature()==mesh_before
assert digest(SOURCE)==source_sha
for name,value in old_hashes.items():
    assert action_hash(bpy.data.actions[backups.get(name,name)])==value,name
bind(bpy.data.actions['MC_Wizard_Idle']);scene.frame_set(1)
bpy.ops.wm.save_as_mainfile(filepath=str(OUTPUT))
report=dict(passed=True,blend=str(OUTPUT),blend_sha256=digest(OUTPUT),source=str(SOURCE),source_sha256=source_sha,
    source_unchanged=True,previous_actions_preserved=True,rest_preserved=True,meshes_weights_preserved=True,
    production_count=9,draft_cast_only=False,records=records,qa=qa,game_imported=False,
    scope='Newly constructed poses and curves for all nine. Focused source pose/contact diagnostics; export performs exact production bind and hierarchy checks.')
(OUT/'WizardFreshV6Report.json').write_text(json.dumps(report,indent=2),encoding='utf8')
print('WIZARD_FRESH_V6_COMPLETE',str(OUTPUT),flush=True)
