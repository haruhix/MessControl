"""One owned solo check: the same real blade pass hits adjacent enemies/food.

Uses the normal F3 grant and held primary input. Enemy bodies are separate30cm
spheres; ingredients use separate50cm saved tomato meshes. The food fixtures
enter before the charged contact and after the initial ordinary prestrike so
both are present for the same measured charged stroke. No range damage helper
or direct HitFood/ReceiveDamage call is used. Output and recording stay on E:.
"""
import builtins
import datetime
import runpy
import traceback
from collections import defaultdict
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
RUN=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
OUTPUT=Path(globals().get('MC_KATANA_CLEAVE_OUTPUT',
    'E:/CodexWork/MessControl/Validation/KatanaFlow/Cleave_'+RUN))
ns=runpy.run_path(str(ROOT/'Tools/Unreal/check_katana_flow_solo.py'),init_globals={
    'MC_KATANA_FLOW_OUTPUT':OUTPUT,'MC_KATANA_FLOW_RECHECK_FAILED':True,
    'MC_KATANA_FLOW_CAPTURE_HZ':0.,'MC_KATANA_FLOW_TRACE_VERBOSITY':2})
Parent=ns['KatanaFlowSolo']


class KatanaCleaveSolo(Parent):
    def __init__(self):
        super().__init__()
        self.report['scope']=__doc__
        self.report['same_pass_targets']={}
        self.food_placed=False

    def begin_phase(self,phase,events=(),targets=(),camera=None):
        if phase=='held_charge':
            phase,events,targets='multi_enemy',((0.,True),(.65,False)),(
                ('enemy',110.,0.,50.,30.),('enemy',180.,0.,50.,30.))
        super().begin_phase(phase,events,targets,camera)

    def verify_cleave(self):
        contacts,cuts,state=self.finish_phase()
        originals={fixture['actor'] for _,fixture in self.targets}
        groups=defaultdict(set)
        for contact in contacts:
            if contact['target'] in originals:
                groups[(contact['attack'],contact['stroke'])].add(contact['target'])
        pairs=[dict(attack=attack,stroke=stroke,targets=sorted(targets))
               for (attack,stroke),targets in groups.items() if targets==originals]
        self.report['same_pass_targets'][self.phase]=pairs
        self.check_case(self.phase+' damages/cuts both separate targets in one actual blade pass',bool(pairs))
        self.check_case(self.phase+' retains one accepted contact per actor per stroke',
            len({(c['attack'],c['stroke'],c['target']) for c in contacts})==len(contacts))

    def tick(self,delta):
        super().tick(delta)
        if self.done or self.world is None:
            return
        try:
            age=self.now()-self.phase_at
            if self.phase=='multi_enemy' and age>1.8:
                self.verify_cleave()
                self.begin_phase('multi_food',((0.,True),(.65,False)))
            elif self.phase=='multi_food':
                if age>.43 and not self.food_placed:
                    for forward in (110.,180.):
                        self.spawn_target('food',forward,0.,25.,50.)
                    self.food_placed=True
                    self.report['food_fixture_insert_time']=self.now()
                if age>1.8:
                    self.verify_cleave()
                    self.finish('\n'.join(self.case_failures) or None)
        except Exception:
            self.finish(traceback.format_exc())


if __name__=='__main__':
    old=getattr(builtins,'mc_katana_cleave_solo',None)
    assert old is None or old.done,'Cleave probe already active'
    probe=KatanaCleaveSolo()
    builtins.mc_katana_cleave_solo=probe
    try:
        probe.start()
    except Exception:
        probe.finish(traceback.format_exc())
        raise
