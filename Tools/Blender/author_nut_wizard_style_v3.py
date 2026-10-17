"""Wizard v3: deliberately staged magical performances on the unchanged ARP rig.
--probe renders key silhouettes only; default authors and saves an independent library.
"""
import ast, argparse, hashlib, json, math, sys
from pathlib import Path
import bpy
from mathutils import Euler, Matrix, Vector
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'ArtSource/NutAnimationStyle/Wizard'
INPUT=OUT/'NutWizard_Style_v2_Game.blend';OUTPUT=OUT/'NutWizard_Style_v3.blend'
parser=argparse.ArgumentParser();parser.add_argument('--probe',action='store_true')
args=parser.parse_args(sys.argv[sys.argv.index('--')+1:]if '--'in sys.argv else [])
digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
original_paths=[INPUT,OUT/'NutWizard_Style_v2.blend',OUT/'NutWizard_Style.blend',Path('C:/Users/user/Downloads/Telegram Desktop/Nut_Wizard.blend')]
input_hashes={str(p):digest(p)for p in original_paths}
bpy.ops.wm.open_mainfile(filepath=str(INPUT));scene=bpy.context.scene;rig=bpy.data.objects['rig']
rig.animation_data.use_nla=False
for track in rig.animation_data.nla_tracks:track.mute=True
rest={b.name:b.matrix_local.copy()for b in rig.data.bones}
original_props={b.name:{k:b[k]for k in b.keys()if isinstance(b[k],(float,int,bool))}for b in rig.pose.bones}
controls=[b for b in rig.pose.bones if b.name.startswith('c_')]
restsig=[(b.name,b.parent.name if b.parent else None,[list(r)for r in b.matrix_local],b.use_deform)for b in rig.data.bones]
tree=ast.parse((ROOT/'Tools/Blender/author_nut_wizard_style.py').read_text(encoding='utf8'))
exec(compile(ast.Module(body=[n for n in tree.body if isinstance(n,ast.FunctionDef)and n.name in {'bind','reset','set_matrix','key','mix'}],type_ignores=[]),'wizard_helpers','exec'))
# The pose vocabulary is newly designed; the previous clips remain backups.
BASE=dict(root=(0,0,-.07),body=(0,0,4),head=(-4,0,-7),r=(-.56,-.18,.74),l=(.65,-.025,1.22),rr=(0,-18,70),lr=(10,-20,-10),fr=(-.235,.19,.156448588),fl=(.235,.13,.156448588),frr=(0,0,-8),flr=(0,0,8),grasp=.18,rgrasp=-1,lgrasp=-1,sr=(0,0,0),sl=(0,0,0))
def pose(**changes):return{**BASE,**changes}
# Fixed hip anchors keep the pelvis/feet grounded while the chest does broad turns.
reset();set_matrix('c_root_master.x',rest['c_root_master.x'].translation+Vector((0,0,-.07)),(0,0,0));bpy.context.view_layer.update()
hips={n:rig.pose.bones[n].matrix.copy()for n in ('c_thigh_b.l','c_thigh_b.r')}
def apply(p,planted=True):
 reset();set_matrix('c_root_master.x',rest['c_root_master.x'].translation+Vector(p['root']),p['body']);bpy.context.view_layer.update()
 if planted:
  for n,m in hips.items():rig.pose.bones[n].matrix=m
  bpy.context.view_layer.update()
 set_matrix('c_head.x',degrees=p['head'],current=True)
 for side,hand,shoulder in (('r',p['r'],p['sr']),('l',p['l'],p['sl'])):
  lift=max(0,min(1,(hand[2]-1.0)/.4));sign=1 if side=='l'else-1
  degrees=tuple(v+w for v,w in zip(shoulder,(0,-sign*60*lift,0)))
  set_matrix('c_shoulder.'+side,degrees=degrees,current=True)
 bpy.context.view_layer.update()
 for side,hand,angles,foot,footangles in (('r',p['r'],p['rr'],p['fr'],p['frr']),('l',p['l'],p['lr'],p['fl'],p['flr'])):
  set_matrix('c_hand_ik.'+side,hand,angles);set_matrix('c_foot_ik.'+side,foot,footangles)
  sign=1 if side=='l'else-1
  set_matrix('c_arms_pole.'+side,(sign*.82,.40,.72));set_matrix('c_leg_pole.'+side,(sign*.26,-.36,.32))
  specific=p['lgrasp'if side=='l'else'rgrasp']
  rig.pose.bones['c_hand_fk.'+side]['fingers_grasp']=p['grasp']if specific<0 else specific
 rig.update_tag();bpy.context.view_layer.update()
idle=pose()
# FIRE: a behind-shoulder coil, a held squeezed spark, then one violent forward snap.
fire_gather=pose(root=(-.035,.045,-.11),body=(-16,0,-20),head=(14,0,22),r=(-.58,.10,1.14),l=(.62,-.11,.94),rr=(25,-20,20),lr=(0,20,-85),grasp=.72)
fire_coil=pose(root=(-.045,.06,-.14),body=(-22,0,-28),head=(20,0,28),r=(-.58,.18,1.25),l=(.58,-.06,1.02),rr=(40,-25,10),lr=(15,0,-90),grasp=.9)
fire_release=pose(root=(0,-.18,-.08),body=(18,0,76),head=(-9,0,-61),r=(-.067,-.904,.713),l=(.42,.43,.60),rr=(-10,0,90),lr=(0,-30,25),grasp=0)
fire_over=pose(**{**fire_release,'root':(.025,-.215,-.1),'body':(27,0,82),'head':(-16,0,-66),'l':(.38,.49,.54),'rr':(-15,0,98)})
fire_rebound=pose(root=(-.02,.035,-.13),body=(-10,0,-11),head=(12,0,16),r=(-.58,-.11,.91),l=(.62,-.26,.96),rr=(15,-30,70),lr=(0,5,-85),grasp=.35)
# SUMMON: drag an imaginary portal from the ground, spread it with an asymmetric crest.
summon_floor=pose(root=(0,-.03,-.22),body=(28,0,0),head=(-18,0,0),r=(-.56,-.34,.42),l=(.57,-.34,.45),rr=(25,0,40),lr=(25,0,-40),grasp=.55)
summon_drag=pose(root=(0,.01,-.19),body=(17,0,-12),head=(-24,0,10),r=(-.54,-.29,.63),l=(.57,-.30,.71),rr=(0,0,75),lr=(10,0,-75),grasp=.75)
summon_crest=pose(root=(0,-.225,-.045),body=(-12,0,78),head=(6,0,-58),r=(-.067,-.904,.713),l=(.38,.16,1.37),rr=(0,0,90),lr=(25,-55,-15),grasp=.04)
summon_spread=pose(**{**summon_crest,'body':(-6,0,84),'head':(1,0,-65),'l':(.57,.10,1.29),'lr':(10,-30,8)})
# RAIN: a large sky-crown before contact; channel is a held conductor, not a greeting.
rain_raise=pose(root=(0,.025,-.11),body=(-14,0,-7),head=(-20,0,5),r=(-.48,-.02,1.33),l=(.48,-.02,1.35),rr=(-5,55,35),lr=(-5,-55,-35),grasp=.1)
rain_crown=pose(root=(0,.02,-.06),body=(-16,0,0),head=(-26,0,0),r=(-.28,.0,1.51),l=(.29,.0,1.53),rr=(-18,60,20),lr=(-18,-60,-20),grasp=0)
rain_hold_a=pose(root=(0,-.31,-.025),body=(-16,0,88),head=(-24,0,-68),r=(-.067,-.904,.713),l=(-.04,.34,1.44),rr=(0,0,90),lr=(-18,-60,-20),grasp=0,rgrasp=0,lgrasp=0)
rain_hold_b=pose(**{**rain_hold_a,'body':(-6,0,83),'head':(-16,0,-63),'l':(.62,.08,1.41),'lr':(-12,-58,-28),'lgrasp':.5})
# HEAVY: two-hand anvil preparation and a crushing down-forward diagonal.
heavy_coil=pose(root=(0,.04,-.20),body=(-18,0,-10),head=(20,0,14),r=(-.47,-.04,1.17),l=(.48,-.04,1.18),rr=(30,15,65),lr=(30,-15,-65),grasp=.85)
heavy_crown=pose(root=(0,.03,-.16),body=(-22,0,0),head=(25,0,0),r=(-.25,.02,1.42),l=(.26,.02,1.43),rr=(20,50,35),lr=(20,-50,-35),grasp=.65)
heavy_release=pose(root=(0,-.265,-.10),body=(23,0,58),head=(-16,0,-44),r=(-.067,-.904,.713),l=(.14,-.60,.93),rr=(0,0,90),lr=(18,-30,-92),grasp=0,sr=(0,10,0))
teleport_fold=pose(root=(-.025,.025,-.18),body=(20,0,-38),head=(-16,0,39),r=(-.37,-.12,.65),l=(.39,-.12,.68),rr=(0,35,95),lr=(0,-35,-95),grasp=.7)
teleport_vanish=pose(root=(0,.04,-.27),body=(31,0,-67),head=(-24,0,63),r=(-.31,-.06,.59),l=(.32,-.07,.62),rr=(15,35,110),lr=(15,-35,-110),grasp=1)
teleport_arrival=pose(root=(-.018,-.025,-.03),body=(-17,0,31),head=(9,0,-32),r=(-.61,.01,1.20),l=(.59,.01,1.24),rr=(-5,30,10),lr=(-5,-30,-10),grasp=0)
# MELEE: recoil with insulted face, disdainful backhand load, one contemptuous shove.
melee_flinch=pose(root=(-.035,.015,-.12),body=(-14,0,22),head=(16,0,-30),r=(-.48,-.02,.91),l=(.61,.05,.81),rr=(0,-25,45),lr=(0,0,-10),grasp=.55)
melee_load=pose(root=(-.02,.045,-.15),body=(-18,0,38),head=(18,0,-42),r=(-.36,.05,.98),l=(.59,.08,.83),rr=(10,-45,35),lr=(0,0,10),grasp=.25)
melee_contact=pose(root=(.03,-.06,-.07),body=(17,0,-19),head=(-11,0,13),r=(-.34,-.49,.78),l=(.64,.16,.73),rr=(-18,-5,100),lr=(0,0,20),grasp=0)
melee_dismiss=pose(root=(-.01,0,-.09),body=(1,0,13),head=(-8,0,-19),r=(-.63,-.09,.97),l=(.61,-.03,.86),rr=(-5,-25,20),lr=(0,0,-30),grasp=.12)
# Hit/death use wholly different theatrical beats, with a deliberate late surrender.
hit_startle=pose(root=(.04,.065,-.14),body=(-23,0,-23),head=(25,0,29),r=(-.66,.0,.99),l=(.66,.0,1.12),rr=(30,0,35),lr=(30,0,-35),grasp=.6)
death_no=pose(root=(-.03,.05,-.14),body=(-17,0,17),head=(26,0,-12),r=(-.60,.02,1.05),l=(.62,-.08,1.08),rr=(20,0,20),lr=(20,0,-20),grasp=.1)
death_defy=pose(root=(.035,.045,-.17),body=(-10,0,-24),head=(16,0,30),r=(-.64,.14,.72),l=(.54,-.04,1.30),rr=(0,0,5),lr=(-8,-45,-35),grasp=.4)
death_crumple=pose(root=(.02,.07,-.27),body=(-23,0,8),head=(32,0,-12),r=(-.55,-.13,.40),l=(.57,-.08,.43),rr=(10,0,35),lr=(10,0,-35),grasp=.1)
death_final=pose(root=(0,-.055,-.205),body=(68,0,-12),head=(21,0,10),r=(-.62,-.29,.18),l=(.62,-.22,.20),rr=(8,0,15),lr=(8,0,-15),fr=(-.24,.24,.156448588),fl=(.25,.20,.156448588),grasp=0)
LIB={
'Idle':([(1,idle),(21,pose(root=(.012,0,-.055),body=(-5,0,9),head=(-12,0,-20),r=(-.58,-.13,.77),l=(.45,-.20,1.24),rr=(0,-25,85),lr=(15,25,-75))),(40,pose(root=(-.012,0,-.075),body=(2,0,-9),head=(0,0,16),r=(-.50,-.25,1.10),l=(.62,-.10,.89),rr=(15,-20,50),lr=(0,0,-35),grasp=.45)),(57,pose(r=(-.49,-.20,1.20),l=(.64,.02,.78),rr=(-10,25,25),head=(-8,0,20))),(79,pose(body=(-4,0,7),head=(6,0,-11),r=(-.62,.04,.82),l=(.46,-.24,1.18),lr=(-15,20,-65),grasp=.1)),(104,idle),(121,idle)],True,True,'Prestigious conjuring palms, delayed suspicious glance, slow wrist flourish and a held poised stance.'),
'Cast':([(1,idle),(12,fire_gather),(29,fire_coil),(47,fire_coil),(51,pose(**{**fire_coil,'body':(-24,0,-33),'grasp':1})),(56,fire_release),(62,fire_over),(70,fire_rebound),(86,pose(head=(5,0,-12))),(101,idle)],False,True,'Behind-shoulder charged spark: squeeze/hold, explosive palm release, swept counterhand and elastic recoil.'),
'HeavyCast':([(1,idle),(16,teleport_fold),(34,teleport_vanish),(50,teleport_vanish),(56,pose(**{**teleport_vanish,'body':(35,0,-74),'head':(-28,0,69)})),(63,teleport_arrival),(69,pose(**{**teleport_arrival,'body':(-23,0,36),'head':(14,0,-38),'l':(.62,.05,1.29)})),(82,pose(body=(-5,0,-14),head=(-10,0,18),r=(-.57,-.04,1.02),l=(.56,-.12,1.18),rr=(0,25,25))),(101,idle)],False,True,'Teleport: fold into a corkscrew, disappear at55%, opposite-diagonal arrival starburst and swagger.'),
'Summon':([(1,idle),(13,summon_floor),(27,summon_floor),(39,summon_drag),(49,pose(**{**summon_drag,'head':(-28,0,12),'grasp':.95})),(56,summon_crest),(63,summon_spread),(74,pose(root=(0,-.04,-.06),body=(-9,0,11),head=(-8,0,-15),r=(-.60,-.24,.96),l=(.65,-.01,1.17),lr=(15,-20,-5))),(88,pose(head=(-9,0,10))),(101,idle)],False,True,'A portal dragged from the ground, chest rises after hands, asymmetric opening and an imperious seal.'),
'Rain':([(1,idle),(14,rain_raise),(31,rain_crown),(45,rain_crown),(50,pose(**{**rain_crown,'root':(0,.01,-.09),'grasp':.3})),(56,rain_hold_a),(59,pose(**{**rain_hold_a,'l':(.29,.19,1.51),'lr':(5,-80,-15)})),(63,rain_hold_b),(72,pose(body=(-4,0,19),head=(-18,0,-12),r=(-.53,-.14,1.06),l=(.52,-.04,1.30),lr=(16,-45,-12))),(88,pose(l=(.60,-.06,.95),head=(-7,0,0))),(101,idle)],False,True,'Broad sky crown and a maintained spell conductor. Distinct held55–62% small elbow/finger beats.'),
'Melee':([(1,idle),(12,melee_flinch),(31,melee_load),(49,melee_load),(56,melee_contact),(61,pose(**{**melee_contact,'body':(24,0,-25),'r':(-.31,-.50,.76)})),(72,melee_dismiss),(84,melee_dismiss),(94,idle),(101,idle)],False,True,'Offended recoil, disdainful backhand load, precise single shove, dismissive wrist and recovery.'),
'Hit':([(1,idle),(4,hit_startle),(9,pose(**{**hit_startle,'body':(-27,0,-27),'head':(31,0,31)})),(15,pose(root=(-.025,0,-.16),body=(13,0,17),head=(-12,0,-18),r=(-.57,-.18,.98),l=(.61,-.18,.95))),(26,pose(head=(-8,0,14))),(41,idle)],False,True,'Startled shock, separated splayed palms, over-correction and offended composure.'),
'Death':([(1,idle),(9,death_no),(23,death_no),(35,death_defy),(46,death_defy),(54,death_crumple),(68,death_final),(75,pose(**{**death_final,'root':(0,-.05,-.185),'body':(63,0,-12)})),(86,death_final),(101,death_final)],False,False,'Disbelief, one last defiant spell, buckling knees, delayed forward collapse, bounce and final stillness.')}
# A magician's backwards promenade: careful lifting toes and a rotating poised hand.
walk=[]
for f,fr,fl in [(1,(-.235,.31,.156448588),(.235,.055,.156448588)),(7,(-.235,.27,.156448588),(.235,.08,.24)),(13,(-.235,.19,.156448588),(.235,.18,.29)),(19,(-.235,.10,.156448588),(.235,.29,.19)),(25,(-.235,.055,.156448588),(.235,.31,.156448588)),(31,(-.235,.08,.24),(.235,.27,.156448588)),(37,(-.235,.18,.29),(.235,.19,.156448588)),(43,(-.235,.29,.19),(.235,.10,.156448588)),(49,(-.235,.31,.156448588),(.235,.055,.156448588))]:
 sign=1 if f<25 else-1
 walk.append((f,pose(root=(.024*sign,0,-.075 if f in(1,25,49)else-.06),body=(-3,0,7*sign),head=(-6,0,-12*sign),fr=fr,fl=fl,r=(-.57,-.17,.91+.06*sign),l=(.53,-.18,1.15-.05*sign),rr=(10,-20,60+15*sign),lr=(5,20,-65),frr=(-12 if fr[2]>.20 else 0,0,-8),flr=(-12 if fl[2]>.20 else 0,0,8))))
walk[-1]=(49,walk[0][1]);LIB['Walk']=(walk,True,False,'Careful backwards toe steps, floating conjuring hands, contrasting confident gaze.')
# Optional performances are authored separately; they never become new gameplay attacks.
LIB['Taunt']=([(1,idle),(14,pose(body=(-8,0,-18),head=(-16,0,22),l=(.57,-.05,1.24),lr=(0,10,-30))),(27,pose(body=(7,0,23),head=(-6,0,-28),r=(-.64,-.03,1.07),rr=(0,-35,10),l=(.62,.08,.82))),(42,pose(body=(-7,0,-17),head=(-17,0,25),l=(.54,-.04,1.25),lr=(15,30,-10))),(61,pose(head=(-18,0,-20),r=(-.60,-.02,.93),l=(.62,.06,1.00),grasp=.35)),(91,idle)],False,True,'Taunt: inspect a glove, beckon dismissively, sneer and regain poise.')
LIB['Victory']=([(1,idle),(20,pose(root=(0,.015,-.12),body=(15,0,0),head=(-10,0,0),r=(-.52,-.28,.81),l=(.55,-.28,.85),grasp=.65)),(37,rain_crown),(51,pose(**{**rain_crown,'r':(-.55,.04,1.42),'l':(.55,.04,1.43),'body':(-18,0,0),'head':(-28,0,0)})),(69,pose(body=(20,0,0),head=(-15,0,0),r=(-.62,-.19,.55),l=(.62,-.19,.58),grasp=0)),(82,pose(body=(15,0,0),head=(-18,0,0))),(101,idle)],False,True,'Victory: collect energy, grand crown, broad triumph and a courtly bow.')
LIB['SpawnFlourish']=([(1,pose(root=(0,.04,-.2),body=(24,0,0),head=(-20,0,0),r=(-.40,-.17,.57),l=(.40,-.17,.60),grasp=.85)),(17,pose(root=(0,.03,-.18),body=(20,0,-25),head=(-14,0,25),r=(-.44,-.16,.61),l=(.45,-.14,.65),grasp=.9)),(31,pose(root=(0,-.04,-.04),body=(-15,0,22),head=(12,0,-24),r=(-.58,.0,1.28),l=(.58,.0,1.30),grasp=0)),(48,pose(body=(-8,0,-15),head=(0,0,20),r=(-.63,.08,.85),l=(.52,-.10,1.23))),(71,pose(head=(-10,0,-15))),(101,idle)],False,True,'Spawn: tightly folded silhouette bursts outward, wrists spiral, self-assured entrance.')
LIB['IdleOrbit']=([(1,idle),(27,pose(body=(-7,0,-15),head=(-15,0,17),r=(-.54,-.22,1.14),l=(.62,-.08,.82),rr=(25,20,30),lr=(0,0,-20))),(58,pose(body=(4,0,17),head=(-4,0,-25),r=(-.63,.08,.74),l=(.46,-.22,1.25),rr=(0,-30,65),lr=(20,25,-40))),(87,pose(body=(-4,0,-9),head=(8,0,12),r=(-.49,-.25,1.18),l=(.62,-.13,.89),rr=(-12,30,20),lr=(0,-20,-20))),(108,idle),(121,idle)],True,True,'Idle orbit: two palms trade an imaginary floating sigil, gaze follows a slow curved path.')
LIB['IdleMeditate']=([(1,pose(root=(0,.0,-.11),body=(-6,0,0),head=(-17,0,0),r=(-.45,-.27,.86),l=(.45,-.27,.86),rr=(0,25,60),lr=(0,-25,-60),grasp=.2)),(39,pose(root=(0,.0,-.085),body=(-9,0,0),head=(-21,0,0),r=(-.45,-.25,.95),l=(.45,-.25,.95),rr=(12,35,55),lr=(12,-35,-55),grasp=.06)),(77,pose(root=(0,0,-.12),body=(-3,0,0),head=(-11,0,0),r=(-.47,-.26,.83),l=(.47,-.26,.83),rr=(-8,20,65),lr=(-8,-20,-65),grasp=.3)),(121,pose(root=(0,.0,-.11),body=(-6,0,0),head=(-17,0,0),r=(-.45,-.27,.86),l=(.45,-.27,.86),rr=(0,25,60),lr=(0,-25,-60),grasp=.2))],True,True,'Meditation: symmetrical suspended palms, held closed-eyed dignity, measured breathing.')
LIB['SpellFizzle']=([(1,idle),(15,fire_gather),(29,fire_coil),(38,pose(body=(8,0,-8),head=(-12,0,7),r=(-.51,-.19,.92),l=(.59,-.16,1.03),grasp=0)),(45,hit_startle),(57,pose(root=(0,0,-.1),body=(-3,0,12),head=(12,0,-25),r=(-.53,-.27,.89),l=(.62,.04,.83),rr=(0,-20,5),grasp=.25)),(68,melee_dismiss),(81,idle)],False,True,'Fizzle: an ambitious gather collapses, startled fingers, inspect the failed palm, dismiss it.')
meshes=[o for o in scene.objects if o.type=='MESH'and any(m.type=='ARMATURE'and m.object==rig for m in o.modifiers)]
def mesh_floor():
 deps=bpy.context.evaluated_depsgraph_get();minimum=float('inf')
 for obj in meshes:
  ev=obj.evaluated_get(deps);mesh=ev.to_mesh()
  try:minimum=min(minimum,min((ev.matrix_world@v.co).z for v in mesh.vertices))
  finally:ev.to_mesh_clear()
 return minimum
# Probe images provide actual geometry and contact evidence before a full animation bake.
if args.probe:
 folder=OUT/'PosesV3Probe';folder.mkdir(exist_ok=True);scene.render.resolution_x=480;scene.render.resolution_y=480;scene.render.resolution_percentage=100
 probe=[('Idle',1,idle),('Cast_Coil',49,fire_coil),('Cast_Release',56,fire_release),('Summon_Ground',27,summon_floor),('Summon_Crest',56,summon_crest),('Rain_Channel',56,rain_hold_a),('Rain_ChannelB',63,rain_hold_b),('Rain_Crown',45,rain_crown),('Teleport_Fold',50,teleport_vanish),('Teleport_Arrival',63,teleport_arrival)]
 rows=[]
 for label,f,p in probe:
  apply(p,True);rows.append({'pose':label,'mesh_min_z_m':mesh_floor(),'right_wrist_m':list(rig.pose.bones['hand.r'].head),'right_wrist_target_error_m':(rig.pose.bones['hand.r'].head-Vector((-.067,-.904,.713))).length if f==56 else None})
  scene.render.filepath=str(folder/(label+'.png'));bpy.ops.render.render(write_still=True)
 (OUT/'WizardV3ProbeReport.json').write_text(json.dumps(rows,indent=2),encoding='utf8');print('WIZARD_V3_PROBE_READY',flush=True);sys.exit(0)
def curves(a):return[f for l in a.layers for s in l.strips for bag in s.channelbags for f in bag.fcurves]
def action_hash(a):return hashlib.sha256(json.dumps([(f.data_path,f.array_index,[(list(k.co),list(k.handle_left),list(k.handle_right),k.interpolation)for k in f.keyframe_points])for f in curves(a)],sort_keys=True).encode()).hexdigest()
old_hashes={a.name:action_hash(a)for a in bpy.data.actions};backup_names={}
for label in ['Idle','Walk','Cast','HeavyCast','Summon','Rain','Melee','Hit','Death']:
 old=bpy.data.actions['MC_Wizard_'+label];backup_names[old.name]='MC_V2Game_Wizard_'+label;old.name=backup_names[old.name]
records=[]
for label,(knots,loop,planted,note)in LIB.items():
 action=bpy.data.actions.new('MC_Wizard_'+label);action.use_fake_user=True;cursor=0
 for f in range(knots[0][0],knots[-1][0]+1):
  while cursor+1<len(knots)-1 and f>knots[cursor+1][0]:cursor+=1
  fa,a=knots[cursor];fb,b=knots[cursor+1];apply(mix(a,b,(f-fa)/(fb-fa),linear_keys=('fr','fl')if label=='Walk'else()),planted)
  rig.animation_data.action=action;key(f)
 bind(action)
 for curve in curves(action):
  for point in curve.keyframe_points:point.interpolation='LINEAR'
 action.asset_mark();action.asset_data.description=note;action['game_slot']=label;action['loop']=loop;action['optional']=label not in ['Idle','Walk','Cast','HeavyCast','Summon','Rain','Melee','Hit','Death'];action['key_pose_frames']=json.dumps([f for f,_ in knots]);action['release_fraction']=.55 if label in ['Cast','HeavyCast','Summon','Rain','Melee']else 0
 records.append({'label':label,'action':action.name,'start':knots[0][0],'end':knots[-1][0],'fps':30,'loop':loop,'planted':planted,'optional':action['optional'],'key_poses':[f for f,_ in knots],'description':note})
 print('WIZARD_V3_BAKED',label,flush=True)
# One focused pass over the nine production clips; optional actions receive key-pose QA.
limbs=['thigh_stretch.l','thigh_stretch.r','leg_stretch.l','leg_stretch.r','arm_stretch.l','arm_stretch.r','forearm_stretch.l','forearm_stretch.r'];qa={}
for rec in records:
 action=bpy.data.actions[rec['action']];bind(action);frames=rec['key_poses']if rec['optional']else range(rec['start'],rec['end']+1);minz=float('inf');ratios=[1,1];footpoints={n:[]for n in ('foot.l','foot.r')};anchor=[]
 for f in frames:
  scene.frame_set(f);bpy.context.view_layer.update()
  for b in rig.pose.bones:assert all(math.isfinite(v)for row in b.matrix for v in row),(rec['label'],f,b.name)
  for n in limbs:
   b=rig.pose.bones[n];r=(b.tail-b.head).length/rig.data.bones[n].length;ratios[0]=min(ratios[0],r);ratios[1]=max(ratios[1],r);assert .95<r<1.05,(rec['label'],f,n,r)
  minz=min(minz,mesh_floor())
  for n in footpoints:footpoints[n].append(rig.pose.bones[n].matrix.translation.copy())
  if rec['label']in ('Cast','Summon','Rain')and f in (56,63):anchor.append({'frame':f,'wrist_target_error_actor_cm':(rig.pose.bones['hand.r'].head-Vector((-.067,-.904,.713))).length*135.547752})
 drift={n:max((p-pts[0]).length for p in pts)for n,pts in footpoints.items()}
 assert minz>=-.003,(rec['label'],'floor',minz)
 if rec['planted']:assert max(drift.values())<.003,(rec['label'],'footcontacts',drift)
 if anchor:assert max(v['wrist_target_error_actor_cm']for v in anchor)<10,(rec['label'],'anchor',anchor)
 scene.frame_set(rec['start']);bpy.context.view_layer.update();first={b.name:b.matrix.copy()for b in rig.pose.bones if b.bone.use_deform};scene.frame_set(rec['end']);bpy.context.view_layer.update();seam=max(abs(v-rig.pose.bones[n].matrix[i][j])for n,m in first.items()for i,row in enumerate(m)for j,v in enumerate(row))
 if rec['loop']:assert seam<1e-4,(rec['label'],'seam',seam)
 qa[rec['label']]={'frames_checked':len(frames),'mesh_min_z_m':minz,'limb_length_ratio_range':ratios,'foot_drift_m':drift,'release_anchor':anchor,'loop_seam_max_matrix_difference':seam if rec['loop']else None,'finite':True}
 print('WIZARD_V3_QA',rec['label'],minz,flush=True)
for old,h in old_hashes.items():assert action_hash(bpy.data.actions[backup_names.get(old,old)])==h,old
assert restsig==[(b.name,b.parent.name if b.parent else None,[list(r)for r in b.matrix_local],b.use_deform)for b in rig.data.bones]
assert all(digest(Path(p))==h for p,h in input_hashes.items())
scene.render.fps=30;scene.frame_start=1;scene.frame_end=121;bind(bpy.data.actions['MC_Wizard_Idle']);scene.frame_set(1)
scene['MC_WizardStyleRevision']='v3 complete redesign: magical staging, contrast and held beats; six optional performances.'
bpy.ops.wm.save_as_mainfile(filepath=str(OUTPUT))
report={'passed':True,'blend':str(OUTPUT),'blend_sha256':digest(OUTPUT),'source_files_unchanged':True,'input_hashes':input_hashes,'previous_actions_preserved':True,'rig_rest_preserved':True,'meshes_and_weights_untouched':True,'production_count':9,'optional_count':6,'records':records,'qa':qa,'game_imported':False,'authoring_reference_emitter_actor_cm':[129.014337,11.936049,5.57991],'scope':'Production all integer frames; optional key poses. Source DCC finite/limb/floor/contact/anchor checks use the authoring reference scale 1.35548. The actual saved runtime profile and imported palm alignment are measured in Saved/Checks/WizardStyleV3Integration.json.'}
(OUT/'WizardStyleV3Report.json').write_text(json.dumps(report,indent=2),encoding='utf8');print('WIZARD_STYLE_V3_COMPLETE',str(OUTPUT),flush=True)

