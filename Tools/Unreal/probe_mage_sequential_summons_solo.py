"""Owned solo PIE: real F3 rain nuts -> native mage cast -> sequential minion pops.

The editor-only native mage fixture calls ConfigureEncounter, with its complete
saved health/entrance/AI. Other boss abilities are postponed before observation.
It adds a diagnostic Mage beside the still-living Knight, so two bosses can be
visible. This does not exercise the normal single-boss Knight -> Mage transition.
No food, health, phase, summon, fuse, explosion or clock is written by Python.
All remaining actual rain nuts are used, so group size follows the native rain.
Position-only observer dodging keeps the camera subject alive; this is not balance
or multiplayer evidence. Exact owned PIE SWindow capture includes the game HUD.
Cancel: builtins.mc_mage_sequential_summons_solo.finish('Stopped by operator').
"""
import builtins
import datetime
import hashlib
import json
import math
import runpy
import time
import traceback
import uuid
from pathlib import Path
import unreal as u

ROOT = Path(u.Paths.project_dir()).resolve()
OUTPUT = Path(globals().get('MC_KAMIKAZE_OUTPUT', ROOT/'Saved/Feedback5_11_20261018/Kamikaze')).resolve()
if OUTPUT.drive.upper() != 'E:':
    raise RuntimeError('Kamikaze evidence must remain on E:')
RUN = OUTPUT/'Runs'/(datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')+'_'+uuid.uuid4().hex[:8])
support = runpy.run_path(str(ROOT/'Tools/Unreal/probe_cataclysm_solo.py'),
    run_name='kamikaze_capture_support', init_globals={'MC_CATACLYSM_MODE':'jellies',
        'MC_CATACLYSM_OUTPUT':RUN, 'MC_CATACLYSM_CAPTURE_HZ':globals().get('MC_KAMIKAZE_CAPTURE_HZ',8)})
Parent, prop, vec, path, valid, enum_name = (support[k] for k in ('CataclysmSolo','prop','vec','path','valid','enum_name'))
KEY = 'mc_mage_sequential_summons_solo'
READINESS = ('NextMeleeAt','NextChargeAt','NextJumpAt','NextRollAt','NextFireballAt','NextRainAt','NextTeleportAt','NextSummonAt','NextSpecialAt')


class MageSequentialSummonsSolo(Parent):
    def __init__(self):
        super().__init__()
        self.output=RUN
        self.mage=None
        self.group=[]
        self.group_paths=set()
        self.nuts_before={}
        self.source_nuts=[]
        self.cast_seen=False
        self.born_at=None
        self.dodge_at=0.
        self.explosion_observations={}
        self.trace_at=0.
        self.report.update(mode='mage_sequential_summons', scope=__doc__, run_directory=str(RUN),
            clips={},frames=[],screenshots=[],samples=[],checks={},source_sha256={},fixture_tuning={},
            tuning_reason='A native full-health Mage readiness fixture follows the real F3 rainfall; no summon fuse or detonation is overwritten.',
            limitations=[
                'One owned standalone PIE, no client prediction/network test, package saves or performance claim.',
                'This checks one actual cast group; all remaining rain nuts are eligible, so it is not an artificial four-minion fixture.',
                'Position-only observer dodging and review camera are disclosed survival/visibility fixtures.',
                'A native editor-only Mage fixture bypasses the earlier Knight defeat, but native ConfigureEncounter/entrance/cast/pursuit/fuses/pops execute unchanged.',
                'Other boss cooldowns are postponed to isolate this one summon cast; no optional player kill is attempted.',
                'Root visual review of exact game-window frames remains necessary.'])
        for name in ('Tools/Unreal/probe_mage_sequential_summons_solo.py','Tools/Unreal/check_approved_environment_solo.py',
                     'Source/MessControl/Private/MCNutBoss.cpp','Source/MessControl/Private/MCNutKamikaze.cpp',
                     'Source/MessControlEditor/Private/MCGameplayUIEditorLibrary.cpp'):
            self.report['source_sha256'][name]=hashlib.sha256((ROOT/name).read_bytes()).hexdigest()
        self.require(callable(getattr(u.MCGameplayUIEditorLibrary,'spawn_pie_mage_for_summon',None)),
                     'Build the native editor-only Mage fixture before running this probe')

    def write(self):
        self.output.mkdir(parents=True,exist_ok=True)
        data=json.dumps(self.report,ensure_ascii=False,indent=2,allow_nan=False)
        (self.output/'Report.json').write_text(data,encoding='utf-8')
        OUTPUT.mkdir(parents=True,exist_ok=True)
        (OUTPUT/'Report.json').write_text(data,encoding='utf-8')

    def place_observer(self, foot, reason):
        pos=foot+u.Vector(0,0,self.hero.capsule_component.get_scaled_capsule_half_height()+3.)
        self.hero.set_actor_location(pos,False,True)
        self.report.setdefault('observer_placements',[]).append(dict(time=self.now(),point_cm=vec(pos),reason=reason))

    def now(self):
        return float(u.GameplayStatics.get_time_seconds(self.world))

    def postpone(self,boss):
        for key in READINESS:
            boss.set_editor_property(key,self.now()+300.)

    def dodge_observer(self,now):
        if now<self.dodge_at:
            return
        self.dodge_at=now+.15
        center,extent,_=u.SystemLibrary.get_component_bounds(prop(self.tongue,'surface'))
        hazards=[(a.get_actor_location(),float(prop(a,'radius'))+130.)
                 for a in self.actors(u.MCNutLandingShadow) if a.is_warning_active()]
        hazards += [(a.get_actor_location(),float(prop(a,'blast_radius'))+155.) for a in self.group
                    if valid(a) and prop(a,'armed') and a.get_fuse_seconds_remaining()<1.15]
        hero=self.hero.get_actor_location()
        current_margin=min((math.hypot(hero.x-p.x,hero.y-p.y)-radius for p,radius in hazards),default=10000.)
        if current_margin>80.:
            return
        choices=[]
        for x in (-.60,-.40,-.20,0.,.20,.40,.60):
            for y in (-.60,-.40,-.20,0.,.20,.40,.60):
                foot=self.floor(center+u.Vector(extent.x*x,extent.y*y,0))
                if foot is None:
                    continue
                edges=[self.floor(foot+offset) for offset in (u.Vector(40,0,0),u.Vector(-40,0,0),u.Vector(0,40,0),u.Vector(0,-40,0))]
                if any(e is None or abs(e.z-foot.z)>25. for e in edges):
                    continue
                margin=min((math.hypot(foot.x-p.x,foot.y-p.y)-radius for p,radius in hazards),default=10000.)
                if margin>100.:
                    choices.append((math.hypot(foot.x-hero.x,foot.y-hero.y),foot))
        self.require(choices,'No supported observer point clear of genuine landing/blast danger')
        self.place_observer(min(choices,key=lambda item:item[0])[1],'Position-only dodge of actual landing/blast radii; native health/damage untouched')

    def frame_group(self):
        if self.born_at is None:
            aim=self.mage.get_actor_location()+u.Vector(0,0,50)
        else:
            live=[a for a in self.group if valid(a) and prop(a,'armed')]
            next_actor=min(live,key=lambda a:float(prop(a,'fuse_explodes_at'))) if live else None
            point=next_actor.get_actor_location() if next_actor else self.hero.get_actor_location()
            aim=(point+self.hero.get_actor_location())*.5+u.Vector(0,0,25)
        center,extent,_=u.SystemLibrary.get_component_bounds(prop(self.tongue,'surface'))
        inward=u.Vector(center.x-aim.x,center.y-aim.y,0)
        distance=math.hypot(inward.x,inward.y)
        inward=inward/distance if distance>200. else u.Vector(0,-1,0)
        eye=aim+inward*850.+u.Vector(0,0,550.)
        # Keep the review camera over the central tongue side of its subject,
        # rather than following a minion through the cheek wall at an arena edge.
        eye.x=max(center.x-extent.x*.60,min(center.x+extent.x*.60,eye.x))
        eye.y=max(center.y-extent.y*.60,min(center.y+extent.y*.60,eye.y))
        self.frame_camera(eye,aim)
        prop(self.camera,'camera_component').set_editor_property('field_of_view',80.)

    def record_group(self,now):
        if now<self.trace_at:
            return
        self.trace_at=now+.05
        rows=[]
        for actor in self.group:
            if not valid(actor):
                continue
            exploded=bool(prop(actor,'exploded'))
            row=dict(path=path(actor),position_cm=vec(actor.get_actor_location()),target=path(prop(actor,'target')),
                health=float(prop(actor,'health')),armed=bool(prop(actor,'armed')),exploded=exploded,
                defeated=bool(prop(actor,'defeated')),fuse_started_at=float(prop(actor,'fuse_started_at')),
                fuse_explodes_at=float(prop(actor,'fuse_explodes_at')),remaining_seconds=float(actor.get_fuse_seconds_remaining()),
                flame_visible=bool(prop(actor,'fuse_flame').is_visible()),warning_visible=bool(prop(actor,'blast_warning').is_visible()))
            if exploded:
                # HitAt can mean a weapon impact before death. It is accepted as
                # the native actual detonation time only with bExploded=true.
                row['actual_detonated_at']=float(prop(actor,'hit_at'))
                self.explosion_observations.setdefault(path(actor),dict(observed_at=now,actual_at=row['actual_detonated_at']))
            rows.append(row)
        self.report['samples'].append(dict(time=now,phase=self.phase,mage_state=enum_name(prop(self.mage,'state')),
            mage_attack=enum_name(prop(self.mage,'attack')),hero_cm=vec(self.hero.get_actor_location()),
            hero_alive=bool(prop(self.hero,'status').is_alive()),minions=rows))

    def update(self,now):
        elapsed=now-self.phase_at
        if self.phase=='launch':
            events=[a for a in self.actors(u.MCNutRainEvent) if a.actor_has_tag('MC_DevKeyEvent')]
            self.require(len(events)==1,'F3 did not create exactly one genuine rain event')
            self.event=events[0]
            self.tongue=self.actors(u.MCTongue)[0]
            self.report['native_rain_event']=path(self.event)
            self.stage('natural_rain')
        elif self.phase=='natural_rain':
            self.require(elapsed<40. and not prop(self.event,'failed'),'Native rain did not reach the supported boss stage')
            self.dodge_observer(now)
            if enum_name(prop(self.event,'stage'))!='ENEMIES':
                return
            for boss in self.actors(u.MCNutBoss):
                self.postpone(boss)
            center,_,_=u.SystemLibrary.get_component_bounds(prop(self.tongue,'surface'))
            candidates=[center+u.Vector(x,y,0) for x,y in ((0,0),(-400,0),(400,0),(0,400),(0,-400))]
            for point in candidates:
                self.mage=u.MCGameplayUIEditorLibrary.spawn_pie_mage_for_summon(self.world,self.tongue,self.event,point,137)
                if valid(self.mage):
                    break
            self.require(valid(self.mage),'Native editor-only mage fixture could not find a supported footprint')
            self.postpone(self.mage)
            self.report['mage_fixture']=dict(actor=path(self.mage),api='UMCGameplayUIEditorLibrary::SpawnPIEMageForSummon -> ConfigureEncounter',
                health=float(prop(self.mage,'health')),saved_full_health=True,phase_or_fuse_written=False)
            self.stage('mage_entrance')
        elif self.phase=='mage_entrance':
            self.require(elapsed<10.,'Native mage entrance did not settle')
            self.dodge_observer(now)
            if enum_name(prop(self.mage,'state'))!='IDLE':
                return
            self.source_nuts=[a for a in prop(self.event,'nuts')
                if valid(a) and not prop(a,'fragment') and enum_name(prop(a,'phase'))!='DISPOSED']
            self.nuts_before={path(a):dict(phase=enum_name(prop(a,'phase')),point_cm=vec(a.get_actor_location()),
                native_rain_tag=bool(a.actor_has_tag('MCNutRain'))) for a in self.source_nuts}
            self.require(len(self.nuts_before)>=2,'Need at least two actual remaining rain nuts for sequential comparison')
            self.mage.set_editor_property('NextSummonAt',now)
            self.mage.set_editor_property('NextSpecialAt',now)
            self.report['ability_readiness_fixture']=dict(time=now,only_ready='Summon',other_abilities_postponed_seconds=300.)
            self.begin_clip('mage_native_cast_and_sequential_pops',60.)
            self.frame_group()
            self.stage('native_cast')
        elif self.phase=='native_cast':
            self.require(elapsed<10.,'Native mage scheduler did not release its real summon cast')
            self.frame_group()
            if enum_name(prop(self.mage,'state')) in ('TELEGRAPH','EXECUTING') and enum_name(prop(self.mage,'attack'))=='SUMMON':
                self.cast_seen=True
            spawned=[a for a in self.actors(u.MCNutKamikaze) if a.get_owner()==self.event]
            if not spawned:
                return
            self.require(self.cast_seen and len(spawned)>=2,'A genuine mage tell/release and minion group were not observed')
            self.group=spawned
            self.group_paths={path(a) for a in spawned}
            self.born_at=now
            self.postpone(self.mage)
            self.report['checks']['native_mage_cast_created_real_minions']=True
            self.report['native_group']=dict(observed_born_at=now,minions=sorted(self.group_paths),
                source_rain_nuts_before=self.nuts_before,
                source_nuts_disposed_at_release=[path(a) for a in self.source_nuts
                    if valid(a) and enum_name(prop(a,'phase'))=='DISPOSED'],manual_spawn_or_fuse_writes=False)
            self.require(all(row['native_rain_tag'] for row in self.nuts_before.values()),'The cast fixture includes a non-rain food actor')
            first_deadline=min(float(prop(a,'fuse_explodes_at')) for a in self.group)
            first_start=min(float(prop(a,'fuse_started_at')) for a in self.group)
            self.require(abs(first_deadline-first_start-5.)<.025,'The first native group fuse no longer lasts five seconds')
            self.report['native_group'].update(first_fuse_started_at=first_start,first_fuse_deadline=first_deadline)
            self.stage('native_pursuit_and_sequence')
        elif self.phase=='native_pursuit_and_sequence':
            self.require(now-self.born_at<50.,'The genuine summon group did not finish its sequential fuses')
            current={path(a) for a in self.actors(u.MCNutKamikaze) if a.get_owner()==self.event}
            self.require(current<=self.group_paths,'An unrelated second mage group entered this focused check')
            self.dodge_observer(now)
            self.frame_group()
            self.record_group(now)
            if len(self.explosion_observations)!=len(self.group):
                return
            ordered=sorted(self.explosion_observations.items(),key=lambda item:item[1]['actual_at'])
            times=[entry['actual_at'] for _,entry in ordered]
            gaps=[b-a for a,b in zip(times,times[1:])]
            self.require(all(gap>=.745 for gap in gaps),'Actual sequential detonations were simultaneous or less than native .75 s apart')
            self.require(any(row['target']==path(self.hero) for sample in self.report['samples'] for row in sample['minions']),
                         'Native summons never acquired the solo player as their pursuit target')
            self.report['detonations']=ordered
            self.report['actual_detonation_gaps_seconds']=gaps
            self.report['checks'].update(actual_detonations_separated_by_native_interval=True,native_nearest_pursuit_observed=True,
                all_group_members_popped_without_fuse_or_damage_edits=True)
            self.stage('pop_hold')
        elif self.phase=='pop_hold' and elapsed>=1.5:
            self.clip=None
            self.require(len(self.report['clips']['mage_native_cast_and_sequential_pops'])>=16,'Insufficient exact game-window evidence')
            self.finish()

    def tick(self,delta):
        if self.done:
            return
        try:
            self.require(time.monotonic()-self.started<300.,'Focused summon probe exceeded 300 wall seconds')
            worlds=list(u.EditorLevelLibrary.get_pie_worlds(True))
            if self.world is None:
                if not worlds:
                    return
                self.require(len(worlds)==1 and u.SystemLibrary.is_standalone(worlds[0]),'Expected one owned standalone PIE')
                pc=u.GameplayStatics.get_player_controller(worlds[0],0)
                if not valid(pc) or not valid(pc.get_controlled_pawn()):
                    return
                self.world,self.pc=worlds[0],pc
                self.stage('first_perk')
                return
            self.require(self.world in worlds and len(worlds)==1,'Owned PIE ended or was replaced')
            self.hero=self.pc.get_controlled_pawn()
            self.require(isinstance(self.hero,u.MCToothCharacter) and prop(self.hero,'status').is_alive(),'Native observer died or changed pawn')
            now=self.now()
            if self.pc.is_reward_menu_open():
                self.pc.choose_reward_perk(0)
                if self.phase=='first_perk':
                    self.stage('perk_resolution')
                return
            if self.last_game_time is not None and now<=self.last_game_time:
                return
            self.last_game_time=now
            if self.phase=='first_perk':
                self.require(now-self.phase_at<25.,'Initial native perk UI did not appear')
                return
            if self.phase=='perk_resolution':
                self.pc.request_dev_action(u.MCDevAction.NUT_ENCOUNTER,-1)
                self.stage('launch')
                return
            self.keep_camera()
            self.update(now)
            if self.done:
                return
            if self.capture_hz and self.clip and now>=self.frame_at:
                self.frame_at=now+1./self.capture_hz
                evidence=self.camera_evidence()
                if evidence and evidence['location_error_cm']<12. and evidence['rotation_error_degrees']<5.:
                    self.screenshot('%05d.png'%len(self.report['frames']),True)
        except Exception:
            self.finish(traceback.format_exc())


def main():
    old=getattr(builtins,KEY,None)
    if old is not None and not old.done:
        raise RuntimeError('A summon probe is already active')
    probe=MageSequentialSummonsSolo()
    setattr(builtins,KEY,probe)
    try:
        probe.start()
    except Exception:
        probe.finish(traceback.format_exc())
        raise


if __name__=='__main__':
    main()
