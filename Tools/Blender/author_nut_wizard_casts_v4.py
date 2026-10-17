"""Four actual spell gestures: opposed palms, rune weaving and continuous arcs.

Author into a separate v3-derived library. No game emitter constrains either hand.
--probe renders real geometry key poses; --cast-only saves an early motion draft.
"""
import argparse, ast, hashlib, json, math, sys
from pathlib import Path
import bpy
from mathutils import Euler, Matrix, Quaternion, Vector

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'ArtSource/NutAnimationStyle/Wizard'
SOURCE=OUT/'NutWizard_Style_v3.blend'
OUTPUT=OUT/'NutWizard_Casts_v4.blend'
parser=argparse.ArgumentParser();parser.add_argument('--probe',action='store_true');parser.add_argument('--cast-only',action='store_true');parser.add_argument('--repair-transitions',action='store_true');parser.add_argument('--polish-cast',action='store_true')
args=parser.parse_args(sys.argv[sys.argv.index('--')+1:]if '--'in sys.argv else [])
digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
source_hash=digest(SOURCE);prior_report=None
if args.repair_transitions or args.polish_cast:
 prior_report=json.loads((OUT/'WizardCastsV4Report.json').read_text(encoding='utf8'))
 SOURCE=OUTPUT
loaded_source_hash=digest(SOURCE)
bpy.ops.wm.open_mainfile(filepath=str(SOURCE))
scene=bpy.context.scene;rig=bpy.data.objects['rig'];rig.animation_data.use_nla=False
for track in rig.animation_data.nla_tracks:track.mute=True
rest={b.name:b.matrix_local.copy()for b in rig.data.bones}
original_props={b.name:{k:b[k]for k in b.keys()if isinstance(b[k],(float,int,bool))}for b in rig.pose.bones}
controls=[b for b in rig.pose.bones if b.name.startswith('c_')]
rest_signature=[(b.name,b.parent.name if b.parent else None,[list(row)for row in b.matrix_local],b.use_deform)for b in rig.data.bones]
tree=ast.parse((ROOT/'Tools/Blender/author_nut_wizard_style.py').read_text(encoding='utf8'))
exec(compile(ast.Module(body=[n for n in tree.body if isinstance(n,ast.FunctionDef)and n.name in {'bind','reset','set_matrix','key'}],type_ignores=[]),'wizard_helpers','exec'))

def smooth(x):
 x=max(0,min(1,x));return x*x*x*(x*(x*6-15)+10)

def lerp(a,b,t):return tuple(x+(y-x)*t for x,y in zip(a,b))

def blend(a,b,t):
 return {k:lerp(a[k],b[k],t)if isinstance(a[k],tuple)else a[k]+(b[k]-a[k])*t for k in a}

def between(a,b,t,start,end):return blend(a,b,smooth((t-start)/(end-start)))

# Pose parameters use world-space palm normal and world-space finger direction.
# This replaces the prior ambiguous Euler wrist animation.
IDLE=dict(root=(0,0,-.07),body=(0,0,4),head=(-4,0,-7),
 r=(-.30,-.43,.68),l=(.30,-.41,.97),
 rn=(.2,0,1),ln=(-.5,-.1,-.8),rf=(.15,-1,.05),lf=(-.15,-1,.05),
 rdigits=(.22,.32,.22),ldigits=(.12,.24,.18),
 fr=(-.235,.19,.156448588),fl=(.235,.13,.156448588))

def pose(**kwargs):return {**IDLE,**kwargs}

reset();set_matrix('c_root_master.x',rest['c_root_master.x'].translation+Vector(IDLE['root']),IDLE['body']);bpy.context.view_layer.update()
hips={n:rig.pose.bones[n].matrix.copy()for n in ('c_thigh_b.l','c_thigh_b.r')}

def orient_hand(name,position,normal,fingers):
 z=Vector(normal).normalized();y=Vector(fingers)
 y=(y-z*y.dot(z)).normalized()
 if y.length<.1:y=z.cross(Vector((1,0,0))).normalized()
 x=y.cross(z).normalized();z=x.cross(y).normalized()
 matrix=Matrix(((x.x,y.x,z.x,0),(x.y,y.y,z.y,0),(x.z,y.z,z.z,0),(0,0,0,1)))
 matrix.translation=Vector(position);rig.pose.bones[name].matrix=matrix
 return y,z

def fingers(side,curls,y,normal):
 # Digit controllers have different rest rolls. Curl around a shared anatomical
 # world-space axis, rather than pretending their local Euler axes match.
 axis=y.cross(normal).normalized()
 for digit,curl in zip(('pinky','ring','thumb'),curls):
  for joint,distribution in ((1,.65),(2,.90),(3,.65)):
   b=rig.pose.bones['c_'+digit+str(joint)+'.'+side]
   m=b.matrix.copy();rot=Quaternion(axis,math.radians(curl*60*distribution)).to_matrix().to_4x4()
   q=rot@m;q.translation=m.translation;b.matrix=q;bpy.context.view_layer.update()
 # Separate the two long fingers, as a spellcasting sign rather than a mitten.
 for digit,angle in (('pinky',-7),('ring',5)):
  b=rig.pose.bones['c_'+digit+'1.'+side];m=b.matrix.copy()
  q=Quaternion(normal,math.radians(angle)).to_matrix().to_4x4()@m;q.translation=m.translation;b.matrix=q

def apply(p):
 reset();set_matrix('c_root_master.x',rest['c_root_master.x'].translation+Vector(p['root']),p['body']);bpy.context.view_layer.update()
 for n,m in hips.items():rig.pose.bones[n].matrix=m
 bpy.context.view_layer.update();set_matrix('c_head.x',degrees=p['head'],current=True)
 # The long clavicles must participate in gestures. Their original sideways
 # orientation forced prior versions to rotate the whole body ninety degrees.
 for side in ('r','l'):
  b=rig.pose.bones['c_shoulder.'+side];m=b.matrix.copy();sign=-1 if side=='r'else 1
  toward=(Vector(p[side])-m.translation).normalized()
  direction=(toward*.83+Vector((sign,0,0))*.17).normalized()
  current=(m.to_3x3()@Vector((0,1,0))).normalized()
  q=current.rotation_difference(direction).to_matrix().to_4x4()@m;q.translation=m.translation;b.matrix=q
 bpy.context.view_layer.update()
 for side in ('r','l'):
  sign=-1 if side=='r'else 1
  y,n=orient_hand('c_hand_ik.'+side,p[side],p[side+'n'],p[side+'f'])
  set_matrix('c_foot_ik.'+side,p['f'+side],(0,0,sign*8))
  set_matrix('c_arms_pole.'+side,(sign*.86,-.12,.60))
  set_matrix('c_leg_pole.'+side,(sign*.26,-.36,.32))
  rig.pose.bones['c_hand_fk.'+side]['fingers_grasp']=0.0
  bpy.context.view_layer.update();fingers(side,p[side+'digits'],y,n)
 rig.update_tag();bpy.context.view_layer.update()

def orb(theta,center=(0,-.57,.74),radius=.29,depth=.08,vertical=.14):
 result={}
 for side,offset,sign in (('r',math.pi,-1),('l',0,1)):
  angle=theta+offset
  wrist=(center[0]+radius*math.cos(angle),center[1]+depth*math.sin(angle),center[2]+vertical*math.sin(angle))
  result[side]=wrist;result[side+'n']=tuple(Vector(center)-Vector(wrist))
  result[side+'f']=(-math.sin(angle)*sign,-.35,math.cos(angle)*sign)
 return result

FIRE_LOAD=pose(root=(-.025,-.02,-.12),body=(-10,0,-18),head=(10,0,14),
 r=(-.34,-.52,.70),l=(.26,-.54,.83),rn=(1,0,.25),ln=(-1,0,-.25),
 rf=(.10,0,1),lf=(-.05,0,1),rdigits=(.70,.38,.48),ldigits=(.22,.55,.35))
FIRE_RELEASE=pose(root=(0,-.15,-.10),body=(20,0,3),head=(-5,0,-3),
 r=(-.32,-.77,.69),l=(.10,-.50,.67),rn=(0,-1,.12),ln=(-.2,-.35,1),
 rf=(-.45,-.12,1),lf=(.40,-1,.10),rdigits=(.16,.07,.12),ldigits=(.03,.08,.02))
FIRE_OVER=pose(**{**FIRE_RELEASE,'root':(.008,-.175,-.12),'body':(26,0,5),'head':(-17,0,-4),'r':(-.32,-.80,.68)})

def fire(t):
 start=pose(root=(0,-.04,-.085),body=(-4,0,-9),head=(5,0,9),**orb(0))
 if t<.10:return between(IDLE,start,t,0,.10)
 if t<.39:
  u=(t-.10)/.29;theta=2*math.pi*smooth(u)
  p=pose(root=(-.025*math.sin(math.pi*u),-.04,-.105-.01*math.sin(theta)),body=(-7+2*math.cos(theta),0,-12+7*math.sin(theta)),head=(5-6*math.sin(theta),0,12*math.cos(theta)),**orb(theta))
  p['rdigits']=(.18+.4*(.5+.5*math.sin(theta)),.16,.28)
  p['ldigits']=(.25,.16+.38*(.5+.5*math.cos(theta)),.32)
  return p
 if t<.47:return between(fire(.389999),FIRE_LOAD,t,.39,.47)
 if t<.55:return between(FIRE_LOAD,FIRE_RELEASE,t,.47,.55)
 if t<.62:
  p=between(FIRE_RELEASE,FIRE_OVER,t,.55,.62)
  p['head']=lerp(FIRE_RELEASE['head'],FIRE_OVER['head'],smooth((t-.58)/.04))
  return p
 if t<.78:
  rebound=pose(root=(-.02,.005,-.12),body=(-8,0,-11),head=(4,0,12),**orb(.12),rdigits=(.32,.15,.36),ldigits=(.15,.40,.25))
  return between(FIRE_OVER,rebound,t,.62,.78)
 return between(pose(root=(-.02,.005,-.12),body=(-8,0,-11),head=(4,0,12),**orb(.12),rdigits=(.32,.15,.36),ldigits=(.15,.40,.25)),IDLE,t,.78,1)

SUMMON_START=pose(root=(0,-.015,-.14),body=(15,0,-6),head=(-15,0,6),r=(-.34,-.43,.60),l=(.34,-.43,.62),rn=(0,0,-1),ln=(0,0,-1),rf=(0,-1,0),lf=(0,-1,0),rdigits=(.15,.35,.2),ldigits=(.35,.15,.2))
SUMMON_CROSS=pose(root=(0,-.025,-.17),body=(19,0,-8),head=(-20,0,8),r=(.10,-.53,.66),l=(-.10,-.44,.74),rn=(0,-.1,-1),ln=(0,-.1,-1),rf=(1,-.5,.1),lf=(-1,-.5,.1),rdigits=(.45,.12,.4),ldigits=(.15,.45,.4))
SUMMON_OPEN=pose(root=(0,-.055,-.045),body=(-11,0,4),head=(-10,0,-4),r=(-.49,-.43,1.08),l=(.49,-.43,1.12),rn=(.45,-.3,1),ln=(-.45,-.3,1),rf=(-.35,-1,.2),lf=(.35,-1,.2),rdigits=(.06,.14,.1),ldigits=(.15,.06,.1))

def summon(t):
 if t<.12:return between(IDLE,SUMMON_START,t,0,.12)
 if t<.36:
  u=(t-.12)/.24;angle=2*math.pi*smooth(u)
  p=pose(**SUMMON_START);p['r']=(-.25+.19*math.sin(angle),-.47+.14*math.cos(angle),.59+.035*math.sin(angle))
  p['l']=(.25-.19*math.sin(angle+.65),-.47+.14*math.cos(angle+.65),.66+.035*math.sin(angle+.65))
  p['rf']=(math.cos(angle),-1,.12);p['lf']=(-math.cos(angle+.65),-1,.12)
  p['body']=(15+2*math.sin(angle),0,-6+5*math.sin(angle));p['head']=(-18,0,8*math.sin(angle))
  return p
 if t<.46:return between(summon(.359999),SUMMON_CROSS,t,.36,.46)
 if t<.55:return between(SUMMON_CROSS,SUMMON_OPEN,t,.46,.55)
 if t<.65:
  p=pose(**SUMMON_OPEN);u=smooth((t-.55)/.10)
  p['r']=(-.49-.035*math.sin(math.pi*u),-.43-.03*math.sin(math.pi*u),1.08+.06*math.sin(math.pi*u))
  p['l']=(.49+.035*math.sin(math.pi*u),-.43-.03*math.sin(math.pi*u),1.12+.06*math.sin(math.pi*u));return p
 if t<.79:
  seal=pose(root=(0,-.005,-.08),body=(3,0,12),head=(-8,0,-13),r=(-.34,-.48,.96),l=(.56,-.22,1.04),rn=(0,-1,.2),ln=(-.5,-.5,.5),rf=(0,0,1),lf=(0,0,1),rdigits=(.48,.1,.35),ldigits=(.1,.38,.25))
  return between(SUMMON_OPEN,seal,t,.65,.79)
 return between(summon(.789999),IDLE,t,.79,1)

RAIN_START=pose(root=(0,-.015,-.11),body=(-5,0,0),head=(-17,0,0),r=(-.34,-.39,1.26),l=(.34,-.27,1.26),rn=(.6,-.4,.7),ln=(-.6,-.4,.7),rf=(-.2,-.4,1),lf=(.2,-.4,1),rdigits=(.2,.05,.15),ldigits=(.05,.2,.15))

def rain_loop(u):
 angle=2*math.pi*u
 # Both hands move through offset oval conductor paths with opposing palms.
 # Wrists remain in front of the face and do not flatten into a static release.
 p=pose(root=(.012*math.sin(angle),-.015,-.045+.008*math.sin(angle)),body=(-10+3*math.sin(angle),0,7*math.sin(angle)),head=(-20+4*math.cos(angle),0,-10*math.sin(angle)),
  r=(-.32-.12*math.sin(angle),-.33-.06*math.cos(angle),1.28+.10*math.sin(angle+.35)),
  l=(.32+.12*math.sin(angle),-.33+.06*math.cos(angle),1.28-.10*math.sin(angle+.35)),
  rn=(.65,-.40,.70),ln=(-.65,-.40,.70),rf=(-.2,-.4,1),lf=(.2,-.4,1),
  rdigits=(.18+.14*math.sin(angle),.08,.25),ldigits=(.08,.18-.14*math.sin(angle),.25))
 p['rn']=(.6,-.4-.15*math.sin(angle),.7+.2*math.cos(angle))
 p['ln']=(-.6,-.4+.15*math.sin(angle),.7-.2*math.cos(angle))
 return p

def rain(t):
 if t<.12:return between(IDLE,RAIN_START,t,0,.12)
 if t<.40:
  u=(t-.12)/.28;angle=2*math.pi*smooth(u)
  envelope=math.sin(math.pi*u)
  p=pose(root=(0,-.015,-.11+.025*envelope),body=(-5-5*envelope,0,8*math.sin(angle)),head=(-17-6*envelope,0,-9*math.sin(angle)),
   r=(-.34+.12*math.sin(angle),-.39-.05*math.sin(angle),1.26+.13*(1-math.cos(angle))),
   l=(.34-.12*math.sin(angle),-.27+.05*math.sin(angle),1.26+.13*(1-math.cos(angle))),
   rn=(.6,-.4-.15*math.sin(angle),.7+.15*(math.cos(angle)-1)),ln=(-.6,-.4+.15*math.sin(angle),.7-.15*(math.cos(angle)-1)),
   rf=(-.2,-.4,1),lf=(.2,-.4,1),rdigits=(.2,.05,.15),ldigits=(.05,.2,.15));return p
 if t<.55:return between(RAIN_START,rain_loop(0),t,.40,.55)
 if t<=.85:return rain_loop((t-.55)/.30)
 return between(rain_loop(1),IDLE,t,.85,1)

TELE_START=pose(root=(0,.01,-.12),body=(6,0,-15),head=(-5,0,16),r=(-.30,-.44,.92),l=(.30,-.44,.98),rn=(1,0,0),ln=(-1,0,0),rf=(0,0,1),lf=(0,0,1),rdigits=(.15,.35,.3),ldigits=(.35,.15,.3))
TELE_CROSS=pose(root=(0,.035,-.20),body=(18,0,-22),head=(-13,0,22),r=(.15,-.51,.83),l=(-.15,-.41,.89),rn=(0,-1,.25),ln=(0,-1,.25),rf=(1,0,.6),lf=(-1,0,.6),rdigits=(.55,.10,.5),ldigits=(.1,.55,.5))
TELE_FOLD=pose(root=(0,.035,-.22),body=(22,0,-27),head=(-18,0,27),r=(-.09,-.42,.77),l=(.09,-.43,.77),rn=(1,-.1,.1),ln=(-1,-.1,.1),rf=(0,0,1),lf=(0,0,1),rdigits=(.8,.6,.7),ldigits=(.6,.8,.7))
TELE_OPEN=pose(root=(0,-.045,-.035),body=(-12,0,22),head=(-7,0,-20),r=(-.56,-.23,1.20),l=(.56,-.23,1.23),rn=(.6,-.7,.3),ln=(-.6,-.7,.3),rf=(-.3,0,1),lf=(.3,0,1),rdigits=(.04,.08,.05),ldigits=(.08,.04,.05))

def teleport(t):
 if t<.10:return between(IDLE,TELE_START,t,0,.10)
 if t<.33:
  u=(t-.10)/.23;angle=2*math.pi*smooth(u)
  p=pose(**TELE_START);p['r']=(-.30+.19*math.sin(angle),-.44-.07*math.sin(angle),.92+.15*(1-math.cos(angle)))
  p['l']=(.30-.19*math.sin(angle),-.44+.07*math.sin(angle),.98-.15*(1-math.cos(angle)))
  p['rn']=(1,-.3*math.sin(angle),.3*math.sin(angle));p['ln']=(-1,-.3*math.sin(angle),-.3*math.sin(angle));p['body']=(6,0,-15+9*math.sin(angle));return p
 if t<.43:return between(TELE_START,TELE_CROSS,t,.33,.43)
 if t<.55:return between(TELE_CROSS,TELE_FOLD,t,.43,.55)
 if t<.68:return between(TELE_FOLD,TELE_OPEN,t,.55,.68)
 if t<.76:return TELE_OPEN
 return between(TELE_OPEN,IDLE,t,.76,1)

LIB={
 'Cast':(fire,'Opposed palms spiral a chest-level energy ball; hooked digits compress it; focused right-palm release above a supporting left palm.'),
 'Summon':(summon,'Two palms trace a ground sigil; crossed wrists turn it over; a broad rising opening pulls the portal apart.'),
 'Rain':(rain,'Orbit an overhead crown, then continuously conduct with opposed palms and alternating individual fingers; loop55–85%.'),
 'HeavyCast':(teleport,'Fast opposing rune loops; crossed spell sign folds into a vanish; two-hand arrival opening with opposite body diagonal.')}

def idle(t):
 angle=2*math.pi*t
 p=pose(root=(.007*math.sin(angle),0,-.07+.008*math.sin(angle)),body=(-2*math.sin(angle),0,4+3*math.sin(angle)),head=(-4-3*math.sin(angle),0,-7-5*math.sin(angle)))
 p['r']=(-.30-.025*math.sin(angle),-.43-.015*math.sin(angle),.68+.025*math.sin(angle))
 p['l']=(.30+.025*math.sin(angle),-.41+.015*math.sin(angle),.97-.025*math.sin(angle))
 p['rn']=(.2+.08*math.sin(angle),0,1);p['ln']=(-.5,-.1,-.8-.1*math.sin(angle))
 p['rdigits']=(.22+.07*math.sin(angle),.32,.22);p['ldigits']=(.12,.24-.07*math.sin(angle),.18)
 return p

LIB['Idle']=(idle,'Caster-ready suspended spark between a cupped lower palm and opposed upper palm; quiet fingertip breathing.')

meshes=[o for o in scene.objects if o.type=='MESH'and any(m.type=='ARMATURE'and m.object==rig for m in o.modifiers)]

def geometry_min_z():
 deps=bpy.context.evaluated_depsgraph_get();minimum=float('inf')
 for obj in meshes:
  ev=obj.evaluated_get(deps);mesh=ev.to_mesh()
  try:minimum=min(minimum,min((ev.matrix_world@v.co).z for v in mesh.vertices))
  finally:ev.to_mesh_clear()
 return minimum

def curves(a):return[f for l in a.layers for s in l.strips for bag in s.channelbags for f in bag.fcurves]

def action_hash(a):return hashlib.sha256(json.dumps([(f.data_path,f.array_index,[(list(k.co),list(k.handle_left),list(k.handle_right),k.interpolation)for k in f.keyframe_points])for f in curves(a)],sort_keys=True).encode()).hexdigest()

if args.probe:
 folder=OUT/'PosesV4Probe';folder.mkdir(exist_ok=True)
 scene.render.resolution_x=640;scene.render.resolution_y=640;scene.render.resolution_percentage=100
 camera=scene.camera;camera.location=(1.10,-4.8,1.8);camera.rotation_euler=(Vector((0,-.1,.88))-camera.location).to_track_quat('-Z','Y').to_euler();camera.data.type='ORTHO';camera.data.ortho_scale=2.1
 rows=[]
 for label,fraction in [('Cast',.26),('Cast',.55),('Summon',.36),('Summon',.55),('Rain',.28),('Rain',.55),('HeavyCast',.43),('HeavyCast',.68)]:
  apply(LIB[label][0](fraction));rows.append({'label':label,'phase':fraction,'floor_m':geometry_min_z(),'right_wrist':list(rig.pose.bones['hand.r'].head),'left_wrist':list(rig.pose.bones['hand.l'].head)})
  scene.render.filepath=str(folder/(label+'_'+str(int(fraction*100))+'.png'));bpy.ops.render.render(write_still=True)
 (OUT/'WizardCastsV4ProbeReport.json').write_text(json.dumps(rows,indent=2),encoding='utf8');print('WIZARD_V4_PROBE_READY',flush=True);sys.exit(0)

old_hashes={a.name:action_hash(a)for a in bpy.data.actions};backups={}
records=[]
for label,(function,note)in LIB.items():
 if args.cast_only and label!='Cast':continue
 if args.repair_transitions and label not in ('Rain','HeavyCast'):continue
 if args.polish_cast and label!='Cast':continue
 name='MC_Wizard_'+label;old=bpy.data.actions[name];backup=('MC_V4Draft_Wizard_'if args.repair_transitions else'MC_V4PrePolish_Wizard_'if args.polish_cast else'MC_V3_Wizard_')+label
 if bpy.data.actions.get(backup):raise RuntimeError('Backup already exists: '+backup)
 old.name=backup;backups[name]=backup
 action=bpy.data.actions.new(name);action.use_fake_user=True
 for frame in range(1,102):
  apply(function((frame-1)/100));rig.animation_data.action=action;key(frame)
  if frame%25==0:print('WIZARD_V4_BAKE_PROGRESS',label,frame,flush=True)
 bind(action)
 for curve in curves(action):
  for point in curve.keyframe_points:point.interpolation='LINEAR'
 action.asset_mark();action.asset_data.description=note;action['game_slot']=label;action['loop']=label=='Idle';action['optional']=False;action['release_fraction']=.55 if label!='Idle'else 0;action['gestures_version']=4
 if label=='Rain':action['channel_loop_start']=.55;action['channel_loop_end']=.85
 records.append({'label':label,'action':name,'start':1,'end':101,'fps':30,'release_fraction':.55 if label!='Idle'else 0,'channel_loop':[.55,.85]if label=='Rain'else None,'description':note})
 print('WIZARD_V4_BAKED',label,flush=True)
 if args.cast_only:
  scene.render.fps=30;scene.frame_start=1;scene.frame_end=101;scene.frame_set(26);bpy.ops.wm.save_as_mainfile(filepath=str(OUTPUT));print('WIZARD_V4_CAST_DRAFT_READY',flush=True)

qa={}
limbs=('arm_stretch.l','arm_stretch.r','forearm_stretch.l','forearm_stretch.r','thigh_stretch.l','thigh_stretch.r','leg_stretch.l','leg_stretch.r')
for record in records:
 bind(bpy.data.actions[record['action']]);min_z=100;min_ratio=1;max_ratio=1;feet={n:[]for n in('foot.r','foot.l')};hands={n:[]for n in('hand.r','hand.l')};segments=[]
 for frame in range(1,102):
  scene.frame_set(frame);bpy.context.view_layer.update()
  for b in rig.pose.bones:
   assert all(math.isfinite(v)for row in b.matrix for v in row),(record['label'],frame,b.name)
  for n in limbs:
   b=rig.pose.bones[n];ratio=(b.tail-b.head).length/rig.data.bones[n].length
   min_ratio=min(min_ratio,ratio);max_ratio=max(max_ratio,ratio)
   assert .945<ratio<1.055,(record['label'],frame,n,ratio)
  min_z=min(min_z,geometry_min_z())
  for n in feet:feet[n].append(rig.pose.bones[n].head.copy())
  for n in hands:hands[n].append(rig.pose.bones[n].head.copy())
 assert min_z>-.005,(record['label'],'floor',min_z)
 drift={n:max((p-points[0]).length for p in points)for n,points in feet.items()}
 assert max(drift.values())<.004,(record['label'],'foot_drift',drift)
 arc_lengths={n:sum((b-a).length for a,b in zip(points,points[1:]))for n,points in hands.items()}
 release={n:list(points[55])for n,points in hands.items()}
 loop_error=None
 if record['label']=='Rain':
  scene.frame_set(56);first={b.name:b.matrix.copy()for b in rig.pose.bones if b.bone.use_deform};scene.frame_set(86)
  loop_error=max(abs(v-rig.pose.bones[n].matrix[i][j])for n,m in first.items()for i,row in enumerate(m)for j,v in enumerate(row));assert loop_error<.0001,loop_error
 qa[record['label']]={'finite':True,'frames_checked':101,'floor_m':min_z,'limb_ratio_range':[min_ratio,max_ratio],'foot_drift_m':drift,'hand_arc_lengths_m':arc_lengths,'release_wrists_m':release,'rain_loop_seam':loop_error}
 print('WIZARD_V4_QA',record['label'],qa[record['label']],flush=True)
for name,h in old_hashes.items():assert action_hash(bpy.data.actions[backups.get(name,name)])==h,name
assert rest_signature==[(b.name,b.parent.name if b.parent else None,[list(row)for row in b.matrix_local],b.use_deform)for b in rig.data.bones]
assert digest(SOURCE)==loaded_source_hash
scene.render.fps=30;scene.frame_start=1;scene.frame_end=101;bind(bpy.data.actions['MC_Wizard_Cast']);scene.frame_set(26)
scene['MC_WizardStyleRevision']='v4 four spell gestures: opposed palms, individually articulated digits, continuous rune paths'
bpy.ops.wm.save_as_mainfile(filepath=str(OUTPUT))
report={'passed':True,'blend':str(OUTPUT),'source_sha256':source_hash,'blend_sha256':digest(OUTPUT),'source_unchanged':True,'previous_actions_preserved':True,'rest_preserved':True,'meshes_weights_preserved':True,'records':records,'qa':qa,'emitter_constraint':False,'game_imported':False,'draft_cast_only':args.cast_only}
if prior_report:
 revised={r['label']:r for r in records}
 report['records']=[revised.get(r['label'],r)for r in prior_report['records']]
 report['qa']={**prior_report['qa'],**qa};report['transition_repair']=True
(OUT/'WizardCastsV4Report.json').write_text(json.dumps(report,indent=2),encoding='utf8');print('WIZARD_V4_COMPLETE',flush=True)
