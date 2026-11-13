"""Focused katana input, animation selection, aim and food check in owned solo PIE.

Uses real F3 grant and SetPrimaryInputHeld press/release handlers. Output is a
fresh E: directory; leaves saved packages untouched and restores PIE settings.
Run only after native authoring compilation and with the Editor idle on L_Mouth.
Optional globals: MC_KATANA_FLOW_OUTPUT, MC_KATANA_FLOW_START_RECORDING,
MC_KATANA_FLOW_CAPTURE_HZ. Cancel: builtins.mc_katana_flow_solo.finish('cancelled').
"""
import builtins
import datetime
import json
import math
import re
import runpy
import time
import traceback
from pathlib import Path

import unreal as u


ROOT = Path(u.Paths.project_dir()).resolve()
RUN = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
OUTPUT = Path(globals().get('MC_KATANA_FLOW_OUTPUT',
    'E:/CodexWork/MessControl/Validation/KatanaFlow/' + RUN)).resolve()
SINGLES = {5, 6, 7, 9}
# This same40cm enemy body lies inside the intersection of all three measured
# blade arcs; the separate ordinary fixture still verifies contact at225cm.
SERIES_ENEMY = ('enemy',150.,0.,50.,40.)
TRACE_VERBOSITY = int(globals().get('MC_KATANA_FLOW_TRACE_VERBOSITY',1))
CAPTURE_HZ = float(globals().get('MC_KATANA_FLOW_CAPTURE_HZ', 0.))
RECHECK_FAILED = bool(globals().get('MC_KATANA_FLOW_RECHECK_FAILED',False))
RECORDER = globals().get('MC_KATANA_FLOW_START_RECORDING',
                       getattr(builtins, 'mc_katana_recorder_factory', None))
ns = runpy.run_path(str(ROOT/'Tools/Unreal/check_katana_solo.py'), init_globals={
    'MC_KATANA_SOLO_OUTPUT': OUTPUT,
    'MC_KATANA_SOLO_CAPTURE_HZ': CAPTURE_HZ,
    'MC_KATANA_SOLO_TRACE_VERBOSITY': TRACE_VERBOSITY,
})
KatanaSolo, prop, xyz, LOGFILE = (ns[key] for key in ('KatanaSolo', 'prop', 'xyz', 'LOGFILE'))
INTAKE = re.compile(r'MC_FOOD_AUTO_INTAKE\s+item=\S+\s+batch=(\d+)\s+units=(\d+)\s+.*?part=(\S+)\s+mass=([0-9.eE+-]+)')


class KatanaFlowSolo(KatanaSolo):
    def __init__(self):
        super().__init__()
        self.report.update(scope=__doc__, action_periods=[], batch_states=[], input_latency=[],
            phase_results={}, observed_singles=[],
            fixture_tuning={'food_size_cm': 100, 'food_center_height_cm':50, 'max_auto_consume_dimensions_cm': 40,
                'reason': 'Keep an ordinary split nonready so charged finishing has a measurable contrast.'})
        self.last_attack_key = None
        self.current_period = None
        self.events = []
        self.event_index = 0
        self.event_due_at = None
        self.next_tap = 0.
        self.pool_hold_at = None
        self.slate_capture_hz = CAPTURE_HZ
        self.muted_food=set()
        self.report['recheck_failed_cases_only']=RECHECK_FAILED

    def shoot(self, held):
        super().shoot(held)
        request = self.report['requests'][-1]
        request['attack_after_input'] = self.attack_id()
        request['yaw_after_input'] = float(self.hero.get_actor_rotation().yaw)
        if held and request['attack_after_input']:
            self.report['input_latency'].append(dict(phase=self.phase, press_time=self.now(),
                attack_visible_in_press_call=True, attack=request['attack_after_input']))

    def sample(self):
        attack = self.attack_id()
        trail=self.hero.get_component_by_class(u.MCWeaponTrailComponent)
        attack_key = (attack,int(prop(trail,'presented_swings'))) if attack else None
        if attack_key != self.last_attack_key:
            if self.current_period is not None:
                self.current_period.update(end=self.now(), duration=self.now()-self.current_period['start'])
            self.current_period = None
            if attack:
                self.current_period = dict(phase=self.phase, attack=attack, start=self.now())
                self.report['action_periods'].append(self.current_period)
                if attack in SINGLES:
                    self.report['observed_singles'].append(attack)
            self.last_attack_key = attack_key
        row = super().sample()
        row['yaw'] = float(self.hero.get_actor_rotation().yaw)
        row['camera_yaw']=float(self.hero.get_component_by_class(u.CameraComponent).get_world_rotation().yaw)
        row['trail']=dict(presented_swings=int(prop(trail,'presented_swings')),
            emitting=bool(prop(trail,'emitting')),active_samples=int(prop(trail,'active_samples')))
        if self.phase=='enemy_series' and attack==8:
            systems=[]
            for component in u.ObjectIterator(u.NiagaraComponent):
                if not component.get_path_name().startswith(self.world.get_path_name()):
                    continue
                asset=component.get_asset()
                if not asset or 'Katana' not in asset.get_path_name():
                    continue
                allowed=component.get_variable_bool('User.Native.TrailRenderAllowed')
                serial=component.get_variable_int('User.Native.BladeBreakSerial')
                if isinstance(allowed,(tuple,list)):
                    allowed=allowed[0] if allowed[1] else False
                if isinstance(serial,(tuple,list)):
                    serial=serial[0] if serial[1] else -1
                if serial is None:
                    serial=-1
                systems.append(dict(active=bool(component.is_active()),allowed=bool(allowed),serial=int(serial)))
            row['katana_niagara']=systems
        row['visible_blade'] = dict(
            base=xyz(self.tool().get_world_transform().transform_location(u.Vector(3.75, 0, 11))),
            tip=xyz(self.tool().get_world_transform().transform_location(u.Vector(-14.5, 0, 197.56))))
        return row

    def periods(self, phase=None):
        return [p for p in self.report['action_periods'] if p['phase'] == (phase or self.phase)]

    def batch_state(self):
        batches = {fixture['batch'] for _, fixture in self.targets if fixture['batch'] is not None}
        records = []
        for food in self.actors(u.MCFoodActor):
            if not u.SystemLibrary.is_valid(food) or prop(food, 'batch') not in batches:
                continue
            visual=prop(food,'cut_visual')
            if visual is None or visual.get_num_sections()<1:
                visual=prop(food,'visual')
            center,extent,_=u.SystemLibrary.get_component_bounds(visual)
            body_center,body_extent,_=u.SystemLibrary.get_component_bounds(prop(food,'body'))
            records.append(dict(actor=food.get_name(),batch=int(prop(food,'batch')),
                phase=str(prop(food,'phase')),hidden=bool(prop(food,'hidden')),
                ready=bool(food.fits_automatic_intake_portion()),
                size_cm=xyz(food.get_hand_portion_size()),mass=float(prop(food,'food_data').mass),
                center_cm=xyz(center),extent_cm=xyz(extent),
                query_body_center_cm=xyz(body_center),query_body_extent_cm=xyz(body_extent),
                distance_from_player_cm=float((center-self.hero.get_actor_location()).length())))
        return records

    def frame_review(self,side=False):
        # Fit the enlarged blade as well as the full character in review views.
        center=self.hero.get_actor_location()+self.hero.get_actor_forward_vector()*95+u.Vector(0,0,45)
        if side:
            location=center+self.hero.get_actor_forward_vector()*70-self.hero.get_actor_right_vector()*490
        else:
            location=center+self.hero.get_actor_forward_vector()*490-self.hero.get_actor_right_vector()*130
        self.frame_camera(location+u.Vector(0,0,105),center)
        self.camera.get_component_by_class(u.CameraComponent).set_editor_property('field_of_view',75.)

    def begin_phase(self, phase, events=(), targets=(), camera=None):
        self.clear_targets()
        self.move_to(self.origin)
        self.pc.set_control_rotation(u.Rotator(yaw=0.))
        self.pc.set_view_target_with_blend(self.hero, 0.)
        self.events, self.event_index = list(events), 0
        self.event_due_at=None
        self.case_index += 1
        self.enter(phase)
        self.case_log_offset = LOGFILE.stat().st_size
        for fixture in targets:
            self.spawn_target(*fixture)
        if camera:
            self.frame_review(side=camera == 'side')
        self.write()

    def spawn_target(self, *args):
        super().spawn_target(*args)
        target, fixture = self.targets[-1]
        if fixture['kind'] == 'food' and self.phase in ('normal_food', 'charged_food', 'large_remainder', 'food_front_review'):
            settings = prop(target, 'settings')
            settings.set_editor_property('max_auto_consume_dimensions',u.Vector(40,40,40))
            target.set_editor_property('Settings', settings)
        if fixture['kind'] == 'food':
            fixture['auto_intake_initially_ready'] = bool(target.fits_automatic_intake_portion())
            fixture['initial_mass'] = float(prop(target,'food_data').mass)

    def run_events(self, age):
        if not self.events or self.event_index>=len(self.events):
            return
        if self.event_due_at is None:
            if age<self.events[0][0]:
                return
            self.event_due_at=self.now()
        while self.event_index < len(self.events) and self.now()>=self.event_due_at:
            planned, active = self.events[self.event_index]
            self.shoot(active)
            if self.phase=='aim_manual_override' and active:
                sensitivity=max(.02,min(1.,float(prop(self.hero,'camera_orbit_sensitivity'))))
                delta=30./sensitivity
                self.report['manual_camera_override']=dict(input_delta_x=delta,
                    camera_yaw_before=float(self.hero.get_component_by_class(u.CameraComponent).get_world_rotation().yaw),
                    body_yaw_before=float(self.hero.get_actor_rotation().yaw))
                self.hero.apply_camera_orbit_input(u.Vector2D(delta,0.))
            self.event_index += 1
            if self.event_index<len(self.events):
                # Preserve actual hold/gap durations even when fixture authoring
                # or a rendered frame delays the first input after phase entry.
                self.event_due_at=self.now()+self.events[self.event_index][0]-planned

    def clear_unrelated_food_view(self):
        batches={f['batch'] for _,f in self.targets if f['batch'] is not None}
        for food in self.actors(u.MCFoodActor):
            if prop(food,'batch') in batches or food.get_path_name() in self.muted_food:
                continue
            prop(food,'body').set_simulate_physics(False)
            food.set_actor_hidden_in_game(True)
            food.set_actor_enable_collision(False)
            food.set_actor_tick_enabled(False)
            self.muted_food.add(food.get_path_name())
        self.report['unrelated_food_hidden_in_owned_pie']=sorted(self.muted_food)

    def finish_phase(self):
        contacts, cuts = self.read_case_log()
        batches={f['batch'] for _,f in self.targets if f['batch'] is not None}
        with LOGFILE.open('rb') as stream:
            stream.seek(self.case_log_offset)
            lines=stream.read().decode('utf-8',errors='replace').splitlines()
        # Charged strokes can hit portions created by their immediate ordinary
        # prestrike. The staged batch records identify those new source actors.
        sources={cut['target'] for cut in cuts}
        known={c['raw'] for c in contacts}
        for line in lines:
            match=ns['CONTACT'].search(line)
            if not match or match[3] not in sources or line in known:
                continue
            contact=dict(case=self.phase,attack=int(match[1]),stroke=int(match[2]),target=match[3],
                point=[float(v) for v in match.groups()[3:6]],normal=[float(v) for v in match.groups()[6:9]],raw=line)
            for field,pattern in (('base',ns['CONTACT_BASE']),('tip',ns['CONTACT_TIP']),('direction',ns['CONTACT_DIRECTION'])):
                if endpoint:=pattern.search(line):
                    contact[field]=[float(v) for v in endpoint.groups()]
            contacts.append(contact)
        self.report['contacts'].extend(contacts)
        self.report['cut_results'].extend(cuts)
        state = self.batch_state()
        self.report['batch_states'].append(dict(phase=self.phase,time=self.now(),actors=state))
        self.report['phase_results'][self.phase] = dict(contacts=len(contacts),splits=cuts,
            attacks=[p['attack'] for p in self.periods()],remaining_food=state)
        receipts=[dict(batch=int(m[1]),units=int(m[2]),actor=m[3],mass=float(m[4]))
                  for line in lines if (m:=INTAKE.search(line)) and int(m[1]) in batches]
        self.report['phase_results'][self.phase]['intake_receipts']=receipts
        return contacts, cuts, state

    def spawn_saved_grapes(self):
        """Reuse the saved menu row and outside-bunch approach from the prior review."""
        table=u.load_asset('/Game/Data/DT_BreakfastMenu')
        exported=u.DataTableFunctionLibrary.export_data_table_to_json_string(table)
        if isinstance(exported,(tuple,list)):
            exported=next(v for v in exported if isinstance(v,str))
        row=next(dict(r) for r in json.loads(exported) if any('Grapes' in p for p in r.get('WholeMeshes',())))
        name=row.pop('Name')
        path=next(p for p in row['WholeMeshes'] if 'Grapes' in p)
        mesh=u.load_asset(path.split("'")[-2] if "'" in path else path)
        self.require(mesh is not None and prop(mesh,'allow_cpu_access'),'Saved grape mesh supports cutting')
        scale=u.Vector(*(float(row['Scale'][axis]) for axis in 'XYZ'))
        extent=mesh.get_bounds().box_extent*scale
        floor=self.floor(self.origin+u.Vector(extent.x+170,0,0))
        self.require(floor is not None,'Saved grape bunch has floor support')
        supports=[self.floor(floor+u.Vector(x,y,0)) for x,y in ((extent.x,0),(-extent.x,0),(0,extent.y),(0,-extent.y))]
        self.require(all(p is not None for p in supports),'Saved-scale grape footprint stays on tongue')
        elevation=max([floor.z]+[p.z for p in supports])+extent.z+8.
        pose=u.Transform(location=u.Vector(floor.x,floor.y,elevation))
        target=self.statics.call_method('BeginDeferredActorSpawnFromClass',args=(self.world,
            u.MCFoodActor.static_class(),pose,u.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN,
            None,u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        self.require(target is not None,'Saved grape fixture allocated')
        self.require(u.ToolsetLibrary.set_object_properties(target,json.dumps({'FoodData':row})),'Copy saved grape menu row')
        batch=672013+self.case_index
        target.set_editor_properties(dict(item_name=name,item_mesh=mesh,health=float(row['Health']),batch=batch))
        self.statics.call_method('FinishSpawningActor',args=(target,pose,u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        prop(target,'body').set_simulate_physics(False)
        target.set_actor_location(target.get_actor_location()+u.Vector(.01,0,0),False,True)
        center,bounds,_=u.SystemLibrary.get_component_bounds(prop(target,'visual'))
        target.set_actor_location(target.get_actor_location()+u.Vector(0,0,floor.z+2.-(center.z-bounds.z)),False,True)
        center,bounds,_=u.SystemLibrary.get_component_bounds(prop(target,'visual'))
        approaches=[]
        grip=prop(target,'grip_surface')
        for forward,yaw,distance in ((u.Vector(1,0,0),0.,bounds.x),(u.Vector(-1,0,0),180.,bounds.x),
                                     (u.Vector(0,1,0),90.,bounds.y),(u.Vector(0,-1,0),-90.,bounds.y)):
            for gap in (75.,100.,130.):
                foot=self.floor(center-forward*(distance+gap))
                if foot is None:
                    continue
                for height in (55.,75.,95.):
                    hit=grip.line_trace_component(foot+u.Vector(0,0,height)+forward*80.,
                        foot+u.Vector(0,0,height)+forward*280.,True,False,False)
                    if isinstance(hit,(tuple,list)) and hit and isinstance(hit[0],u.Vector):
                        approaches.append((abs(yaw),gap,foot,yaw,xyz(hit[0]),height))
        self.require(approaches,'Supported outside-bunch approach reaches real grape surface')
        _,gap,foot,yaw,impact,height=min(approaches,key=lambda r:(r[0],r[5],r[1]))
        self.move_to(foot,yaw)
        self.pc.set_control_rotation(u.Rotator(yaw=yaw))
        settings=prop(target,'settings')
        settings.set_editor_property('max_auto_consume_dimensions',u.Vector(40,40,40))
        target.set_editor_property('Settings',settings)
        fixture=dict(case=self.phase,kind='food',actor=target.get_name(),batch=batch,mesh=mesh.get_path_name(),
            center=xyz(center),extent=xyz(bounds),initial_health=float(prop(target,'health')),
            last_health=float(prop(target,'health')),initial_mass=float(prop(target,'food_data').mass),
            saved_row=name,saved_scale=xyz(scale),approach_gap_cm=gap,queried_surface_cm=impact,
            queried_height_cm=height,auto_consume_limit_cm=40)
        self.targets.append((target,fixture))
        self.report['fixtures'].append(fixture)
        self.report['fixture_tuning']['grapes']='Saved geometry/menu scale,40cm intake budget to keep remote islands measurable.'

    def begin_queue(self):
        self.begin_phase('queued_singles', ((0.,True),(.025,False),(.09,True),(.115,False),(.18,True),(.205,False)))

    def finish_queue(self):
        actions = self.periods()
        self.check_case('Three pressed clicks execute three singles after input has stopped',
            len(actions)==3 and all(p['attack'] in SINGLES for p in actions))
        self.check_case('Queued singles alternate variants',all(a['attack']!=b['attack'] for a,b in zip(actions,actions[1:])))
        self.check_case('Ordinary recovery is below 0.55 seconds at base rate',
            bool(actions) and all(p.get('duration',99.) < .55 for p in actions))
        presses = [r for r in self.report['requests'] if r['phase']==self.phase and r['held']]
        self.check_case('Idle tap starts in its press handler without waiting for release',
            bool(presses) and presses[0]['attack_after_input'] in SINGLES)
        self.finish_phase()
        self.begin_phase('random_pool')
        self.next_tap = self.now()+.05

    def run_pool(self, age):
        if self.pool_hold_at is not None and self.now()-self.pool_hold_at >= .025:
            self.shoot(False)
            self.pool_hold_at = None
            self.next_tap = self.now()+.05
        elif not self.primary and not self.attack_id() and self.now()>=self.next_tap:
            actions = self.periods()
            ids = [p['attack'] for p in actions]
            if len(ids)>=12 and set(ids)==SINGLES or len(ids)>=32:
                self.check_case('Random pool includes both diagonals, horizontal and overhead',set(ids)==SINGLES)
                self.check_case('Adjacent ordinary attacks never repeat',all(a!=b for a,b in zip(ids,ids[1:])))
                self.check_case('Overhead attack drives the actual swing trail',any(
                    r['phase']==self.phase and r['attack']==9 and r['trail']['emitting']
                    for r in self.report['samples']))
                self.finish_phase()
                self.begin_phase('held_charge',((0.,True),(1.65,False)))
            else:
                self.shoot(True)
                self.pool_hold_at = self.now()
        self.require(age < 25.,'Random-pool sample exceeded bounded observation window')

    def tick(self, _delta):
        if self.done or self.busy:
            return
        self.busy = True
        try:
            self.require(time.monotonic()-self.started < 180.,'Flow probe reached wall-time limit')
            worlds = list(u.EditorLevelLibrary.get_pie_worlds(True))
            if self.world is None:
                if not worlds:
                    return
                self.require(len(worlds)==1 and u.SystemLibrary.is_server(worlds[0]),'One authoritative solo PIE')
                pc=u.GameplayStatics.get_player_controller(worlds[0],0)
                if pc is None or pc.get_controlled_pawn() is None:
                    return
                self.world,self.pc,self.hero=worlds[0],pc,pc.get_controlled_pawn()
                self.require(isinstance(self.hero,u.MCToothCharacter),'Gameplay tooth pawn')
                self.inventory=self.hero.get_component_by_class(u.MCInventoryComponent)
                self.tongue,=self.actors(u.MCTongue)
                self.engine.set_editor_property('bUseFixedFrameRate',False)
                self.enter('readiness')
                return
            self.require(self.owned_pie_is_active(worlds),'Owned standalone PIE stays active')
            now,age=self.now(),self.now()-self.phase_at
            if self.pc.is_reward_menu_open():
                self.pc.choose_reward_perk(0)
            if self.phase=='readiness':
                if age>1. and self.ready():
                    self.enter('arrival')
                return
            if self.phase=='arrival':
                if age<1. or not self.ready():
                    return
                self.origin=self.floor(self.hero.get_actor_location())
                self.require(self.origin is not None,'Actual tongue support')
                self.move_to(self.origin)
                self.clear_unrelated_food_view()
                perks=prop(prop(self.hero,'player_state'),'perks')
                passive_ids=list(prop(perks,'passive_perk_ids'))
                self.report['fixture_removed_passives']=[str(perk) for perk in passive_ids]
                for perk in passive_ids:
                    self.require(perks.remove_perk(perk),'Could not isolate damage fixture from passive '+str(perk))
                self.report['fixture_tuning']['damage']='No passive perks; removed only in this owned transient PIE.'
                self.pc.toggle_dev_panel()
                try:
                    self.pc.request_dev_action(u.MCDevAction.GRANT_TOOL_BOOSTER,2)
                finally:
                    self.pc.toggle_dev_panel()
                self.inventory.server_select(u.MCToolSlot.KNIFE)
                self.trace_debug=u.SystemLibrary.get_console_variable_int_value('mc.Katana.TraceDebug')
                self.console('mc.Katana.TraceDebug '+str(TRACE_VERBOSITY))
                if RECORDER:
                    self.window_recording=RECORDER(self.output)
                elif not self.slate_capture_hz:
                    self.slate_capture_hz=15.
                self.enter('equip')
                return
            self.sample()
            self.clear_unrelated_food_view()
            if self.slate_capture_hz>0 and now>=self.capture_at:
                self.capture_at=now+1./self.slate_capture_hz
                self.screenshot('%05d.png'%len(self.report['frames']),sequence=True)
            self.run_events(age)
            if self.phase=='equip' and age>.5:
                self.check_case('F3 grant equips visible katana',self.hero.is_katana_equipped() and self.tool().is_visible())
                if RECHECK_FAILED:
                    self.begin_phase('held_charge',((0.,True),(1.65,False)))
                else:
                    self.begin_phase('moving_single',((.12,True),(.145,False)))
            elif self.phase=='moving_single':
                if age<.65:
                    self.hero.add_movement_input(self.hero.get_actor_forward_vector(),.65,False)
                if age>.9:
                    during=[r for r in self.report['samples'] if r['phase']==self.phase and r['attack'] in SINGLES]
                    self.check_case('Locomotion continues through a fast ordinary cut',bool(during) and any(math.hypot(*r['velocity'][:2])>10. for r in during))
                    self.finish_phase()
                    self.begin_queue()
            elif self.phase=='queued_singles' and age>1.7:
                self.finish_queue()
            elif self.phase=='random_pool':
                self.run_pool(age)
            elif self.phase=='held_charge' and age>1.95:
                actions=self.periods()
                ids=[p['attack'] for p in actions]
                self.check_case('Held press makes one instant single then exactly one automatic charged series',
                    len(ids)==2 and ids[0] in SINGLES and ids[1]==8)
                release=next(r['time'] for r in self.report['requests'] if r['phase']==self.phase and not r['held'])
                charged=next((p for p in actions if p['attack']==8),None)
                self.check_case('Charged series starts before release and long holding does not repeat it',
                    charged is not None and charged['start']<release and self.attack_id()==0)
                self.finish_phase()
                self.begin_phase('queued_charges',((0.,True),(.45,False),(.51,True),(.93,False)))
            elif self.phase=='queued_charges' and age>2.85:
                ids=[p['attack'] for p in self.periods()]
                self.check_case('Press and automatic charge queued during recovery retain FIFO order',
                    len(ids)==4 and ids[0] in SINGLES and ids[1]==8 and ids[2] in SINGLES and ids[3]==8)
                self.finish_phase()
                self.begin_phase('normal_food',((0.,True),(.025,False)),(('food',130.,0.,50.,100.),))
            elif self.phase=='normal_food' and age>1.:
                contacts,cuts,state=self.finish_phase()
                self.check_case('Ordinary blade makes a real contact-plane split',bool(contacts) and bool(cuts))
                self.check_case('Ordinary split leaves a nonready physical remainder in contrast fixture',
                    any(not row['hidden'] and not row['ready'] and 'DISPOSED' not in row['phase'].upper() for row in state))
                self.begin_phase('charged_food',((0.,True),(.45,False)),(('food',130.,0.,50.,100.),))
            elif self.phase=='charged_food' and age>1.8:
                contacts,cuts,state=self.finish_phase()
                self.check_case('Charged blade contacts real food geometry',bool(contacts))
                self.check_case('Charged food finishing readies every prepared part of an accepted split',
                    any(cut['parts']>=2 and cut['parts']==cut['ready'] for cut in cuts))
                self.check_case('Charged series leaves no physical nonready remainder',
                    not any(not row['hidden'] and not row['ready'] and 'DISPOSED' not in row['phase'].upper() for row in state))
                receipts=self.report['phase_results'][self.phase]['intake_receipts']
                mass=math.fsum(r['mass'] for r in receipts)
                initial=math.fsum(f['initial_mass'] for _,f in self.targets)
                self.check_case('Charged finishing accounts for the full food mass exactly once',
                    bool(receipts) and abs(mass-initial)<max(.00001,initial*.0001)
                    and len({r['actor'] for r in receipts})==len(receipts))
                self.begin_phase('large_remainder',((0.,True),(.45,False)),(('food',260.,0.,170.,320.),))
            elif self.phase=='large_remainder' and age>1.8:
                contacts,cuts,state=self.finish_phase()
                remaining=[r for r in state if not r['hidden'] and 'DISPOSED' not in r['phase'].upper()]
                self.check_case('Charged series makes actual contact with oversized ingredient',
                    any(c['attack']==8 for c in contacts))
                self.check_case('Contacted oversized ingredient retains its nonready physical remainder',
                    any(not r['ready'] and max(r['size_cm'])>220. for r in remaining))
                receipts=self.report['phase_results'][self.phase]['intake_receipts']
                accounted=math.fsum(r['mass'] for r in receipts)+math.fsum(r['mass'] for r in remaining)
                initial=math.fsum(f['initial_mass'] for _,f in self.targets)
                self.check_case('Out-of-footprint food mass is retained without loss or duplicate awards',
                    abs(accounted-initial)<max(.00001,initial*.0001))
                self.begin_phase('saved_grapes',((0.,True),(.65,False)))
                self.spawn_saved_grapes()
                self.report['grape_remote_islands_before_charge']=None
            elif self.phase=='saved_grapes':
                if age>.43 and self.report['grape_remote_islands_before_charge'] is None:
                    self.report['grape_remote_islands_before_charge']=[r for r in self.batch_state()
                        if not r['hidden'] and not r['ready'] and r['distance_from_player_cm']>400.]
                if age>2.:
                    contacts,cuts,state=self.finish_phase()
                    self.check_case('Saved-scale grape bunch receives actual blade cuts',bool(contacts) and bool(cuts))
                    self.report['grape_contact_distribution']=dict(
                        ordinary=sum(c['attack'] in SINGLES for c in contacts),
                        charged=sum(c['attack']==8 for c in contacts),
                        interpretation='The random ordinary prestrike can clear or displace the nearby fruit; later charged strokes hit only surviving geometry they actually intersect.')
                    remote=self.report['grape_remote_islands_before_charge'] or []
                    current={r['actor']:r for r in state}
                    self.check_case('Uncontacted distant grape islands retain actor identity and mass',bool(remote) and all(
                        r['actor'] in current and abs(current[r['actor']]['mass']-r['mass'])<.00001 for r in remote))
                    remaining=[r for r in state if not r['hidden'] and 'DISPOSED' not in r['phase'].upper()]
                    receipts=self.report['phase_results'][self.phase]['intake_receipts']
                    accounted=math.fsum(r['mass'] for r in receipts)+math.fsum(r['mass'] for r in remaining)
                    initial=math.fsum(f['initial_mass'] for _,f in self.targets)
                    self.check_case('Saved-scale grapes retain or deliver every mass unit once',
                        abs(accounted-initial)<max(.00001,initial*.0001))
                    self.begin_phase('outside_sweep',((0.,True),(.45,False)),
                        (('food',390.,-35.,90.,36.),('enemy',390.,35.,90.,25.)))
            elif self.phase=='outside_sweep' and age>1.8:
                contacts,cuts,state=self.finish_phase()
                self.check_case('Charged attack leaves targets outside blade reach untouched',not contacts and not cuts and all(
                    u.SystemLibrary.is_valid(t) and abs(float(prop(t,'health'))-f['initial_health'])<.001 for t,f in self.targets))
                self.begin_phase('aim_food',((.05,True),(.075,False)),(('food',210.,65.,85.,70.),))
            elif self.phase=='aim_food' and age>1.:
                samples=[r for r in self.report['samples'] if r['phase']==self.phase]
                yaw=max((float(r['yaw']) for r in samples),default=0.)
                self.report['aim_assist']=dict(max_yaw_deg=yaw,initial_yaw_deg=0.,target_lateral_cm=65.)
                self.check_case('Soft assist rotates toward nearby off-axis food',yaw>1.)
                self.check_case('Soft assist stays bounded without a hard snap',yaw<26.)
                self.finish_phase()
                self.begin_phase('aim_manual_override',((.05,True),(.085,False)),(('food',210.,65.,85.,70.),))
            elif self.phase=='aim_manual_override' and age>1.:
                record=self.report['manual_camera_override']
                samples=[r for r in self.report['samples'] if r['phase']==self.phase]
                yaw=max((abs(r['yaw']) for r in samples),default=0.)
                camera=float(self.hero.get_component_by_class(u.CameraComponent).get_world_rotation().yaw)
                camera_delta=(camera-record['camera_yaw_before']+180.)%360.-180.
                record.update(max_body_yaw_deg=yaw,camera_yaw_after=camera,camera_delta_deg=camera_delta)
                self.check_case('Real manual camera orbit cancels the automatic body correction',abs(camera_delta)>6. and yaw<1.5)
                self.hero.apply_camera_orbit_input(u.Vector2D(-record['input_delta_x'],0.))
                self.finish_phase()
                self.begin_phase('enemy_single',((0.,True),(.025,False)),(('enemy',225.,0.,50.,45.),))
            elif self.phase=='enemy_single' and age>1.:
                contacts,cuts,state=self.finish_phase()
                self.check_case('Fast ordinary attack damages a real enemy body',bool(contacts) and any(
                    h['phase']==self.phase for h in self.report['target_hits']))
                hits=[h for h in self.report['target_hits'] if h['phase']==self.phase]
                self.report['ordinary_enemy_damage']=[h['before']-h['after'] for h in hits]
                presses=[r for r in self.report['requests'] if r['phase']==self.phase and r['held']]
                if hits and presses:
                    delay=hits[0]['time']-presses[0]['time']
                    self.report['first_enemy_contact_after_press_seconds']=delay
                    self.check_case('Fast single first contact occurs within 0.22 seconds of press',delay<.22)
                self.begin_phase('enemy_series',((0.,True),(.45,False)),(SERIES_ENEMY,))
            elif self.phase=='enemy_series' and age>1.8:
                contacts,cuts,state=self.finish_phase()
                self.check_case('Charged attack retains three distinct enemy strokes',{c['stroke'] for c in contacts if c['attack']==8}=={0,1,2})
                damage=[h['before']-h['after'] for h in self.report['target_hits'] if h['phase']==self.phase and h['attack']==8]
                ordinary=self.report.get('ordinary_enemy_damage',[])
                self.report['charged_enemy_damage_per_stroke']=damage
                self.check_case('Each charged stroke damages enemy HP more than an ordinary cut',
                    len(damage)==3 and bool(ordinary) and all(d>max(ordinary)+.001 for d in damage))
                serials={system['serial'] for r in self.report['samples'] if r['phase']==self.phase
                    for system in r.get('katana_niagara',()) if system['active'] and system['allowed']}
                self.report['charged_niagara_segments']=sorted(serials)
                self.check_case('Charged Niagara trail retains three separate blade segments',len(serials)>=3)
                self.begin_phase('food_front_review',((0.,True),(.45,False)),(('food',130.,0.,50.,100.),),'front')
            elif self.phase=='food_front_review' and age>1.8:
                self.finish_phase()
                self.begin_phase('enemy_side_review',((0.,True),(.45,False)),(SERIES_ENEMY,),'side')
            elif self.phase=='enemy_side_review' and age>1.8:
                self.finish_phase()
                if RECHECK_FAILED:
                    self.finish('\n'.join(self.case_failures) or None)
                else:
                    self.begin_phase('interrupt',((0.,True),(.025,False),(.09,True),(.115,False),(.18,True),(.205,False)))
            elif self.phase=='interrupt' and age>.23:
                self.inventory.server_select(u.MCToolSlot.BRUSH)
                self.enter('interrupt_brush')
            elif self.phase=='interrupt_brush' and age>.6:
                self.check_case('Brush replacement clears active katana ownership',not self.hero.is_katana_equipped() and self.attack_id()==0)
                self.inventory.server_select(u.MCToolSlot.KNIFE)
                self.enter('interrupt_return')
            elif self.phase=='interrupt_return' and age>1.3:
                self.check_case('Returning to katana does not replay cancelled queued clicks',not self.periods() and self.attack_id()==0)
                self.check_case('Effort face remains active during attacks',any(r['attack'] and r['mouth_effort']>.05 for r in self.report['samples']))
                self.finish('\n'.join(self.case_failures) or None)
        except Exception:
            self.finish(traceback.format_exc())
        finally:
            self.busy=False


if __name__=='__main__':
    old=getattr(builtins,'mc_katana_flow_solo',None)
    assert old is None or old.done,'Flow probe already owns PIE'
    probe=KatanaFlowSolo()
    builtins.mc_katana_flow_solo=probe
    try:
        probe.start()
    except Exception:
        probe.finish(traceback.format_exc())
        raise
