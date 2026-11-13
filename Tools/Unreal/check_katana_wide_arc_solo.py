"""Owned solo katana coverage matrix using real input and actual blade contacts.

Wide horizontal/diagonal cuts and charged strokes0/1 are distinguished from
overhead/charged stroke2, which retain a vertical centre corridor. R190 is
measured coverage, not an invented guaranteed hemisphere. Behind targets must
remain untouched. No direct damage helper, forced variant or enlarged trace is
used. All generated evidence lives in an explicit E: directory.
"""
import builtins
from collections import defaultdict
import datetime
import math
from pathlib import Path
import re
import runpy
import traceback

import unreal as u


ROOT=Path(u.Paths.project_dir()).resolve()
RUN=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
OUTPUT=Path(globals().get('MC_KATANA_WIDE_OUTPUT',
    'E:/CodexWork/MessControl/Validation/KatanaWideArc/'+RUN)).resolve()
MAX_TRIALS=int(globals().get('MC_KATANA_WIDE_MAX_TRIALS',32))
FLOOR_DIAGNOSTICS=bool(globals().get('MC_KATANA_WIDE_FLOOR_DIAGNOSTICS',True))
INPUT_REGRESSION=bool(globals().get('MC_KATANA_WIDE_INPUT_REGRESSION',True))
FRONT=(-75.,-45.,0.,45.,75.)
BEHIND=(-110.,110.,180.)
SINGLES={5,6,7,9}
ns=runpy.run_path(str(ROOT/'Tools/Unreal/check_katana_flow_solo.py'),init_globals={
    'MC_KATANA_FLOW_OUTPUT':OUTPUT,'MC_KATANA_FLOW_RECHECK_FAILED':True,
    'MC_KATANA_FLOW_CAPTURE_HZ':0.,'MC_KATANA_FLOW_TRACE_VERBOSITY':2,
    'MC_KATANA_FLOW_START_RECORDING':globals().get('MC_KATANA_WIDE_START_RECORDING',
        getattr(builtins,'mc_katana_wide_recorder_factory',None))})
Parent,prop,xyz,LOGFILE=(ns[key] for key in ('KatanaFlowSolo','prop','xyz','LOGFILE'))
N=r'([-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?)'
V=rf'X={N}\s+Y={N}\s+Z={N}'
SWEEP=re.compile(rf'MC_KATANA_SWEEP attack=(\d+) stroke=(\d+).*?base={V}.*?tip={V}')


SCENARIOS=(
    dict(name='singles_r150',kind='enemy',reach=150.,height=50.,size=36.,mode='all_singles'),
    dict(name='singles_r190',kind='enemy',reach=190.,height=50.,size=36.,mode='all_singles'),
    dict(name='horizontal_precise_r150',kind='enemy',reach=150.,height=50.,size=20.,mode='horizontal'),
    dict(name='charged_r150',kind='enemy',reach=150.,height=50.,size=36.,mode='charged'),
    dict(name='charged_r190',kind='enemy',reach=190.,height=50.,size=36.,mode='charged'),
    dict(name='food_body_r150',kind='food',reach=150.,height=70.,size=50.,mode='single'),
    dict(name='food_body_r190',kind='food',reach=190.,height=70.,size=50.,mode='single'),
    dict(name='charged_food_r150',kind='food',reach=150.,height=70.,size=50.,mode='charged',delayed=True),
    dict(name='overhead_ground',kind='food',reach=150.,height=25.,size=50.,mode='overhead',angles=(0.,)),
    dict(name='overhead_high',kind='food',reach=150.,height=70.,size=50.,mode='overhead',angles=(0.,)),
    dict(name='review_front',kind='enemy',reach=150.,height=50.,size=36.,mode='charged',angles=(-60.,0.,60.),camera='front'),
    dict(name='review_side',kind='enemy',reach=150.,height=50.,size=36.,mode='charged',angles=(-60.,0.,60.),camera='side'),
    dict(name='review_top',kind='enemy',reach=150.,height=50.,size=36.,mode='charged',angles=(-60.,0.,60.),camera='top'),
)
SCENARIOS=tuple(globals().get('MC_KATANA_WIDE_SCENARIOS',SCENARIOS))


class KatanaWideArcSolo(Parent):
    def __init__(self):
        super().__init__()
        self.report.update(scope=__doc__,coverage_matrix=[],sweep_envelopes=[],
            single_variants_by_reach={},grip_samples=[],review_views=['gameplay','front','side','top'],
            niagara_samples=[],blade_floor_samples=[],
            fixture_contract=dict(front_angles=list(FRONT),behind_angles=list(BEHIND),
                reaches_cm=[150,190],enemy_radius_cm=36,horizontal_precise_radius_cm=20,
                enemy_centre_height_cm=50,food_size_cm=50,food_centre_height_cm=70,
                overhead_ground_height_cm=25,far_ring='Actual coverage; no guarantee beyond the finite blade.'))
        self.scenario_index=-1
        self.scenario=None
        self.trial_index=0
        self.seen=set()
        self.delayed_spawned=False
        self.regression_stage=None
        self.grip_baseline=None
        self.feet_baseline=None
        self.clip_key_rates={}

    def begin_phase(self,phase,events=(),targets=(),camera=None):
        if phase=='held_charge' and self.scenario_index<0:
            self.next_scenario()
            return
        super().begin_phase(phase,events,targets,camera)

    def next_scenario(self):
        self.scenario_index+=1
        if self.scenario_index>=len(SCENARIOS):
            self.scenario=None
            if not INPUT_REGRESSION:
                self.finish('\n'.join(self.case_failures) or None)
                return
            self.regression_stage='queue'
            super().begin_phase('wide_queue',((0.,True),(.025,False),(.09,True),(.115,False),(.18,True),(.205,False)))
            return
        self.scenario=SCENARIOS[self.scenario_index]
        self.trial_index=0
        self.seen=set()
        self.start_trial()

    def fixture_specs(self):
        s=self.scenario
        angles=s.get('angles',FRONT)
        if not s.get('camera'):
            angles=(*angles,*BEHIND)
        specs=[]
        for angle in angles:
            rad=math.radians(angle)
            specs.append((s['kind'],s['reach']*math.cos(rad),s['reach']*math.sin(rad),s['height'],s['size']))
        return angles,specs

    def spawn_trial_targets(self):
        angles,specs=self.fixture_specs()
        for angle,spec in zip(angles,specs):
            self.spawn_target(*spec)
            actor,fixture=self.targets[-1]
            if self.scenario['kind']=='enemy':
                actual=float(prop(actor,'body').get_scaled_sphere_radius())
                fixture['actual_query_sphere_radius_cm']=actual
                self.require(abs(actual-self.scenario['size'])<.05,
                    'Fixture body radius differs from requested radius: '+str((actual,self.scenario['size'])))
            fixture.update(polar_angle_deg=angle,radial_reach_cm=self.scenario['reach'],
                centre_height_cm=self.scenario['height'],nominal_size_cm=self.scenario['size'],
                front_target=abs(angle)<=90)
        self.delayed_spawned=True

    def start_trial(self):
        self.trial_index+=1
        s=self.scenario
        events=((0.,True),(.65,False)) if s['mode']=='charged' else ((0.,True),(.025,False))
        phase='wide_'+s['name']+'_trial%02d'%self.trial_index
        super().begin_phase(phase,events)
        self.delayed_spawned=False
        if not s.get('delayed'):
            self.spawn_trial_targets()
        if s.get('camera'):
            self.wide_review_camera(s['camera'])
        self.write()

    def wide_review_camera(self,view):
        centre=self.hero.get_actor_location()+self.hero.get_actor_forward_vector()*25+u.Vector(0,0,35)
        if view=='top':
            location=centre+u.Vector(0,0,650)
        elif view=='front':
            location=centre+self.hero.get_actor_forward_vector()*490-self.hero.get_actor_right_vector()*90+u.Vector(0,0,130)
        else:
            location=centre-self.hero.get_actor_right_vector()*490+u.Vector(0,0,125)
        self.frame_camera(location,centre)
        self.camera.get_component_by_class(u.CameraComponent).set_editor_property('field_of_view',75. if view=='top' else 55.)

    def sample(self):
        row=super().sample()
        transform=self.tool().get_world_transform()
        local={side:xyz(transform.inverse_transform_location(u.Vector(*row['pose']['hand_'+side]['location']))) for side in ('r','l')}
        tq=list(transform.rotation.to_tuple())
        def relative_quat(q):
            ax,ay,az,aw=-tq[0],-tq[1],-tq[2],tq[3]
            bx,by,bz,bw=q
            value=[aw*bx+ax*bw+ay*bz-az*by,aw*by-ax*bz+ay*bw+az*bx,
                aw*bz+ax*by-ay*bx+az*bw,aw*bw-ax*bx-ay*by-az*bz]
            length=math.sqrt(sum(x*x for x in value))
            return [x/length for x in value]
        rotation={side:relative_quat(row['pose']['hand_'+side]['rotation']) for side in ('r','l')}
        feet={side:[x-y for x,y in zip(row['pose']['foot_'+side]['location'],row['position'])] for side in ('r','l')}
        if self.phase=='equip' and row['tool_visible']:
            self.grip_baseline=local
            self.grip_rotation_baseline=rotation
            self.feet_baseline=feet
            self.report['idle_reference']=dict(capsule_cm=row['position'],feet_relative_cm=feet,
                hands_relative_to_weapon_cm=local,hand_relative_rotations=rotation)
            if 'clip_sampling' not in self.report:
                records=[]
                library=getattr(u,'AnimationLibrary',None) or getattr(u,'AnimationBlueprintLibrary',None)
                profile=prop(self.hero,'animation_profile')
                for variant,field in ((5,'katana_diagonal_right'),(6,'katana_diagonal_left'),
                                      (7,'katana_horizontal'),(8,'katana_charged'),(9,'katana_overhead')):
                    clip=prop(profile,field)
                    record=dict(variant=variant,asset=clip.get_path_name(),seconds=float(clip.get_play_length()))
                    try:
                        keys=library.get_num_keys(clip)
                        if isinstance(keys,(tuple,list)):
                            keys=keys[0]
                        rate=(int(keys)-1)/record['seconds']
                        record.update(sampled_keys=int(keys),computed_sampled_key_rate_hz=rate)
                        self.clip_key_rates[variant]=rate
                    except Exception as error:
                        record['sampling_api_limitation']=str(error)
                    records.append(record)
                self.report['clip_sampling']=records
            if 'rig_parent_chains' not in self.report:
                mesh=prop(self.hero,'mesh')
                chains={}
                for start in ('hand_r','hand_l','foot_r','foot_l'):
                    chain=[start]
                    for _ in range(20):
                        parent=str(mesh.get_parent_bone(chain[-1]))
                        if parent in ('None','',chain[-1]):
                            break
                        chain.append(parent)
                    chains[start]=chain
                self.report['rig_parent_chains']=chains
        row['tool_relative_hand_bones_cm']=local
        if row['attack'] and FLOOR_DIAGNOSTICS:
            for endpoint,position in row['visible_blade'].items():
                floor=self.floor(u.Vector(*position))
                self.report['blade_floor_samples'].append(dict(phase=self.phase,time=self.now(),attack=row['attack'],
                    endpoint=endpoint,world_cm=position,floor_world_cm=xyz(floor) if floor is not None else None,
                    clearance_cm=position[2]-floor.z if floor is not None else None))
        if row['attack'] and self.grip_baseline:
            self.report['grip_samples'].append(dict(phase=self.phase,time=self.now(),attack=row['attack'],
                local=local,relative_rotation=rotation,
                drift_cm={side:math.dist(local[side],self.grip_baseline[side]) for side in ('r','l')},
                rotation_drift_deg={side:math.degrees(2*math.acos(min(1.,abs(sum(
                    a*b for a,b in zip(rotation[side],self.grip_rotation_baseline[side])))))) for side in ('r','l')},
                feet_relative_to_capsule_cm=feet,
                foot_drift_cm={side:math.dist(feet[side],self.feet_baseline[side]) for side in ('r','l')},
                capsule_cm=row['position'],velocity_cm_s=row['velocity']))
        if row['attack']:
            def value(result,default=None):
                if isinstance(result,(tuple,list)):
                    return result[0] if result[1] else default
                return default if result is None else result
            systems=[]
            for component in u.ObjectIterator(u.NiagaraComponent):
                if not component.is_active() or not component.get_path_name().startswith(self.world.get_path_name()):
                    continue
                asset=component.get_asset()
                if not asset or 'NS_KatanaSwingTrail' not in asset.get_path_name():
                    continue
                step=value(component.get_variable_int('User.Native.SwingStep'),-1)
                if step!=row['attack']:
                    continue
                native_age=value(component.get_variable_float('User.Native.SwingVisualAge'))
                native_rate=value(component.get_variable_float('User.Native.SwingRate'))
                system=dict(component=component.get_path_name(),attack=int(step),
                    allowed=bool(value(component.get_variable_bool('User.Native.TrailRenderAllowed'),False)),
                    segment=int(value(component.get_variable_int('User.Native.BladeBreakSerial'),-1)),
                    native_visual_age_seconds=native_age,native_play_rate=native_rate)
                for parameter,key in (('BladeBase','base'),('BladeTip','tip')):
                    point=value(component.get_variable_position('User.Native.'+parameter))
                    if point is not None:
                        system[key+'_world_cm']=xyz(point)
                        system[key+'_vs_visible_blade_error_cm']=math.dist(xyz(point),row['visible_blade'][key])
                rate=self.clip_key_rates.get(row['attack'])
                if rate and native_age is not None and native_rate is not None:
                    # Integral authored-frame start trims become integral keys
                    # in the dense120Hz bake; the charged trim is zero.
                    fraction=(native_age*native_rate*rate)%1.
                    system['native_clock_baked_key_fraction']=fraction
                    row['near_half_baked_key_sample']=.35<=fraction<=.65
                    if self.report['grip_samples'] and self.report['grip_samples'][-1]['time']==self.now():
                        self.report['grip_samples'][-1].update(native_clock_baked_key_fraction=fraction,
                            near_half_baked_key_sample=row['near_half_baked_key_sample'])
                systems.append(system)
            row['katana_niagara']=systems
            if systems:
                self.report['niagara_samples'].append(dict(phase=self.phase,time=self.now(),systems=systems))
        return row

    def trial_sweeps(self):
        with LOGFILE.open('rb') as stream:
            stream.seek(self.case_log_offset)
            text=stream.read().decode('utf8',errors='replace')
        origin=self.hero.get_actor_location()
        yaw=math.radians(-self.hero.get_actor_rotation().yaw)
        rows=[]
        for m in SWEEP.finditer(text):
            point=[float(x) for x in m.groups()[5:8]]
            offset=[point[i]-xyz(origin)[i] for i in range(3)]
            x=offset[0]*math.cos(yaw)-offset[1]*math.sin(yaw)
            y=offset[0]*math.sin(yaw)+offset[1]*math.cos(yaw)
            rows.append(dict(attack=int(m[1]),stroke=int(m[2]),tip_world_cm=point,
                tip_yaw_deg=math.degrees(math.atan2(y,x)),tip_radius_cm=math.hypot(x,y),
                tip_height_above_root_cm=offset[2]))
        envelopes=[]
        for attack,stroke in sorted({(r['attack'],r['stroke']) for r in rows}):
            group=[r for r in rows if r['attack']==attack and r['stroke']==stroke]
            angles=[r['tip_yaw_deg'] for r in group]
            heights=[r['tip_height_above_root_cm'] for r in group]
            envelopes.append(dict(phase=self.phase,attack=attack,stroke=stroke,samples=len(group),
                yaw_min_deg=min(angles),yaw_max_deg=max(angles),yaw_span_deg=max(angles)-min(angles),
                radial_min_cm=min(r['tip_radius_cm'] for r in group),radial_max_cm=max(r['tip_radius_cm'] for r in group),
                tip_height_min_cm=min(heights),tip_height_max_cm=max(heights)))
        self.report['sweep_envelopes'].extend(envelopes)
        return envelopes

    def verify_trial(self):
        s=self.scenario
        contacts,cuts,state=self.finish_phase()
        periods=self.periods()
        ordinary=next((p['attack'] for p in periods if p['attack'] in SINGLES),None)
        if ordinary:
            self.seen.add(ordinary)
        accepted=8 if s['mode']=='charged' else ordinary
        self.check_case(self.phase+' accepted requested native input',accepted is not None and any(p['attack']==accepted for p in periods))
        originals={f['actor']:f for _,f in self.targets}
        behind={name for name,f in originals.items() if not f['front_target']}
        self.check_case(self.phase+' kept rear targets untouched',not any(c['target'] in behind for c in contacts)
            and all(abs(float(prop(t,'health'))-f['initial_health'])<.001 for t,f in self.targets if not f['front_target']))
        keys=[(c['attack'],c['stroke'],c['target']) for c in contacts]
        self.check_case(self.phase+' has at most one contact per actor per stroke',len(keys)==len(set(keys)))
        rows=[]
        strokes=(0,1,2) if accepted==8 else (0,)
        for stroke in strokes:
            hits={c['target'] for c in contacts if c['attack']==accepted and c['stroke']==stroke}
            for name,f in originals.items():
                rows.append(dict(scenario=s['name'],phase=self.phase,variant=accepted,stroke=stroke,
                    angle_deg=f['polar_angle_deg'],reach_cm=f['radial_reach_cm'],height_cm=f['centre_height_cm'],
                    nominal_size_cm=f['nominal_size_cm'],kind=f['kind'],front=f['front_target'],hit=name in hits,actor=name))
        self.report['coverage_matrix'].extend(rows)
        envelopes=self.trial_sweeps()
        if s['mode']=='horizontal' and ordinary==7:
            front=[r for r in rows if r['front']]
            self.check_case('Horizontal R150/r20 hits all five real front-angle bodies',bool(front) and all(r['hit'] for r in front))
        if s['mode']=='overhead' and ordinary==9:
            self.check_case(s['name']+' overhead cuts the centre food geometry',any(r['front'] and r['hit'] for r in rows))
        if s['kind']=='food' and s['mode'] in ('single','charged') and s['reach']<=150.:
            self.check_case(s['name']+' cuts actual food geometry',any(r['front'] and r['hit'] for r in rows))
        if s['mode']=='charged' and s['kind']=='enemy':
            self.check_case(s['name']+' traced all three charged strokes',{e['stroke'] for e in envelopes if e['attack']==8}=={0,1,2})
            if s['reach']<=150.:
                self.check_case(s['name']+' third vertical stroke hits the centre body',
                    any(r['variant']==8 and r['stroke']==2 and r['angle_deg']==0. and r['hit'] for r in rows))
            segments={n['segment'] for r in self.report['niagara_samples'] if r['phase']==self.phase
                for n in r['systems'] if n['attack']==8 and n['allowed']}
            self.check_case(s['name']+' exposes all three real charged Niagara segments',len(segments)>=3)
        # The envelope is measured on real active edge samples. Vertical cuts
        # deliberately do not inherit a horizontal-hemisphere expectation.
        for e in envelopes:
            wide=e['attack'] in (5,6,7) or e['attack']==8 and e['stroke'] in (0,1)
            if wide:
                self.check_case(self.phase+' %d/%d has a broad physical sweep'%(e['attack'],e['stroke']),
                    e['yaw_min_deg']<=-70 and e['yaw_max_deg']>=70 and e['yaw_span_deg']>=150)
        self.report['single_variants_by_reach'].setdefault(str(s['reach']),[])
        if s['mode']=='all_singles':
            self.report['single_variants_by_reach'][str(s['reach'])]=sorted(self.seen)
        done=(s['mode']=='all_singles' and self.seen==SINGLES
              or s['mode']=='horizontal' and ordinary==7
              or s['mode']=='overhead' and ordinary==9
              or s['mode'] not in ('all_singles','horizontal','overhead'))
        if done:
            self.next_scenario()
        elif self.trial_index>=MAX_TRIALS:
            self.check_case(s['name']+' reached required actual variants within observation budget',False)
            self.next_scenario()
        else:
            self.start_trial()

    def finish(self,error=None):
        if not self.done and self.report.get('grip_samples'):
            idle=[x for x in self.report['grip_samples'] if x['phase']!='wide_walk' and math.hypot(*x['velocity_cm_s'][:2])<1.]
            walking=[x for x in self.report['grip_samples'] if x['phase']=='wide_walk']
            self.report['grip_diagnostics']=dict(
                max_right_bone_drift_cm=max(x['drift_cm']['r'] for x in self.report['grip_samples']),
                max_left_bone_drift_cm=max(x['drift_cm']['l'] for x in self.report['grip_samples']),
                max_right_rotation_drift_deg=max(x['rotation_drift_deg']['r'] for x in self.report['grip_samples']),
                max_left_rotation_drift_deg=max(x['rotation_drift_deg']['l'] for x in self.report['grip_samples']),
                max_right_foot_drift_cm=max(x['foot_drift_cm']['r'] for x in self.report['grip_samples']),
                max_left_foot_drift_cm=max(x['foot_drift_cm']['l'] for x in self.report['grip_samples']),
                idle_foot_sample_count=len(idle),walking_attack_sample_count=len(walking),
                max_idle_foot_drift_cm={side:max((x['foot_drift_cm'][side] for x in idle),default=None) for side in ('r','l')},
                max_walking_foot_motion_cm={side:max((x['foot_drift_cm'][side] for x in walking),default=None) for side in ('r','l')},
                near_half_baked_key_ordinary_samples=sum(bool(x.get('near_half_baked_key_sample')) for x in self.report['samples']),
                interpretation='Bone/tool stability diagnostic only; evaluated visible glove contact still requires primary visual inspection.')
            floors=[r['clearance_cm'] for r in self.report['blade_floor_samples'] if r['clearance_cm'] is not None]
            self.report['measured_blade_endpoint_floor_clearance_min_cm']=min(floors) if floors else None
        super().finish(error)

    def tick(self,delta):
        super().tick(delta)
        if self.done or self.world is None:
            return
        try:
            if self.scenario:
                presses=[r for r in self.report['requests'] if r['phase']==self.phase and r['held']]
                if not presses:
                    return
                elapsed=self.now()-presses[0]['time']
                if self.scenario.get('delayed') and not self.delayed_spawned and elapsed>.30:
                    self.spawn_trial_targets()
                duration=1.6 if self.scenario['mode']=='charged' else .65
                if elapsed>duration and self.attack_id()==0 and not self.primary:
                    self.verify_trial()
            elif self.regression_stage=='queue' and self.now()-self.phase_at>1.7 and not self.attack_id() and not self.primary:
                ids=[p['attack'] for p in self.periods()]
                self.check_case('Wide clips preserve the three-press queue',len(ids)==3 and all(i in SINGLES for i in ids))
                self.check_case('Wide clips preserve nonrepeating ordinary choices',all(a!=b for a,b in zip(ids,ids[1:])))
                self.regression_stage='hold'
                super().begin_phase('wide_hold',((0.,True),(1.65,False)))
            elif self.regression_stage=='hold':
                presses=[r for r in self.report['requests'] if r['phase']==self.phase and r['held']]
                if presses and self.now()-presses[0]['time']>1.95 and not self.primary and not self.attack_id():
                    ids=[p['attack'] for p in self.periods()]
                    self.check_case('Wide clips preserve one automatic series per held press',len(ids)==2 and ids[0] in SINGLES and ids[1]==8)
                    self.regression_stage='walk'
                    super().begin_phase('wide_walk',((.12,True),(.145,False)),camera='side')
                    self.wide_review_camera('side')
            elif self.regression_stage=='walk':
                age=self.now()-self.phase_at
                if age<.75:
                    self.hero.add_movement_input(self.hero.get_actor_forward_vector(),.65,False)
                if age>1.1:
                    walking=[r for r in self.report['samples'] if r['phase']=='wide_walk']
                    self.check_case('Wide upper-body cut keeps ordinary locomotion active',
                        any(r['attack'] in SINGLES and math.hypot(*r['velocity'][:2])>10. for r in walking))
                    self.report['walk_foot_samples']=[dict(time=r['time'],capsule=r['position'],velocity=r['velocity'],
                        feet={side:r['pose']['foot_'+side] for side in ('r','l')}) for r in walking]
                    self.report['walking_foot_interpretation']='Actual visible-rig foot sockets are retained for stance/slide review; they are not replaced with zero targets or mistaken for an idle pivot.'
                    self.finish('\n'.join(self.case_failures) or None)
        except Exception:
            self.finish(traceback.format_exc())


if __name__=='__main__':
    old=getattr(builtins,'mc_katana_wide_arc_solo',None)
    assert old is None or old.done,'Wide-arc probe already owns PIE'
    probe=KatanaWideArcSolo()
    builtins.mc_katana_wide_arc_solo=probe
    try:
        probe.start()
    except Exception:
        probe.finish(traceback.format_exc())
        raise
