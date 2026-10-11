"""Eight Blender-only asymmetric boss performances on the reviewed sole rig.

The artist mesh, frozen primary ARP and existing actions are preserved. New
editable BoneBrush actions use deliberate pose beats, planted soles, a heavy
palm and an outward elbow solve. This is authored motion, not a new Mixamo
download. The caller renders the saved action collection before UE approval.
"""
import json, math, sys
from pathlib import Path
import bpy
from mathutils import Matrix, Quaternion, Vector

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).parent))
import carries_animation_helpers as helpers

SOURCE = ROOT/'ArtSource/CarriesStage2/HandSupportWalk/Carries_Stage2_HandSupportWalk_v4.blend'
SOURCE_REPORT = SOURCE.parent/'AuthorReport_v4.json'
DEST = ROOT/'ArtSource/CarriesStage2/CombatPool_v1'
BLEND = DEST/'Carries_Stage2_CombatPool_v1.blend'
REPORT = DEST/'Report_v1.json'
KEEP = ('SM_Character', 'SM_Character.001', 'SM_Character_006_002_003.004', 'SM_Character_006_002_003.005')
AXES = [Vector(v) for v in ((1,0,0),(0,1,0),(0,0,1))]


def value(frame, points):
    return helpers.author.remap(frame, points)


def vector_curve(frame, points):
    return Vector([value(frame, [(f, v[i]) for f, v in points]) for i in range(3)])


def rotate(degrees):
    q = Quaternion()
    for axis, angle in zip(AXES, degrees):
        q = Quaternion(axis, math.radians(angle)) @ q
    return q


def pivot_rotation(point, q):
    return Matrix.Translation(point) @ q.to_matrix().to_4x4() @ Matrix.Translation(-point)


CASES = [
    dict(name='AwakenRoar', labelRU='Пробуждение и рёв', useRU='Кисть отрывается последней; корпус распрямляется, затем подаётся вперёд в рёве.', frames=96,
         event_frames=dict(crouch=18, rise=38, roar=48, hand_drop=67, settle=90)),
    dict(name='ForwardHammer', labelRU='Тяжёлый удар вперёд', useRU='Босс разворачивает корпус назад, поднимает сжатую кисть и резко выбрасывает её вперёд.', frames=108,
         event_frames=dict(load=33, apex=40, strike=48, heavy_hold=55, recover=87)),
    dict(name='WideBackhand', labelRU='Широкий обратный удар', useRU='Рука уходит через переднюю дугу влево и возвращается широким обратным взмахом.', frames=102,
         event_frames=dict(load=31, hold=37, strike=45, follow=55, recover=83)),
    dict(name='PalmShove', labelRU='Толчок с коротким шагом', useRU='Короткий шаг задаёт вес; открытая ладонь давит вперёд и остаётся в давлении после толчка.', frames=102,
         event_frames=dict(crouch=24, first_step=35, shove=47, press=60, recover=90)),
    dict(name='InfectionPulse', labelRU='Каст инфекции в землю', useRU='Рука поднимает воображаемый сгусток; корпус сжимается и выпускает один импульс в пол.', frames=120,
         event_frames=dict(gather=31, charge=47, release=63, ground_contact=67, recoil=77, recover=108)),
    dict(name='HeavyStagger', labelRU='Сильное попадание', useRU='Корпус принимает удар; тяжёлая рука запаздывает, затем ловит тело опорой о пол.', frames=90,
         event_frames=dict(impact=14, hand_lag=21, support=35, overshoot=52, settle=78)),
    dict(name='ExhaustedRecovery', labelRU='Изнеможение и подъём', useRU='Босс долго висит на неподвижной руке; медленно возвращает вес на ноги и выпрямляется.', frames=120,
         event_frames=dict(collapse=25, fatigue=48, push=75, rise=93, settle=115)),
    dict(name='SupportedTurn', labelRU='Разворот с опорой', useRU='Кисть остаётся на месте, пока корпус поворачивается; ноги переступают, затем тяжёлая рука переносится.', frames=120,
         event_frames=dict(first_step=37, pivot=56, palm_swing=72, second_step=83, new_support=97, settle=115)),
]


def performance(case, frame, palm):
    """Artist pose beats, with the hand and trunk deliberately out of phase."""
    f, kind = frame, case['name']
    target = palm.copy(); body = Vector(); angles = Vector(); hand_angles = Vector()
    grip, stretch, yaw, travel = 0., Vector((1,1,1)), 0., Vector()
    ft = [Vector(), Vector()]; foot_roll = [0., 0.]
    if kind == 'AwakenRoar':
        body = vector_curve(f, [(1,(0,0,0)),(18,(-.025,.015,-.025)),(38,(.018,.02,.018)),(48,(.012,-.035,.002)),(63,(0,-.025,-.02)),(83,(0,.01,-.006)),(96,(0,0,0))])
        angles = vector_curve(f, [(1,(0,0,0)),(18,(11,-4,6)),(38,(-13,5,-7)),(48,(16,-2,3)),(63,(8,1,0)),(83,(-2,0,0)),(96,(0,0,0))])
        # Keep the raised hand outside the mouth silhouette during the roar.
        target += vector_curve(f, [(1,(0,0,0)),(24,(0,0,0)),(40,(.50,.45,.81)),(48,(.48,.28,.80)),(62,(.44,.24,.56)),(78,(.06,-.07,.13)),(88,(0,0,0)),(96,(0,0,0))])
        hand_angles = vector_curve(f, [(1,(0,0,0)),(40,(22,5,-18)),(50,(36,-8,-27)),(68,(10,2,-12)),(96,(0,0,0))])
        grip = value(f, [(1,0),(24,.08),(42,.85),(55,.75),(79,.08),(96,0)])
        stretch = vector_curve(f, [(1,(1,1,1)),(18,(1.025,1.025,.97)),(39,(.985,.985,1.025)),(50,(1.01,1.02,.985)),(96,(1,1,1))])
    elif kind == 'ForwardHammer':
        body = vector_curve(f, [(1,(0,0,0)),(31,(-.045,.035,-.025)),(40,(-.035,.025,-.008)),(48,(.03,-.04,-.036)),(57,(.023,-.03,-.035)),(83,(-.012,.012,-.016)),(108,(0,0,0))])
        angles = vector_curve(f, [(1,(0,0,0)),(33,(-10,-7,-18)),(40,(-12,-5,-16)),(48,(18,6,21)),(57,(15,5,16)),(85,(-4,-1,-3)),(108,(0,0,0))])
        target = vector_curve(f, [(1,tuple(palm)),(15,(.80,-.72,.34)),(33,(1.17,.16,1.02)),(40,(1.22,.20,1.12)),(48,(.60,-1.25,.78)),(55,(.62,-1.24,.73)),(68,(.79,-1.00,.61)),(87,(.93,-.67,.39)),(103,tuple(palm)),(108,tuple(palm))])
        hand_angles = vector_curve(f, [(1,(0,0,0)),(33,(46,-20,-22)),(40,(52,-16,-25)),(48,(58,12,14)),(62,(44,6,16)),(90,(7,0,2)),(108,(0,0,0))])
        grip = value(f, [(1,0),(21,.25),(33,.95),(60,1),(85,.28),(108,0)])
        ft[1]=vector_curve(f,[(1,(0,0,0)),(36,(0,0,0)),(42,(.06,-.05,.04)),(47,(.12,-.10,0)),(62,(.12,-.10,0)),(79,(.06,-.05,.035)),(98,(0,0,0)),(108,(0,0,0))])
    elif kind == 'WideBackhand':
        # The arc stays in front of the face. A return stroke, not the old sweep.
        phi = math.radians(value(f, [(1,-72),(14,-72),(31,-146),(37,-146),(47,-36),(55,-17),(79,-46),(97,-72),(102,-72)]))
        radius = value(f, [(1,.82),(31,1.02),(37,1.02),(47,1.10),(55,1.12),(79,.89),(102,.82)])
        active = value(f, [(1,0),(12,0),(28,1),(79,1),(102,0)])
        arc = Vector((.43+radius*math.cos(phi), -.17+radius*math.sin(phi),value(f,[(1,palm.z),(31,.70),(37,.71),(47,.79),(55,.87),(80,.44),(102,palm.z)])))
        target = palm.lerp(arc, active)
        angles = vector_curve(f, [(1,(0,0,0)),(31,(-4,3,23)),(37,(-3,3,24)),(47,(9,-5,-20)),(55,(4,-6,-26)),(81,(-2,2,5)),(102,(0,0,0))])
        body = vector_curve(f, [(1,(0,0,0)),(31,(-.035,.015,-.03)),(47,(.035,-.015,-.025)),(55,(.040,0,-.03)),(82,(-.01,0,-.009)),(102,(0,0,0))])
        hand_angles = vector_curve(f, [(1,(0,0,0)),(31,(25,8,28)),(37,(22,8,31)),(47,(35,-9,-32)),(57,(31,-12,-44)),(85,(5,0,8)),(102,(0,0,0))])
        grip = value(f, [(1,0),(30,.35),(46,.55),(57,.46),(90,.1),(102,0)])
    elif kind == 'PalmShove':
        target = vector_curve(f, [(1,tuple(palm)),(24,(.87,-.31,.72)),(35,(.86,-.30,.81)),(47,(.60,-1.25,.63)),(60,(.58,-1.24,.58)),(77,(.81,-.73,.43)),(97,tuple(palm)),(102,tuple(palm))])
        body = vector_curve(f, [(1,(0,0,0)),(24,(-.02,.025,-.052)),(36,(0,0,-.04)),(47,(.015,-.065,-.035)),(62,(.01,-.05,-.043)),(82,(0,.008,-.008)),(102,(0,0,0))])
        angles = vector_curve(f, [(1,(0,0,0)),(24,(-9,-4,-6)),(36,(-5,0,-3)),(47,(18,3,8)),(62,(16,2,7)),(84,(-3,0,-1)),(102,(0,0,0))])
        hand_angles = vector_curve(f, [(1,(0,0,0)),(26,(-61,0,-9)),(47,(-68,3,4)),(62,(-66,0,6)),(86,(-12,0,0)),(102,(0,0,0))])
        travel.y = value(f, [(1,0),(25,0),(47,-.13),(65,-.13),(94,0),(102,0)])
        ft[0].y = value(f, [(1,0),(26,0),(41,-.15),(65,-.15),(90,0),(102,0)])
        ft[0].z = value(f, [(1,0),(26,0),(33,.055),(41,0),(73,0),(81,.04),(90,0),(102,0)])
        ft[1].x = value(f, [(1,0),(30,0),(43,.06),(70,.06),(96,0),(102,0)])
        ft[1].y = value(f, [(1,0),(30,0),(43,-.24),(70,-.24),(96,0),(102,0)])
        ft[1].z = value(f, [(1,0),(30,0),(36,.055),(43,0),(79,0),(86,.035),(96,0),(102,0)])
    elif kind == 'InfectionPulse':
        target = vector_curve(f, [(1,tuple(palm)),(17,(.84,-.78,.50)),(31,(1.04,-.59,1.04)),(47,(1.09,-.52,1.08)),(57,(1.01,-.52,1.17)),(67,(.78,-.87,palm.z)),(72,(.78,-.87,palm.z)),(77,(.81,-.81,.35)),(96,(.97,-.65,.62)),(115,tuple(palm)),(120,tuple(palm))])
        body = vector_curve(f, [(1,(0,0,0)),(31,(-.025,.02,-.015)),(48,(-.035,.03,-.03)),(57,(0,.03,-.01)),(67,(.018,-.04,-.025)),(72,(.016,-.036,-.026)),(79,(0,.01,-.027)),(96,(-.01,.015,.004)),(120,(0,0,0))])
        angles = vector_curve(f, [(1,(0,0,0)),(31,(-8,-3,-7)),(48,(-11,-5,-9)),(57,(-15,-3,-12)),(67,(13,4,9)),(72,(12,4,8)),(79,(7,0,4)),(98,(-3,-1,0)),(120,(0,0,0))])
        hand_angles = vector_curve(f, [(1,(0,0,0)),(31,(-32,8,-8)),(47,(-39,12,-12)),(57,(-46,9,-12)),(67,(0,0,0)),(73,(0,0,0)),(96,(-10,0,-5)),(120,(0,0,0))])
        grip = value(f, [(1,0),(17,.22),(31,.48),(49,.63),(60,.32),(67,0),(75,0),(96,.10),(120,0)])
        ft[1].y=value(f,[(1,0),(51,0),(62,-.115),(81,-.115),(111,0),(120,0)])
        ft[1].z=value(f,[(1,0),(51,0),(56,.035),(62,0),(88,0),(97,.025),(111,0),(120,0)])
    elif kind == 'HeavyStagger':
        body = vector_curve(f, [(1,(0,0,0)),(10,(0,0,0)),(14,(-.06,.085,-.055)),(23,(-.072,.062,-.080)),(35,(-.028,.006,-.043)),(52,(.013,-.02,-.018)),(76,(0,0,-.004)),(90,(0,0,0))])
        angles = vector_curve(f, [(1,(0,0,0)),(10,(0,0,0)),(14,(-21,-12,13)),(23,(-15,-16,9)),(35,(12,-9,4)),(52,(-4,4,-3)),(76,(1,0,0)),(90,(0,0,0))])
        target += vector_curve(f, [(1,(0,0,0)),(14,(0,0,0)),(21,(.12,.12,.31)),(27,(.08,.07,.26)),(35,(0,-.025,0)),(42,(0,-.025,0)),(58,(.025,.018,.05)),(78,(0,0,0)),(90,(0,0,0))])
        hand_angles = vector_curve(f, [(1,(0,0,0)),(14,(0,0,0)),(24,(15,-5,17)),(35,(0,0,0)),(42,(0,0,0)),(61,(-3,0,-3)),(90,(0,0,0))])
        grip = value(f, [(1,0),(14,0),(23,.58),(33,.1),(35,0),(90,0)])
        stretch = vector_curve(f, [(1,(1,1,1)),(10,(1,1,1)),(15,(1.025,1.01,.97)),(30,(1.01,1.025,.975)),(52,(.995,.995,1.005)),(90,(1,1,1))])
    elif kind == 'ExhaustedRecovery':
        body = vector_curve(f, [(1,(0,0,0)),(25,(-.016,-.016,-.04)),(48,(-.019,-.022,-.043)),(66,(-.02,-.02,-.043)),(75,(-.014,-.012,-.035)),(93,(.002,.008,.010)),(104,(0,.006,.006)),(120,(0,0,0))])
        angles = vector_curve(f, [(1,(0,0,0)),(25,(12,-5,5)),(48,(14,-6,6)),(66,(13,-6,6)),(75,(9,-4,3)),(93,(-4,2,-2)),(120,(0,0,0))])
        if 28<f<70:
            breath = math.sin((f-28)*math.pi/21)
            body.z += .005*breath; angles.x += 1.5*breath
        # Hand is a load-bearing anchor throughout; no decorative wrist wiggle.
    elif kind == 'SupportedTurn':
        # An in-place weight pivot, ready to combine with game turning later.
        # The asymmetric global rest transform is deliberately left intact.
        yaw = 0.
        body = vector_curve(f, [(1,(0,0,0)),(28,(.006,0,-.033)),(54,(.018,-.014,-.045)),(75,(.004,0,-.04)),(97,(0,0,-.016)),(120,(0,0,0))])
        angles = vector_curve(f, [(1,(0,0,0)),(28,(5,3,-10)),(53,(8,5,-24)),(73,(6,1,-22)),(98,(1,0,-7)),(120,(0,0,0))])
        hand_angles = vector_curve(f, [(1,(0,0,0)),(59,(0,0,0)),(74,(-17,0,-8)),(97,(0,0,0)),(120,(0,0,0))])
        target += vector_curve(f,[(1,(0,0,0)),(58,(0,0,0)),(75,(.10,.09,.26)),(97,(.09,.11,0)),(106,(.09,.11,0)),(113,(.04,.05,.11)),(120,(0,0,0))])
        ft[0]=vector_curve(f,[(1,(0,0,0)),(22,(0,0,0)),(34,(-.025,-.035,.04)),(45,(-.05,-.07,0)),(83,(-.05,-.07,0)),(98,(-.025,-.035,.035)),(117,(0,0,0)),(120,(0,0,0))])
        ft[1]=vector_curve(f,[(1,(0,0,0)),(43,(0,0,0)),(58,(.03,-.04,.035)),(76,(.06,-.08,0)),(86,(.06,-.08,0)),(102,(.03,-.04,.03)),(119,(0,0,0)),(120,(0,0,0))])
    return dict(target=target, body=body, angles=angles, hand_angles=hand_angles, grip=grip,
                stretch=stretch, yaw=yaw, travel=travel, foot_delta=ft, foot_roll=foot_roll)


def main():
    assert bpy.app.background and not BLEND.exists(), 'Create a new derivative; preserve previous revisions'
    parent = json.loads(SOURCE_REPORT.read_text(encoding='utf8'))
    source_paths = [SOURCE]
    hashes = {p: helpers.author.sha(p) for p in source_paths}
    bpy.ops.wm.open_mainfile(filepath=str(SOURCE))
    scene, rig, mesh = bpy.context.scene, bpy.data.objects['BoneBrushRig'], bpy.data.objects['SM_Character']
    scene.frame_set(1); bpy.context.view_layer.update()
    defaults = helpers.defaults(rig)
    helpers.author.bind(rig, None, defaults)
    originals = {a.name: helpers.signature(a) for a in bpy.data.actions}
    for a in bpy.data.actions: a.use_fake_user = True
    controls = [p.name for p in rig.pose.bones if p.name.startswith('CTRL-')]
    skins = ['MC_Foot_PosX','MC_Foot_PosX_UpperSkin','MC_Foot_NegX','MC_Foot_NegX_UpperSkin']
    names = controls + skins
    bases = {n: defaults[n]['basis'].decompose() for n in names}
    base_world = {n: rig.matrix_world @ rig.pose.bones[n].matrix for n in names}
    heads = {n: base_world[n].translation.copy() for n in names}
    wrist_name, body_name, global_name = 'CTRL-ik_segment','CTRL-body','CTRL-global'
    palm, floor = heads[wrist_name].copy(), parent['skin_contact_floor']
    wrist_rotation = base_world[wrist_name].to_quaternion()
    inverse_rig = rig.matrix_world.inverted()
    finger_names = ['CTRL-ik_segment.L','CTRL-ik_segment.L_01','CTRL-ik_segment.L_02']
    finger_directions = {}
    for n in finger_names:
        bind_frame = (base_world[n] @ defaults[n]['basis'].inverted()).to_3x3()
        finger_directions[n] = bind_frame.inverted() @ (palm-heads[n]).normalized()
    foot_specs = [dict(ctrl='CTRL-ik_segment.R', calf='segment_006_02', thigh='segment_006_01', skins=skins[:2]),
                  dict(ctrl='CTRL-ik_segment.R_01', calf='segment_007_02', thigh='segment_007_01', skins=skins[2:])]
    for spec in foot_specs:
        spec['length'] = sum(rig.pose.bones[n].length for n in (spec['calf'],spec['thigh']))
        print('COMBAT_FOOT_BASE',spec['ctrl'],spec['length'],list(heads[spec['ctrl']]),list(rig.matrix_world @ rig.pose.bones[spec['calf']].tail),list(rig.matrix_world @ rig.pose.bones[spec['thigh']].head),flush=True)
    arm_ids = [v.index for v in mesh.data.vertices if sum(g.weight for g in v.groups if mesh.vertex_groups[g.group].name.startswith(('segment_002','segment_008','segment_009','segment_010','segment_011'))) > .75]
    arm_max, leg_max, contacts, corrections = 0., 0., [], {}
    bounds, skin_lows = [], []

    def set_world(name, desired):
        rig.pose.bones[name].matrix = inverse_rig @ desired
        bpy.context.view_layer.update()

    def posed(case, frame, measure=False):
        nonlocal arm_max, leg_max
        helpers.author.bind(rig, None, defaults)
        p = performance(case, frame, palm)
        root_q = rotate((0,0,p['yaw']))
        root = Matrix.Translation(p['travel']) @ pivot_rotation(heads[global_name], root_q)
        set_world(global_name, root @ base_world[global_name])
        body_matrix = root @ Matrix.Translation(p['body']) @ pivot_rotation(heads[body_name], rotate(p['angles'])) @ base_world[body_name]
        set_world(body_name, body_matrix)
        rig.pose.bones[body_name].scale *= p['stretch']; bpy.context.view_layer.update()
        foot_worlds = []
        for index, spec in enumerate(foot_specs):
            desired=base_world[spec['ctrl']].copy(); desired.translation += p['foot_delta'][index]
            set_world(spec['ctrl'],desired); foot_worlds.append(desired)
        # Keep very short legs bent. Correct only vertical pelvis height if a
        # strong trunk accent would extend the source two-bone IK fully.
        for _ in range(3):
            excess=0.
            for spec, fw in zip(foot_specs, foot_worlds):
                hip=rig.matrix_world @ rig.pose.bones[spec['thigh']].head
                delta=hip-fw.translation
                allowed=(spec['length']*.979)**2-delta.x**2-delta.y**2
                if allowed>0: excess=max(excess, delta.z-math.sqrt(allowed))
            if excess<.0002: break
            adjusted=rig.matrix_world @ rig.pose.bones[body_name].matrix
            adjusted.translation.z -= excess
            set_world(body_name,adjusted)
        target=p['target'].copy()
        hand_q=rotate(p['hand_angles']) @ wrist_rotation
        if case['name']=='PalmShove':
            target += p['travel']
        shoulder=rig.matrix_world @ rig.pose.bones['segment_002_01'].head
        distance=(target-shoulder).length
        if distance>1.35: target=shoulder+(target-shoulder).normalized()*1.35
        if case['name']=='InfectionPulse':
            target.z += corrections.get(case['name'],0.)*value(frame,[(1,0),(59,0),(67,1),(72,1),(79,0),(120,0)])
        if case['name']=='HeavyStagger':
            target.z += corrections.get(case['name'],0.)*value(frame,[(1,0),(29,0),(35,1),(42,1),(55,0),(90,0)])
        if case['name']=='AwakenRoar':
            target.z += corrections.get(case['name'],0.)*value(frame,[(1,0),(9,0),(18,1),(24,1),(37,0),(96,0)])
        wrist_matrix=Matrix.LocRotScale(target,hand_q,base_world[wrist_name].to_scale())
        set_world(wrist_name,wrist_matrix)
        for n in finger_names:
            loc,rot,scale=bases[n]
            rig.pose.bones[n].matrix_basis=Matrix.LocRotScale(loc+finger_directions[n]*(.075*p['grip']),rot,scale)
        bpy.context.view_layer.update()
        # Outward/high elbow: fixed sign and anatomical bend plane, no flips.
        shoulder=rig.matrix_world @ rig.pose.bones['segment_002_01'].head
        elbow=rig.matrix_world @ rig.pose.bones['segment_002_01'].tail
        pole=rig.matrix_world @ rig.pose.bones['CTRL-pole_segment'].head
        axis=(target-shoulder).normalized()
        def projected(v):
            projected=v-axis*v.dot(axis)
            if projected.length<1e-7: projected=Vector((0,0,1))-axis*axis.z
            return projected.normalized()
        a,b=projected(pole-shoulder),projected(elbow-shoulder)
        bias=math.atan2(axis.dot(a.cross(b)),a.dot(b))
        desired_dir=projected(root_q @ Vector((1,-.05,.65)))
        pole_matrix=base_world['CTRL-pole_segment'].copy()
        pole_matrix.translation=shoulder+(Quaternion(axis,-bias) @ desired_dir)*1.5
        set_world('CTRL-pole_segment',pole_matrix)
        for spec, fw in zip(foot_specs, foot_worlds):
            move=fw @ base_world[spec['ctrl']].inverted()
            for n in spec['skins']: set_world(n,move @ base_world[n])
        pose={n:rig.pose.bones[n].matrix_basis.decompose() for n in names}
        if measure:
            dg=bpy.context.evaluated_depsgraph_get()
            for n in KEEP:
                obj=bpy.data.objects[n].evaluated_get(dg)
                if obj.data.vertices: bounds.extend(obj.matrix_world @ Vector(c) for c in obj.bound_box)
            evaluated=mesh.evaluated_get(dg)
            low=min((evaluated.matrix_world @ v.co).z for v in evaluated.data.vertices)
            arm_low=min((evaluated.matrix_world @ evaluated.data.vertices[i].co).z for i in arm_ids)
            skin_lows.append(low)
            ar=(shoulder-target).length/(rig.pose.bones['segment_002_01'].length+rig.pose.bones['segment_002_02'].length)
            arm_max=max(arm_max,ar)
            for spec,fw in zip(foot_specs,foot_worlds):
                hip=rig.matrix_world @ rig.pose.bones[spec['thigh']].head
                ratio=(hip-fw.translation).length/spec['length']
                leg_max=max(leg_max,ratio)
                if ratio>1.001:
                    print('COMBAT_UNREACHABLE_KEY',case['name'],frame,spec['ctrl'],ratio,list(hip),list(fw.translation),flush=True)
            contacts.append(dict(clip=case['name'],frame=frame,minimum_skin_z=low,arm_low=arm_low,wrist=list(target)))
        return pose

    print('STAGE2_COMBAT_BASE', 'palm', list(palm), 'controls', controls, flush=True)
    # Fit only two authored contacts to actual forearm/palm skin, not wrist Z.
    for kind,frame in [('InfectionPulse',67),('HeavyStagger',35),('AwakenRoar',18)]:
        case=next(c for c in CASES if c['name']==kind)
        correction=0.
        for _ in range(3):
            corrections[kind]=correction; posed(case,frame)
            obj=mesh.evaluated_get(bpy.context.evaluated_depsgraph_get())
            low=min((obj.matrix_world @ obj.data.vertices[i].co).z for i in arm_ids)
            delta=floor+.004-low
            if abs(delta)<.0008: break
            correction+=delta
        corrections[kind]=correction
        assert abs(correction)<.16,(kind,correction)
    for case in CASES:
        sample=set(case['event_frames'].values())|{1,case['frames']}
        records=[(posed(case,f,measure=f in sample),{}) for f in range(1,case['frames']+1)]
        action=bpy.data.actions.new('MC_Stage2_New_'+case['name']); action.use_fake_user=True
        helpers.bake(rig,action,defaults,records)
        action['labelRU']=case['labelRU']; action['useRU']=case['useRU']
        action['MC_source_fps']=30; action['MC_source_method']='Authored asymmetric pose performance on preserved native skin'
        action['MC_events_json']=json.dumps(case['event_frames'])
        case['action']=action.name
        print('STAGE2_COMBAT_ACTION_READY',case['name'],case['frames'],flush=True)
    assert all(helpers.signature(bpy.data.actions[n])==sig for n,sig in originals.items())
    assert arm_max<.95,(arm_max,'arm')
    assert leg_max<1.001,(leg_max,'leg')
    if min(skin_lows)<floor-.010:
        raise AssertionError(('Skin penetrates floor at key pose',min(skin_lows),floor,[c for c in contacts if c['minimum_skin_z']<floor-.010]))
    make_face_actions(CASES)
    lo=Vector([min(v[i] for v in bounds) for i in range(3)])
    hi=Vector([max(v[i] for v in bounds) for i in range(3)])
    focus=(lo+hi)*.5; cameras={}
    for view,offset in [('ThreeQuarter',(3,-7,2.5)),('Front',(0,-7,1.65))]:
        name='MC_Carries_'+view
        camera=bpy.data.objects.get(name)
        if camera is None:
            camera=bpy.data.objects.new(name,bpy.data.cameras.new(name)); scene.collection.objects.link(camera)
        camera.location=focus+Vector(offset)
        camera.rotation_euler=(focus-camera.location).to_track_quat('-Z','Y').to_euler(); camera.data.type='ORTHO'
        bpy.context.view_layer.update(); inverse=camera.matrix_world.inverted()
        camera.data.ortho_scale=max(max(2*abs((inverse @ v).x)/.89,2*abs((inverse @ v).y)*(800/600)/.89) for v in bounds)
        cameras[view]=dict(name=name,scale=camera.data.ortho_scale)
    scene.camera=bpy.data.objects[cameras['ThreeQuarter']['name']]
    scene.render.fps,scene.render.fps_base=30,1.
    scene.render.resolution_x,scene.render.resolution_y=800,600
    scene.render.engine='BLENDER_WORKBENCH'
    scene.display.render_aa='5'; scene.display.shading.color_type='TEXTURE'
    scene.display.shading.light='STUDIO'; scene.display.shading.studio_light='paint.sl'
    scene.display.shading.show_shadows=True; scene.display.shading.show_cavity=True
    # Timeline concatenates editable actions; saving neutral controls makes the
    # standalone action renderer restore the correct sole bind for every clip.
    helpers.author.bind(rig,None,defaults)
    track=rig.animation_data.nla_tracks.new(); track.name='MC_PHASE2_NEW_PERFORMANCES'
    scene.timeline_markers.clear(); cursor=1
    for case in CASES:
        action=bpy.data.actions[case['action']]
        strip=track.strips.new(case['name'],cursor,action)
        strip.action_frame_start,strip.action_frame_end=1,case['frames']+1
        strip.frame_end=cursor+case['frames']; strip.action_slot=action.slots[0]
        strip.blend_type='REPLACE'; strip.extrapolation='NOTHING'
        strip.blend_in=0; strip.blend_out=0; strip.use_auto_blend=False
        case['preview_start']=cursor; case['preview_end']=cursor+case['frames']-1
        scene.timeline_markers.new(case['labelRU'],frame=cursor)
        for label,frame in case['event_frames'].items(): scene.timeline_markers.new(label,frame=cursor+frame-1)
        cursor+=case['frames']
    rig.animation_data.use_nla=True; track.mute=False
    scene.frame_start,scene.frame_end=1,cursor-1; scene.frame_set(1)
    note=bpy.data.texts.new('MC_PHASE2_COMBAT_README')
    note.write('Eight new asymmetric stage-two boss performances, Blender only.\n'
               'Play timeline MC_PHASE2_NEW_PERFORMANCES or select an individual MC_Stage2_New_* Action.\n'
               'Original primary ARP frozen; BoneBrush controls and four reviewed sole skin bones animate.\n'
               'Authored pose beats, not new downloaded Mixamo animations. No attack combos.\n'
               'True 30 FPS dense curves, stable quaternion signs; render evaluates at 60 FPS subframes.\n'
               'The native Mouth_Surprise/Mouth_Angry/Open_Mouth shapes have synchronized editable face Actions.\n'
               'Ground infection pulse has no VFX/hitboxes yet. Approval required before Unreal integration.\n'
               'Forward shove has a short world lunge and returns; support pivot is in place with a neutral return.\n')
    DEST.mkdir(parents=True,exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(BLEND))
    assert all(helpers.author.sha(p)==digest for p,digest in hashes.items())
    report=dict(passed=True,blend=BLEND.name,blend_sha256=helpers.author.sha(BLEND),
                source='../HandSupportWalk/'+SOURCE.name,source_sha256=hashes[SOURCE],
                parent_review=str(SOURCE),parent_review_sha256=hashes[SOURCE],
                historical_user_source=parent['source'],historical_user_source_sha256=parent['source_sha256'],
                artist_motion_source=parent['artist_motion_source'],artist_motion_source_sha256=parent['artist_motion_source_sha256'],
                artist_motion_is_historical_reference=True,
                artist_motion_reference_only=True,
                fps=30,clips=CASES,cameras=cameras,bounds=[list(lo),list(hi)],timeline_frames=cursor-1,
                primary_arp_pose_frozen=True,original_actions_preserved=True,original_files_unchanged=True,
                source_method='New authored asymmetric pose performances on the reviewed native rig; no new Mixamo download',
                skin_contact_floor=floor,minimum_keypose_skin_z=min(skin_lows),contact_corrections=corrections,
                max_arm_reach_ratio=arm_max,max_leg_reach_ratio=leg_max,keypose_checks=contacts,
                scope='Blender-only new phase-two animation pool for video approval before Unreal integration.',
                limitations=['Preview animation performances; gameplay hitboxes, VFX and arena speed are not integrated.',
                             'Asymmetric original BoneBrush/ARP skin retained; bespoke clips are not automatic humanoid Mixamo retargets.'])
    REPORT.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
    (DEST/'README.txt').write_text(note.as_string(),encoding='utf8')
    print('STAGE2_COMBAT_POOL_READY',str(BLEND),str(REPORT),flush=True)


def face_values(kind, frame):
    """Existing artist expression keys only; never invent an unsupported blink."""
    if kind=='AwakenRoar':
        surprise=value(frame,[(1,0),(21,.08),(37,.45),(48,.8),(62,.70),(82,.1),(96,0)])
        angry=value(frame,[(1,0),(25,.30),(42,.40),(59,.35),(84,.10),(96,0)])
    elif kind=='ForwardHammer':
        surprise=value(frame,[(1,0),(33,.08),(47,.32),(56,.22),(85,0),(108,0)])
        angry=value(frame,[(1,0),(25,.38),(42,.58),(58,.52),(87,.10),(108,0)])
    elif kind=='WideBackhand':
        surprise=value(frame,[(1,0),(35,.06),(46,.28),(58,.20),(85,0),(102,0)])
        angry=value(frame,[(1,0),(31,.47),(49,.62),(64,.36),(89,0),(102,0)])
    elif kind=='PalmShove':
        surprise=value(frame,[(1,0),(36,.08),(47,.25),(65,.16),(89,0),(102,0)])
        angry=value(frame,[(1,0),(24,.33),(43,.58),(66,.5),(90,.1),(102,0)])
    elif kind=='InfectionPulse':
        surprise=value(frame,[(1,0),(32,.2),(53,.38),(67,.55),(78,.3),(104,.07),(120,0)])
        angry=value(frame,[(1,0),(29,.3),(53,.48),(70,.42),(90,.2),(120,0)])
    elif kind=='HeavyStagger':
        surprise=value(frame,[(1,0),(10,0),(15,.62),(23,.47),(40,.23),(65,.05),(90,0)])
        angry=value(frame,[(1,0),(10,0),(15,.3),(29,.4),(50,.23),(73,.04),(90,0)])
    elif kind=='ExhaustedRecovery':
        surprise=value(frame,[(1,0),(27,.32),(55,.40),(74,.30),(95,.16),(120,0)])
        angry=value(frame,[(1,0),(24,.13),(66,.15),(86,.12),(109,0),(120,0)])
    else:
        surprise=value(frame,[(1,0),(25,.06),(70,.10),(103,.02),(120,0)])
        angry=value(frame,[(1,0),(32,.2),(70,.25),(104,.08),(120,0)])
    return {'Mouth_Surprise':surprise*.50,'Mouth_Angry':angry*.65,'Open_Mouth':surprise*.33}


def make_face_actions(clips):
    objects=[bpy.data.objects[n] for n in KEEP if bpy.data.objects[n].data.shape_keys]
    for row in clips:
        row['shape_actions']={}
        for obj in objects:
            keys=obj.data.shape_keys
            supported=[name for name in ('Mouth_Surprise','Mouth_Angry','Open_Mouth') if name in keys.key_blocks]
            if not supported: continue
            keys.animation_data_create(); keys.animation_data.action=None
            action=bpy.data.actions.new('MC_Stage2_Face_'+row['name']+'_'+obj.name)
            keys.animation_data.action=action; action.use_fake_user=True
            for frame in range(1,row['frames']+1):
                values=face_values(row['name'],frame)
                for name in supported:
                    key=keys.key_blocks[name]; key.value=values[name]
                    key.keyframe_insert(data_path='value',frame=frame,group='Native expression')
            for fc in helpers.author.curves(action):
                for key in fc.keyframe_points: key.interpolation='LINEAR'
            row['shape_actions'][obj.name]=action.name
            keys.animation_data.action=None
            for name in supported: keys.key_blocks[name].value=0.
    # The authoring timeline also shows its synchronized facial performance.
    for obj in objects:
        keys=obj.data.shape_keys; keys.animation_data_create()
        for old in keys.animation_data.nla_tracks: old.mute=True
        track=keys.animation_data.nla_tracks.new(); track.name='MC_PHASE2_NATIVE_FACE'
        cursor=1
        for row in clips:
            if obj.name in row['shape_actions']:
                action=bpy.data.actions[row['shape_actions'][obj.name]]
                strip=track.strips.new(row['name'],cursor,action)
                strip.action_frame_start,strip.action_frame_end=1,row['frames']+1
                strip.frame_end=cursor+row['frames']; strip.action_slot=action.slots[0]
                strip.blend_type='REPLACE'; strip.extrapolation='NOTHING'
            cursor+=row['frames']
        keys.animation_data.use_nla=True; track.mute=False


def facial_pass():
    """Finish this derivative after the independent main author process."""
    report=json.loads(REPORT.read_text(encoding='utf8'))
    assert report['passed'] is True
    assert not any(row.get('shape_actions') for row in report['clips']), 'Facial actions already exist; preserve this revision'
    bpy.ops.wm.open_mainfile(filepath=str(BLEND))
    make_face_actions(report['clips'])
    bpy.context.scene.frame_set(1)
    bpy.context.preferences.filepaths.save_version=0
    bpy.ops.wm.save_as_mainfile(filepath=str(BLEND))
    report['blend_sha256']=helpers.author.sha(BLEND)
    parent=json.loads(SOURCE_REPORT.read_text(encoding='utf8'))
    report['historical_user_source']=parent['source']
    report['historical_user_source_sha256']=parent['source_sha256']
    report['blend']=BLEND.name
    report['source']='../HandSupportWalk/'+SOURCE.name; report['source_sha256']=helpers.author.sha(SOURCE)
    report['artist_motion_is_historical_reference']=True
    report['artist_motion_reference_only']=True
    report['facial_performance']='Synchronized native Mouth_Surprise/Mouth_Angry/Open_Mouth shape keys; primary ARP body remains frozen'
    REPORT.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
    print('STAGE2_COMBAT_FACE_READY',str(REPORT),flush=True)


if __name__=='__main__':
    facial_pass() if '--facial-pass' in sys.argv else main()
