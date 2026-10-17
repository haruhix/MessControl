"""Blender-only human acting pass. Original V7 and all Unreal assets stay saved as-is."""
import copy
import hashlib
import json
import math
from pathlib import Path

import bpy
from mathutils import Euler, Matrix, Quaternion, Vector

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'ArtSource/NutAnimationStyle/Wizard'
SOURCE = OUT / 'NutWizard_Performance_v7.blend'
OUTPUT = OUT / 'NutWizard_Human_v8.blend'

BASE = dict(root=(0,0,-.045), pelvis=(0,0,-5), body=(-3,1,8),
    ur=(-.25,.03,-.968), lr=(.08,-.34,-.937), ul=(.38,-.10,-.919), ll=(-.26,-.83,-.495),
    wr=(0,3,0), wl=(-3,-8,0), gr=(.12,.16,.12), gl=(.14,.18,.14),
    sh=(4,-9), fr=(-.265,.24,.156448588), fl=(.265,-.12,.156448588),
    frr=(0,0,-12), flr=(0,0,8), scale=(1,1,1))


def pose(**kw):
    return dict(BASE, **kw)


def design():
    # Coordinates: front is -Y, right is -X. The following are independently
    # staged performances, not offsets applied to the previous animation.
    ready = pose()
    return {
    'Idle': (3.0, [(0, ready),
        (.65,pose(root=(.025,0,-.035),body=(-2,-1,10),ul=(.36,-.10,-.928),ll=(-.28,-.85,-.445))),
        (1.35,pose(root=(.03,.01,-.045),body=(-4,2,5),wl=(-5,-12,0),gl=(.20,.23,.13))),
        (2.15,pose(root=(-.015,0,-.045),body=(-3,-1,9),ur=(-.29,.02,-.957))), (3.0,ready)]),
    'Walk': (1.2, [(0,ready),
        (.15,pose(root=(.035,0,-.025),pelvis=(0,0,-8),body=(3,2,4),
            fr=(-.265,-.08,.156448588),fl=(.265,.14,.23),frr=(0,0,-9),flr=(18,0,6),
            ur=(-.24,-.39,-.889),lr=(.07,-.58,-.811),ul=(.36,.20,-.911),ll=(-.12,-.12,-.985))),
        (.30,pose(root=(.045,0,-.055),pelvis=(0,0,-6),body=(2,2,4),
            fr=(-.265,-.25,.156448588),fl=(.265,.25,.156448588),
            ur=(-.26,-.44,-.86),lr=(.08,-.62,-.78),ul=(.35,.26,-.90),ll=(-.12,-.08,-.99))),
        (.45,pose(root=(.015,0,-.025),body=(3,1,7),fr=(-.265,-.06,.156448588),fl=(.265,.10,.27))),
        (.60,ready),
        (.75,pose(root=(-.035,0,-.025),pelvis=(0,0,7),body=(3,-2,13),
            fl=(.265,-.08,.156448588),fr=(-.265,.14,.23),flr=(0,0,6),frr=(18,0,-9),
            ul=(.29,-.40,-.869),ll=(-.08,-.59,-.803),ur=(-.34,.24,-.908),lr=(.12,-.06,-.991))),
        (.90,pose(root=(-.045,0,-.055),pelvis=(0,0,6),body=(2,-2,12),
            fl=(.265,-.25,.156448588),fr=(-.265,.25,.156448588),
            ul=(.29,-.44,-.850),ll=(-.08,-.62,-.780),ur=(-.34,.26,-.903),lr=(.12,-.08,-.989))),
        (1.05,pose(root=(-.015,0,-.025),body=(3,-1,10),fl=(.265,-.06,.156448588),fr=(-.265,.10,.27))),
        (1.2,ready)]),
    'Cast': (2.4, [(0,ready),
        (.24,pose(root=(-.025,.04,-.06),body=(-3,-3,-12),pelvis=(0,0,-14),
            ur=(-.70,.20,-.684),lr=(.20,-.63,.75),ul=(.42,-.44,-.794),ll=(-.30,-.84,.45),
            wr=(-5,16,0),wl=(0,-24,2),gr=(.34,.40,.25),gl=(.10,.12,.10))),
        (.70,pose(root=(-.07,.08,-.11),body=(-8,-5,-35),pelvis=(-4,0,-18),
            ur=(-.83,.24,-.50),lr=(.28,-.37,.886),ul=(.20,-.63,-.75),ll=(-.10,-.97,.22),
            wr=(-6,20,3),wl=(5,-10,0),gr=(.38,.44,.26),gl=(.06,.09,.08),sh=(15,-18))),
        (.95,pose(root=(-.07,.08,-.11),body=(-9,-4,-36),pelvis=(-4,0,-18),
            ur=(-.83,.24,-.50),lr=(.28,-.37,.886),ul=(.20,-.63,-.75),ll=(-.10,-.97,.22),
            wr=(-6,20,3),wl=(5,-10,0),gr=(.40,.45,.26),gl=(.06,.09,.08),sh=(15,-18))),
        (1.12,pose(root=(.045,-.13,-.09),body=(6,3,25),pelvis=(4,0,17),
            ur=(-.53,-.80,.28),lr=(-.16,-.97,.16),ul=(.38,.08,-.921),ll=(-.16,-.42,-.893),
            wr=(-4,8,0),wl=(0,-8,0),gr=(.03,.06,.05),gl=(.26,.31,.20),sh=(28,-5),
            fl=(.28,-.38,.156448588),frr=(20,0,-8))),
        (1.32,pose(root=(.06,-.17,-.10),body=(9,2,35),pelvis=(4,0,21),
            ur=(-.58,-.80,.15),lr=(-.20,-.972,.12),ul=(.36,.18,-.915),ll=(-.12,-.24,-.963),
            wr=(-2,4,0),wl=(0,-6,0),gr=(.04,.07,.06),gl=(.25,.30,.20),sh=(30,-4),
            fl=(.28,-.38,.156448588),frr=(23,0,-8))),
        (1.65,pose(root=(.025,-.08,-.06),body=(2,1,16),ur=(-.48,-.48,-.734),lr=(.03,-.85,-.526),
            ul=(.36,.10,-.927),ll=(-.22,-.58,-.784),fl=(.28,-.28,.156448588),frr=(6,0,-10))),
        (2.1,pose(fl=(.265,-.15,.156448588))), (2.4,ready)]),
    'Melee': (1.85, [(0,ready),
        (.28,pose(root=(-.075,.02,-.09),body=(-5,-7,-34),pelvis=(0,0,-16),
            ur=(-.83,.39,-.395),lr=(.27,-.46,.846),ul=(.27,-.37,-.889),ll=(-.32,-.84,.436),
            wr=(0,13,0),gr=(.16,.20,.12),sh=(20,-10))),
        (.65,pose(root=(-.07,.03,-.13),body=(-5,-8,-48),pelvis=(-3,0,-20),
            ur=(-.85,.43,-.303),lr=(.22,-.44,.871),ul=(.26,-.40,-.879),ll=(-.30,-.86,.412),sh=(25,-12))),
        (.78,pose(root=(.035,-.14,-.10),body=(8,6,38),pelvis=(3,0,23),
            ur=(-.46,-.84,.287),lr=(.36,-.927,.10),ul=(.38,.18,-.907),ll=(-.10,-.35,-.931),
            wr=(0,12,0),gr=(.02,.05,.04),sh=(30,-4),fl=(.29,-.35,.156448588),frr=(18,0,-8))),
        (.96,pose(root=(.045,-.17,-.11),body=(10,8,57),pelvis=(4,0,25),
            ur=(-.40,-.86,-.315),lr=(.69,-.70,-.18),ul=(.40,.20,-.894),ll=(-.10,-.24,-.966),
            wr=(0,16,0),gr=(.03,.06,.04),sh=(28,-4),fl=(.29,-.35,.156448588),frr=(22,0,-8))),
        (1.3,pose(root=(.02,-.04,-.06),body=(1,3,18),ur=(-.43,-.28,-.858),lr=(.35,-.49,-.798))),
        (1.65,ready),(1.85,ready)]),
    'HeavyCast': (2.8, [(0,ready),
        (.32,pose(root=(0,.015,-.11),body=(-7,0,-8),ur=(-.62,-.22,-.753),lr=(.40,-.52,.754),
            ul=(.62,-.22,-.753),ll=(-.40,-.52,.754),wr=(-5,28,0),wl=(-5,-28,0),gr=(.28,.33,.20),gl=(.28,.33,.20),sh=(15,-15))),
        (.95,pose(root=(-.015,.05,-.18),body=(-12,0,-16),pelvis=(-5,0,-9),
            ur=(-.82,-.10,-.562),lr=(.27,-.26,.928),ul=(.82,-.10,-.562),ll=(-.27,-.26,.928),
            wr=(-5,16,0),wl=(-5,-16,0),gr=(.10,.13,.11),gl=(.10,.13,.11),sh=(20,-20))),
        (1.25,pose(root=(0,.02,-.06),body=(-13,0,-9),
            ur=(-.65,-.20,.733),lr=(.24,-.32,.916),ul=(.65,-.20,.733),ll=(-.24,-.32,.916),
            wr=(0,10,0),wl=(0,-10,0),gr=(.08,.12,.10),gl=(.08,.12,.10),sh=(12,-12))),
        (1.48,pose(root=(0,-.04,-.025),body=(5,0,4),pelvis=(4,0,0),
            ur=(-.94,-.18,.288),lr=(-.82,-.48,.31),ul=(.94,-.18,.288),ll=(.82,-.48,.31),
            wr=(0,5,0),wl=(0,-5,0),gr=(.01,.03,.03),gl=(.01,.03,.03),sh=(5,-5))),
        (1.72,pose(root=(0,-.035,-.07),body=(7,0,7),ur=(-.95,-.20,.23),lr=(-.90,-.42,.11),
            ul=(.95,-.20,.23),ll=(.90,-.42,.11),gr=(.04,.06,.04),gl=(.04,.06,.04),sh=(5,-5))),
        (2.12,pose(root=(0,0,-.07),body=(-2,0,12),ur=(-.64,-.12,-.758),lr=(.30,-.59,-.75),
            ul=(.64,-.12,-.758),ll=(-.30,-.59,-.75))), (2.8,ready)]),
    'Summon': (3.0, [(0,ready),
        (.40,pose(root=(0,-.01,-.18),body=(16,0,7),pelvis=(-6,0,-2),
            ur=(-.35,-.45,-.822),lr=(.10,-.75,-.654),ul=(.40,-.45,-.799),ll=(-.10,-.75,-.654),
            gr=(.50,.55,.32),gl=(.50,.55,.32),sh=(8,-8))),
        (.90,pose(root=(-.02,-.02,-.23),body=(20,-3,-8),pelvis=(-7,0,-5),
            ur=(-.30,-.45,-.841),lr=(.20,-.81,-.55),ul=(.42,-.44,-.795),ll=(-.20,-.81,-.55),
            gr=(.58,.62,.36),gl=(.58,.62,.36))),
        (1.26,pose(root=(-.02,0,-.08),body=(-9,-2,-15),pelvis=(5,0,-7),
            ur=(-.68,-.25,.69),lr=(.24,-.45,.86),ul=(.68,-.25,.69),ll=(-.24,-.45,.86),
            gr=(.18,.22,.16),gl=(.18,.22,.16),wr=(0,12,0),wl=(0,-12,0),sh=(15,-15))),
        (1.55,pose(root=(0,0,-.015),body=(-12,0,6),pelvis=(3,0,2),
            ur=(-.76,-.26,.595),lr=(-.24,-.45,.86),ul=(.76,-.26,.595),ll=(.24,-.45,.86),
            gr=(.03,.06,.06),gl=(.03,.06,.06),wr=(0,8,0),wl=(0,-8,0),sh=(7,-7))),
        (1.85,pose(root=(0,-.02,-.045),body=(-8,1,12),ur=(-.80,-.25,.545),lr=(-.25,-.60,.76),
            ul=(.75,-.35,.561),ll=(.20,-.53,.824),gr=(.10,.14,.09),gl=(.09,.12,.09))),
        (2.28,pose(root=(0,0,-.06),body=(0,2,14),ur=(-.62,-.27,-.736),lr=(.10,-.85,.52),
            ul=(.38,-.15,-.913),ll=(-.22,-.74,-.635))), (3.0,ready)]),
    'Rain': (2.8, [(0,ready),
        (.35,pose(root=(-.03,.01,-.075),body=(-5,-3,-17),ur=(-.75,-.10,-.654),lr=(.22,-.31,.925),
            ul=(.30,-.30,-.905),ll=(-.22,-.80,.56),wr=(0,14,0),gr=(.16,.21,.15),gl=(.14,.18,.12),sh=(16,-10))),
        (.85,pose(root=(-.025,.015,-.055),body=(-12,-4,-15),ur=(-.45,-.15,.88),lr=(.20,-.35,.916),
            ul=(.36,-.28,-.89),ll=(-.20,-.87,.45),wr=(0,18,0),gr=(.05,.08,.05),sh=(12,-9))),
        (1.12,pose(root=(.025,-.04,-.10),body=(10,3,18),pelvis=(3,0,12),
            ur=(-.40,-.75,.526),lr=(.05,-.91,-.41),ul=(.36,.10,-.928),ll=(-.14,-.34,-.93),
            wr=(-7,9,0),gr=(.14,.20,.12),gl=(.30,.35,.23),sh=(25,-5))),
        (1.45,pose(root=(.04,-.05,-.14),body=(14,4,23),pelvis=(-2,0,12),
            ur=(-.38,-.77,.512),lr=(.07,-.88,-.47),ul=(.36,.10,-.928),ll=(-.14,-.34,-.93),
            wr=(-8,7,0),gr=(.18,.24,.15),gl=(.33,.38,.23),sh=(25,-5))),
        (1.80,pose(root=(.035,-.04,-.105),body=(10,3,20),ur=(-.38,-.75,.54),lr=(.07,-.90,-.43),
            ul=(.36,.10,-.928),ll=(-.14,-.34,-.93),wr=(-6,9,0),gr=(.14,.20,.12),gl=(.30,.35,.23),sh=(25,-5))),
        (2.05,pose(root=(.025,-.05,-.13),body=(13,4,22),ur=(-.38,-.77,.512),lr=(.07,-.88,-.47),
            ul=(.36,.10,-.928),ll=(-.14,-.34,-.93),wr=(-8,7,0),gr=(.18,.24,.15),gl=(.33,.38,.23),sh=(25,-5))),
        (2.45,pose(root=(.01,0,-.06),body=(1,0,12),ur=(-.47,-.40,-.787),lr=(.12,-.62,-.775))), (2.8,ready)]),
    'Hit': (.75, [(0,ready),
        (.08,pose(root=(-.02,.08,-.10),body=(-16,-8,-18),pelvis=(-3,0,-10),
            ur=(-.72,-.12,-.684),lr=(.30,-.45,.84),ul=(.55,-.32,-.772),ll=(-.32,-.73,.60),
            gr=(.08,.12,.08),gl=(.08,.12,.08),sh=(12,-14))),
        (.18,pose(root=(-.04,.15,-.13),body=(-22,-12,-27),pelvis=(-6,0,-14),
            ur=(-.73,.06,-.68),lr=(.35,-.35,.869),ul=(.60,-.10,-.794),ll=(-.30,-.56,.772),
            gr=(.14,.18,.12),gl=(.14,.18,.12),fr=(-.29,.44,.18),frr=(8,0,-16),sh=(14,-16))),
        (.35,pose(root=(-.03,.08,-.15),body=(8,-6,-8),pelvis=(-8,0,-4),
            ur=(-.53,-.30,-.79),lr=(.22,-.60,.77),ul=(.49,-.27,-.828),ll=(-.30,-.73,.61),
            fr=(-.29,.40,.156448588))),
        (.55,pose(root=(-.01,.02,-.07),body=(2,-2,4))), (.75,ready)]),
    'Death': (3.2, [(0,ready),
        (.18,pose(root=(-.03,.10,-.13),body=(-19,-10,-24),pelvis=(-6,0,-15),
            ur=(-.63,-.15,-.762),lr=(.34,-.53,.776),ul=(.56,-.22,-.799),ll=(-.29,-.61,.737),gr=(.10,.14,.10),gl=(.10,.14,.10))),
        (.58,pose(root=(-.07,.08,-.22),body=(16,-13,-12),pelvis=(-10,0,-7),
            ur=(-.30,-.60,-.742),lr=(.14,-.95,.278),ul=(.49,-.08,-.868),ll=(-.24,-.54,.806),
            fr=(-.29,.34,.156448588),fl=(.28,-.30,.156448588))),
        (1.05,pose(root=(-.10,.06,-.28),body=(18,-16,-8),pelvis=(-13,0,-4),
            ur=(-.48,-.70,-.529),lr=(.10,-.95,.296),ul=(.44,-.08,-.894),ll=(-.14,-.46,-.877),
            gr=(.04,.08,.06),gl=(.23,.29,.17),fr=(-.31,.36,.156448588),fl=(.30,-.30,.156448588))),
        (1.40,pose(root=(-.10,.05,-.35),body=(22,-22,-5),pelvis=(-20,0,-2),
            ur=(-.36,-.32,-.876),lr=(.20,-.66,-.724),ul=(.38,-.15,-.913),ll=(-.18,-.54,-.823),
            gr=(.10,.14,.10),gl=(.14,.18,.12),fr=(-.33,.30,.156448588),fl=(.31,-.25,.156448588))),
        (1.80,pose(root=(-.19,-.10,-.43),body=(38,-32,6),pelvis=(-28,-8,6),
            ur=(-.58,-.38,-.72),lr=(.06,-.88,-.47),ul=(.58,-.30,-.757),ll=(-.06,-.86,-.50),
            gr=(.04,.06,.04),gl=(.04,.06,.04),fr=(-.36,.28,.156448588),fl=(.33,-.25,.156448588))),
        (2.12,pose(root=(-.28,-.23,-.48),body=(46,-40,14),pelvis=(-34,-10,10),
            ur=(-.60,-.23,-.766),lr=(.08,-.91,-.407),ul=(.58,-.20,-.789),ll=(-.09,-.90,-.426),
            gr=(.03,.05,.04),gl=(.03,.05,.04),fr=(-.40,.27,.156448588),fl=(.34,-.23,.156448588))),
        (2.55,pose(root=(-.28,-.23,-.46),body=(44,-39,12),pelvis=(-33,-9,8),
            ur=(-.60,-.23,-.766),lr=(.08,-.91,-.407),ul=(.58,-.20,-.789),ll=(-.09,-.90,-.426),
            gr=(.08,.11,.06),gl=(.08,.11,.06),fr=(-.40,.27,.156448588),fl=(.34,-.23,.156448588))),
        (3.2,pose(root=(-.28,-.23,-.46),body=(44,-39,12),pelvis=(-33,-9,8),
            ur=(-.60,-.23,-.766),lr=(.08,-.91,-.407),ul=(.58,-.20,-.789),ll=(-.09,-.90,-.426),
            gr=(.08,.11,.06),gl=(.08,.11,.06),fr=(-.40,.27,.156448588),fl=(.34,-.23,.156448588)))])}


def stage_palms(performances):
    """Define human hand intent, then let the forearm plane carry palm orientation.

    A wrist Euler roll cannot turn forward-pointing fingers into a presented
    palm. For each action the actual palm and finger directions are authored;
    the elbow plane is reconstructed from these instead of twisting the wrist.
    """
    for label, (_, knots) in performances.items():
        for at,p in knots:
            if label=='Walk' and at not in (0,.60,1.2):
                old={k:p[k] for k in ('ur','ul','lr','ll')}
                for part in ('u','l'):
                    p[part+'r']=(-old[part+'l'][0],old[part+'l'][1],old[part+'l'][2])
                    p[part+'l']=(-old[part+'r'][0],old[part+'r'][1],old[part+'r'][2])
            p['nr']=(1,-.16,0);p['nl']=(-1,-.16,0)
            p['dr']=p['lr'];p['dl']=p['ll']
            p['br']=(math.degrees(Vector(p['ur']).angle(Vector(p['lr']))),)
            p['bl']=(math.degrees(Vector(p['ul']).angle(Vector(p['ll']))),)
            for side in ('r','l'):
                if p['b'+side][0]<15:p['b'+side]=(15.,)
            if label=='Cast' and .24<=at<=1.32:
                p['nr']=(0,0,1);p['dr']=(0,-1,0)
                p['lr']=(.35,-.88,.31)
                if at>=1.12:
                    p['nr']=(0,-1,0);p['dr']=(0,0,1)
                    p['lr']=(.10,-.6502,.7532);p['br']=(100.,)
                    p['ur']=(-.9451,-.1879,-.2672)
                    p['gr']=(.08,.12,.30)
                    p['ul']=(.62,.08,-.780);p['ll']=(-.18,-.86,.477)
                    p['dl']=(.10,-.98,.17)
                if at<=.95:
                    p['ll']=(-.22,-.82,-.529);p['dl']=p['ll']
            elif label=='Melee' and .28<=at<=.96:
                p['nr']=(0,-1,0);p['dr']=(.85,0,.526)
                p['ur']=(-.68,.18,-.711);p['lr']=(.60,-.65,.465)
                p['gr']=(.52,.58,.36)
                if at>=.78:
                    p['ur']=(-.42,-.20,-.885) if at<.9 else (-.40,-.86,-.315)
                    p['lr']=(.80,-.55,-.237) if at<.9 else (.89,-.22,-.38)
                    p['dr']=(.95,0,-.31) if at<.9 else (.90,0,-.43)
            elif label in ('HeavyCast','Summon') and .3<=at<=1.85:
                for side in ('r','l'):
                    if label=='Summon' and at<1.:
                        p['n'+side]=(0,0,1);p['d'+side]=(0,-1,0)
                    else:
                        p['n'+side]=(0,-1,0)
                        p['d'+side]=(0,0,1) if at<1.48 else ((-.85 if side=='r' else .85),0,.53)
                if label=='HeavyCast' and at in (.32,.95,1.25):
                    p['lr']=(.23,-.40,.887);p['ll']=(-.23,-.40,.887)
                    p['br']=(105.,);p['bl']=(105.,)
                    if at==1.25:
                        p['root']=(0,.02,-.20);p['body']=(8,0,-12)
                        p['sh']=(16,-16)
                        p['ur']=(-.74,-.18,-.648);p['ul']=(.74,-.18,-.648)
                if label=='Summon' and at>=1.26:
                    p['nr']=(0,0,1);p['dr']=(0,-1,0)
                    p['ur']=(-.67,-.34,.659);p['lr']=(.20,-.82,.536)
                    p['nl']=(0,0,-1);p['dl']=(0,-1,0)
                    p['ul']=(.42,-.35,-.837);p['ll']=(-.20,-.84,-.505)
                    p['root']=(-.06,-.025,-.13 if at<1.85 else -.15)
                    p['fl']=(.28,-.26,.156448588)
            elif label=='Rain' and .35<=at<=2.05:
                p['nr']=(0,-1,0);p['dr']=(0,0,1)
                if at>=.85:
                    p['nr']=(0,0,-1);p['dr']=(0,-1,0)
                    if at==.85:p['lr']=(.20,-.94,.274);p['br']=(65.,)
                p['ul']=(.62,.08,-.780);p['ll']=(-.18,-.86,.477)
                p['nl']=(-1,-.1,.15);p['dl']=(.10,-.98,.17)
            elif label=='Hit' and at<=.35 and at>0:
                for side in ('r','l'):
                    p['n'+side]=(0,-1,0);p['d'+side]=(0,0,1)
            elif label=='Death' and at>0:
                if at<=1.05:
                    p['nr']=(0,-1,0);p['dr']=(0,0,1)
                    if at==1.05:p['lr']=(.1,-.70,.706);p['br']=(90.,)
                else:
                    if at>=1.8:
                        p['body']=((28,-55,12) if at<2.12 else (12,-74,16) if at<2.55 else (8,-80,16))
                        p['pelvis']=(-18,-50,8) if at<2.12 else (-8,-65,12)
                        p['ul']=(.60,.50,-.624);p['ll']=(.20,.89,-.41)
                        p['root']=(-.28,-.23,-.44)
                    torso=Euler(tuple(math.radians(v) for v in p['body']),'XYZ').to_quaternion()
                    for side in ('r','l'):
                        p['n'+side]=tuple(torso.inverted()@Vector((0,0,1)))
                        p['d'+side]=tuple(torso.inverted()@Vector((0,-1 if side=='r' else 1,0)))
    return performances


def main(labels=None):
    import sys
    sys.path.insert(0,str(ROOT/'Tools/Blender'))
    from author_wizard_performance_v7 import hermite, curves, world_rotation
    assert Path(bpy.data.filepath).name in (SOURCE.name, OUTPUT.name)
    performances=stage_palms(design())
    selected=set(performances) if labels is None else set(labels)
    assert selected and selected<=set(performances)
    for label in performances:
        if label not in selected:continue
        old=bpy.data.actions.get('MC_WizardV8_'+label)
        if old:old.name='MC_PrePalmV8_'+label
    scene=bpy.context.scene
    rig,mesh=bpy.data.objects['rig'],bpy.data.objects['SM_NutWizard']
    assert rig.get('MC_PerformanceRigV7')
    source_sha=hashlib.sha256(SOURCE.read_bytes()).hexdigest()
    rest={b.name:b.matrix_local.copy() for b in rig.data.bones}
    signature=[(b.name,b.parent.name if b.parent else None,list(map(list,b.matrix_local))) for b in rig.data.bones]
    props={b.name:{k:b[k] for k in b.keys() if isinstance(b[k],(bool,int,float))} for b in rig.pose.bones}
    rig.animation_data.action=None
    rig.animation_data.use_nla=False
    for track in rig.animation_data.nla_tracks: track.mute=True
    foot_ids={s:[v.index for v in mesh.data.vertices if (mesh.matrix_world@v.co).z<.19 and ((mesh.matrix_world@v.co).x<0)==(s=='r')] for s in ('r','l')}
    body_ids=json.loads((OUT/'WizardPerformanceV7Report.json').read_text(encoding='utf-8'))['rig']['body_vertex_indices']
    names=['c_root_master.x','c_root.x','c_head.x']
    for side in ('r','l'):
        names += [n+'.'+side for n in ('c_shoulder_shell','c_arm_shell_fk','c_forearm_fk','c_hand_fk','c_foot_ik','c_leg_pole')]
        names += [f'c_{d}{j}.{side}' for d in ('ring','pinky','thumb') for j in (1,2,3)]
        names += [f'c_{d}1_base.{side}' for d in ('ring','pinky','thumb')]

    def reset():
        rig.animation_data.action=None
        for b in rig.pose.bones:
            b.matrix_basis.identity()
            for k,v in props[b.name].items(): b[k]=v
        for side in ('r','l'):
            rig.pose.bones['c_hand_ik.'+side]['ik_fk_switch']=1.
            rig.pose.bones['c_foot_ik.'+side]['ik_fk_switch']=0.
            rig.pose.bones['c_hand_ik.'+side]['auto_stretch']=0.
            rig.pose.bones['c_foot_ik.'+side]['auto_stretch']=0.
            rig.pose.bones['c_hand_fk.'+side]['fingers_grasp']=0.
            for d in ('ring','pinky','thumb'): rig.pose.bones[f'c_{d}1_base.{side}']['bend_all']=0.
        bpy.context.view_layer.update()

    def apply(knots,t,label):
        reset()
        def channel(field,delay=0):
            return hermite([(at,p[field]) for at,p in knots],max(0,t-delay))
        master=rig.pose.bones['c_root_master.x']
        m=rest[master.name].copy();m.translation+=Vector(channel('root'));master.matrix=m
        bpy.context.view_layer.update()
        world_rotation(rig.pose.bones['c_root.x'],channel('pelvis'))
        bpy.context.view_layer.update()
        shell=rig.pose.bones['c_head.x'];world_rotation(shell,channel('body',.025 if label not in ('Idle','Walk') else 0))
        shell.scale=channel('scale');bpy.context.view_layer.update()
        sh=channel('sh',.025)
        for i,side in enumerate(('r','l')):
            shoulder=rig.pose.bones['c_shoulder_shell.'+side]
            shoulder.rotation_mode='XYZ';shoulder.rotation_euler=(0,0,math.radians(sh[i]))
        bpy.context.view_layer.update()
        for side in ('r','l'):
            arm=rig.pose.bones['c_arm_shell_fk.'+side];fore=rig.pose.bones['c_forearm_fk.'+side];hand=rig.pose.bones['c_hand_fk.'+side]
            delay=.025 if side=='r' else .015
            if label in ('Idle','Walk'):delay=0
            delta=shell.matrix.to_quaternion()@rest['c_head.x'].to_quaternion().inverted()
            lower=delta@Vector(channel('l'+side,delay+.015)).normalized()
            sign=1 if side=='r' else -1
            normal=Vector(channel('n'+side,delay+.025)).normalized()
            z=-normal
            digits=Vector(channel('d'+side,delay+.025))
            digits=(digits-z*digits.dot(z)).normalized()
            x=digits.cross(z).normalized()
            target_local=Matrix((x,digits,z)).transposed().to_quaternion()
            target=delta@target_local
            desired_digits=target@Vector((0,1,0))
            upper=delta@Vector(channel('u'+side,delay)).normalized()
            cross=upper.cross(lower)
            assert cross.length>.025,(label,t,side,'Elbow singularity')
            hinge=cross.normalized()*sign
            bend=math.acos(max(-1,min(1,upper.dot(lower))))
            arm.rotation_mode='QUATERNION'
            m=Matrix((upper.cross(hinge),upper,hinge)).transposed().to_4x4();m.translation=arm.matrix.translation;arm.matrix=m
            relative=rest[arm.name].to_3x3().inverted()@rest[fore.name].to_3x3();rest_y=relative@Vector((0,1,0))
            fore.rotation_mode='QUATERNION'
            hinge_rotation=Euler((0,0,sign*bend-math.atan2(-rest_y.x,rest_y.y)),'XYZ').to_quaternion()
            fore.rotation_quaternion=hinge_rotation
            bpy.context.view_layer.update()
            hand.rotation_mode='QUATERNION'
            hand.rotation_quaternion=Quaternion()
            bpy.context.view_layer.update()
            baseline=hand.matrix.to_quaternion()
            residual=baseline.inverted()@target
            swing,twist=residual.to_swing_twist('Y')
            # Human pronation belongs largely to the forearm. Distribute it
            # there while preserving the hinge and segment direction; keep
            # the remaining hand twist small instead of rotating a wrist 180°.
            fore.rotation_quaternion=hinge_rotation@Quaternion((0,1,0),twist*.78)
            bpy.context.view_layer.update()
            hand.rotation_quaternion=Quaternion();bpy.context.view_layer.update()
            hand.rotation_quaternion=hand.matrix.to_quaternion().inverted()@target
            bpy.context.view_layer.update()
            grip=channel('g'+side,delay+.035)
            for digit,amount in zip(('ring','pinky','thumb'),grip):rig.pose.bones[f'c_{digit}1_base.{side}']['bend_all']=amount*(.85 if digit=='thumb' else .65)
            bpy.context.view_layer.update()
            palm=-(hand.matrix.to_quaternion()@Vector((0,0,1)))
            for digit,amount in zip(('ring','pinky','thumb'),grip):
                first=rig.pose.bones[f'c_{digit}1.{side}'];first.rotation_mode='QUATERNION'
                if digit=='thumb':
                    axis=first.matrix.to_quaternion().inverted()@(-palm)
                    first.rotation_quaternion=Quaternion(axis,math.radians(min(amount*70,40))*(-1 if side=='r' else 1))
                    bpy.context.view_layer.update()
                if digit=='pinky':
                    hand_q=hand.matrix.to_quaternion();first_q=first.matrix.to_quaternion()
                    direction=hand_q.inverted()@(first_q@Vector((0,1,0)))
                    target_fan=math.radians(12 if side=='r' else -12)
                    fan=target_fan-math.atan2(direction.x,direction.y)
                    axis=first_q.inverted()@(hand_q@Vector((0,0,-1)))
                    first.rotation_quaternion=first.rotation_quaternion@Quaternion(axis,fan)
                    bpy.context.view_layer.update()
                direction=first.matrix.to_quaternion()@Vector((0,1,0))
                axis=first.matrix.to_quaternion().inverted()@direction.cross(palm).normalized()
                first.rotation_quaternion=first.rotation_quaternion@Quaternion(axis,min(amount*.70,.25 if digit=='thumb' else .46))
                bpy.context.view_layer.update()
        for side in ('r','l'):
            foot=rig.pose.bones['c_foot_ik.'+side]
            rot=Euler(tuple(math.radians(v) for v in channel('f'+side+'r')),'XYZ').to_matrix().to_4x4()
            m=rot@rest[foot.name];m.translation=Vector(channel('f'+side));foot.matrix=m
            pole=rig.pose.bones['c_leg_pole.'+side];m=rest[pole.name].copy()
            m.translation=Vector((-.30 if side=='r' else .30,-.48,.32));pole.matrix=m
        bpy.context.view_layer.update()
        geometry=mesh.evaluated_get(bpy.context.evaluated_depsgraph_get());evaluated=geometry.to_mesh()
        mins={s:min((geometry.matrix_world@evaluated.vertices[i].co).z for i in foot_ids[s]) for s in ('r','l')}
        geometry.to_mesh_clear()
        for side in ('r','l'):
            foot=rig.pose.bones['c_foot_ik.'+side];m=foot.matrix.copy()
            # Explicit airborne feet retain lift; grounded feet keep their sole at z=0.
            lift=max(0,channel('f'+side)[2]-.156448588)
            m.translation.z+=lift-mins[side];foot.matrix=m
        bpy.context.view_layer.update()
        if label=='Death' and t>=1.8:
            geometry=mesh.evaluated_get(bpy.context.evaluated_depsgraph_get());evaluated=geometry.to_mesh()
            bottom=min((geometry.matrix_world@evaluated.vertices[i].co).z for i in body_ids)
            geometry.to_mesh_clear()
            feet={s:rig.pose.bones['c_foot_ik.'+s].matrix.copy() for s in ('r','l')}
            contact=min(1,(t-1.8)/.32)
            m=master.matrix.copy();m.translation.z+=(.012-bottom)*contact;master.matrix=m
            bpy.context.view_layer.update()
            for s,m in feet.items():rig.pose.bones['c_foot_ik.'+s].matrix=m
            bpy.context.view_layer.update()

    report=[]
    for label,(duration,knots) in performances.items():
        if label not in selected:continue
        action=bpy.data.actions.new('MC_WizardV8_'+label);action.use_fake_user=True
        previous={};samples=[]
        end=int(round(duration*30))+1
        for frame in range(1,end+1):
            t=(frame-1)/30
            apply(knots,t,label)
            rig.animation_data.action=action
            for name in names:
                pb=rig.pose.bones[name];rot='rotation_quaternion' if pb.rotation_mode=='QUATERNION' else 'rotation_euler'
                if rot=='rotation_quaternion':
                    q=pb.rotation_quaternion
                    if name in previous and q.dot(previous[name])<0:q.negate()
                    previous[name]=q.copy()
                for prop in ('location',rot,'scale'):pb.keyframe_insert(prop,frame=frame,group=name)
                if 'bend_all' in pb:pb.keyframe_insert('["bend_all"]',frame=frame,group=name)
            for side in ('r','l'):
                rig.pose.bones['c_hand_ik.'+side].keyframe_insert('["ik_fk_switch"]',frame=frame)
                rig.pose.bones['c_foot_ik.'+side].keyframe_insert('["ik_fk_switch"]',frame=frame)
            if frame in (1,end) or any(abs(t-at)<1/60 for at,_ in knots):
                samples.append(dict(frame=frame,time=t,hands={s:list(rig.pose.bones['c_hand_fk.'+s].matrix.translation) for s in ('r','l')}))
        for f in curves(action):
            for k in f.keyframe_points:k.interpolation='LINEAR'
        action['duration_seconds']=duration;action['frame_start']=1;action['frame_end']=end
        action['revision']='HumanActingV8';action['game_integrated']=False
        action.asset_mark();action.asset_data.description='Human wizard acting: asymmetrical intent, support, anticipation, accents and recovery. Blender review only.'
        report.append(dict(label=label,action=action.name,duration_seconds=duration,frame_start=1,frame_end=end,story_beats=[x for x,_ in knots],samples=samples))
        print('V8_AUTHORED',label,flush=True)
    assert signature==[(b.name,b.parent.name if b.parent else None,list(map(list,b.matrix_local))) for b in rig.data.bones]
    scene.render.fps=30;scene.render.fps_base=1
    rig.animation_data.action=bpy.data.actions['MC_WizardV8_Cast'];rig.animation_data.action_slot=rig.animation_data.action.slots[0]
    scene.frame_start=1;scene.frame_end=73;scene.frame_set(35)
    for marker in list(scene.timeline_markers):scene.timeline_markers.remove(marker)
    for name,frame in [('Ready',1),('Draw',8),('Load',22),('Aim',30),('Release',35),('Follow',41),('Recover',64)]:scene.timeline_markers.new(name,frame=frame)
    bpy.context.view_layer.update()
    bpy.ops.wm.save_as_mainfile(filepath=str(OUTPUT))
    assert hashlib.sha256(SOURCE.read_bytes()).hexdigest()==source_sha
    if labels is not None:
        previous_report=json.loads((OUT/'WizardHumanV8Report.json').read_text(encoding='utf-8'))
        updated={r['label']:r for r in report}
        report=[updated.get(r['label'],r) for r in previous_report['actions']]
    result=dict(source=str(OUTPUT),source_sha256=hashlib.sha256(OUTPUT.read_bytes()).hexdigest(),actions=report,
                scope='Nine new Blender-only human-acting drafts. No Unreal import, export or gameplay changes.',source_v7_unchanged=True)
    (OUT/'WizardHumanV8Report.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    return result


if __name__=='__main__':main()
