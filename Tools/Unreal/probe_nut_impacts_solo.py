"""ROOT-RUN ONLY: disclosed real-Chaos Nutageddon impact fixtures in owned solo PIE.

Uses the normal first-perk UI and actual F3 NutEncounter. Two naturally emitted
whole nuts are isolated after spawning. Their authored bodies, native tick,
OnComponentHit and tooth damage resolver remain active. The fixture teleports
them to verified clear poses and sets body velocity once per disclosed shot;
gravity is disabled on those two bodies to isolate horizontal kinetic energy.
No direct damage, HitFood, OnHit, tooth-health or native-counter writes.
The strong shot is followed by a second real contact inside its native cooldown.
Actual component delegates supply contact evidence, not proximity alone.
Other storm actors/schedulers are temporarily frozen/hidden and restored before
ending only the owned PIE. No package saves, builds or network tests.
Cancel: builtins.mc_nut_impacts_solo.finish('Stopped by operator').
Non-main import is inert. Optional MC_NUT_IMPACTS_OUTPUT and capture HZ (0..6).
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
OUTPUT = Path(globals().get('MC_NUT_IMPACTS_OUTPUT', ROOT/'Saved/Checks/NutImpactsSolo')).resolve()
if ROOT.drive.upper() != 'E:' or OUTPUT.drive.upper() != 'E:':
    raise RuntimeError('All project/probe output paths must be on E:')
RUN = OUTPUT/'Runs'/(datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')+'_'+uuid.uuid4().hex[:8])
KEY = 'mc_nut_impacts_solo'
support = runpy.run_path(str(ROOT/'Tools/Unreal/probe_nut_pattern_kinetics_solo.py'),
    run_name='nut_impact_support', init_globals={'MC_NUT_PATTERN_OUTPUT': RUN})
Base, Camera, prop, vec, valid = (support[k] for k in ('Base', 'Camera', 'prop', 'vec', 'valid'))
HIT_FIELDS = ('blocking_hit', 'initial_overlap', 'time', 'distance', 'location', 'impact_point',
              'normal', 'impact_normal', 'phys_mat', 'actor', 'component', 'hit_bone_name',
              'bone_name', 'item', 'element_index', 'face_index', 'trace_start', 'trace_end')


def path(obj):
    return obj.get_path_name() if valid(obj) else None


def length(vector):
    return math.sqrt(vector.x**2+vector.y**2+vector.z**2)


def normalized(vector):
    size = length(vector)
    if size < 1.e-6:
        raise RuntimeError('Degenerate fixture direction')
    return vector/size


class NutImpactsSolo(Base):
    keep_review_camera = Camera.keep_review_camera
    camera_evidence = Camera.camera_evidence

    def __init__(self):
        super().__init__()
        self.output = RUN
        self.capture_hz = max(0., min(6., float(globals().get('MC_NUT_IMPACTS_HZ', 6.))))
        self.event = self.nut = self.other = self.tooth = None
        self.clip = self.case = None
        self.next_frame = self.next_sample = self.phase_wall = time.monotonic()
        self.frozen = []
        self.food_restore = []
        self.delegates = []
        self.contact_errors = []
        self.first_strong = None
        self.report.update(mode='nut_impacts_solo', time_clock='wall_seconds', run_directory=str(RUN),
            scope='Controlled real-body impact fixtures using actual F3 whole nuts in one owned solo authority PIE',
            clips={}, frames=[], screenshots=[], samples=[], contacts=[], cases=[], checks={},
            fixture_tuning={'two_test_nuts_gravity_disabled': True,
                'native_body_geometry_and_collision_preserved': True,
                'native_health_damage_counters_and_cooldowns_never_assigned': True,
                'one_velocity_assignment_per_disclosed_launch': True,
                'repeat_strong_launch_within_native_hit_cooldown': True},
            protected_packages_before=support['protected_packages'](),
            dirty_packages_before=support['dirty_packages'](),
            limitations=['Controlled horizontal impact fixture, not an untouched natural storm or balance playthrough.',
                'Gravity is disabled on exactly two actual nuts; other foods are frozen/hidden/collision-disabled for isolation.',
                'Event/director/landing-warning ticks are temporarily stopped; no boss spell/body-push coverage is claimed.',
                'Native OnComponentHit delegates, damage state and counters must prove results; estimates of input kinetic energy are fixture setup only.',
                'The cooldown test deliberately teleports/relaunches the same nut for a second actual collision; it never invokes a damage helper.',
                'Captures are genuine <=6Hz engine PNGs. Root visual review is required; no network, performance or packaged-build claim.'])
        self.report['source_sha256'] = {name: hashlib.sha256((ROOT/name).read_bytes()).hexdigest()
            for name in ('Tools/Unreal/probe_nut_impacts_solo.py',
                'Tools/Unreal/probe_nut_pattern_kinetics_solo.py',
                'Source/MessControl/Private/MCFoodActor.cpp', 'Source/MessControl/Public/MCFoodActor.h',
                'Source/MessControl/Private/MCFoodBodyComponent.cpp', 'Source/MessControl/Private/MCArenaTooth.cpp')}

    def write(self):
        self.output.mkdir(parents=True, exist_ok=True)
        encoded = json.dumps(self.report, ensure_ascii=False, indent=2, allow_nan=False)
        (self.output/'Report.json').write_text(encoded, encoding='utf8')
        OUTPUT.mkdir(parents=True, exist_ok=True)
        (OUTPUT/'Report.json').write_text(encoded, encoding='utf8')

    def stage(self, name):
        self.phase_wall = time.monotonic()
        super().stage(name)

    def native_hit(self, result):
        values = list(result) if isinstance(result, (tuple, list)) else [result]
        hit = next((v for v in values if isinstance(v, u.HitResult)), None)
        if hit is None:
            return None
        fields = self.statics.call_method('BreakHitResult', args=(hit,))
        self.require(len(fields) == len(HIT_FIELDS), 'Unexpected native BreakHitResult output')
        return dict(zip(HIT_FIELDS, fields))

    def closest(self, body, point):
        result = body.get_closest_point_on_collision(point)
        values = list(result) if isinstance(result, (tuple, list)) else [result]
        distance = next((float(v) for v in values if isinstance(v, (int, float)) and not isinstance(v, bool)), None)
        contact = next((v for v in values if isinstance(v, u.Vector)), None)
        self.require(distance is not None and distance >= 0 and contact is not None,
                     'Actual body closest-point query failed: '+path(body))
        return distance, contact

    def nut_state(self, nut):
        self.require(valid(nut), 'A native test nut was destroyed')
        body = prop(nut, 'body')
        return dict(path=path(nut), body=path(body), position_cm=vec(nut.get_actor_location()),
            velocity_cm_s=vec(body.get_physics_linear_velocity()), mass_kg=float(body.get_mass()),
            simulating=bool(body.is_simulating_physics()), enabled=bool(prop(nut, 'nut_kinetics_enabled')),
            fragment=bool(prop(nut, 'fragment')), phase=str(prop(nut, 'phase')),
            health=float(prop(nut, 'health')), impacts=int(prop(nut, 'nut_tooth_impacts')),
            last_energy_j=float(prop(nut, 'last_nut_impact_energy')))

    def tooth_state(self):
        state = prop(self.tooth, 'state')
        return dict(path=path(self.tooth), health=float(prop(state, 'health')),
                    hit_serial=int(prop(state, 'hit_serial')))

    def bind_contacts(self, nut):
        body = prop(nut, 'body')

        def observe(component, other, other_component, impulse, hit):
            try:
                record = dict(native_game_time=float(u.GameplayStatics.get_time_seconds(self.world)),
                    wall_seconds=time.monotonic()-self.started, clip=self.clip, nut=path(nut),
                    other=path(other), other_component=path(other_component), impulse_kg_cm_s=vec(impulse),
                    native_counter=int(prop(nut, 'nut_tooth_impacts')),
                    native_energy_j=float(prop(nut, 'last_nut_impact_energy')))
                native = self.native_hit(hit)
                if native:
                    record.update(blocking=bool(native['blocking_hit']), point_cm=vec(native['impact_point']),
                                  normal=vec(native['impact_normal']))
                if other == self.tooth:
                    record['tooth'] = self.tooth_state()
                self.report['contacts'].append(record)
            except Exception:
                self.contact_errors.append(traceback.format_exc())

        body.on_component_hit.add_callable(observe)
        self.delegates.append((body, observe))

    def freeze_tick(self, actor):
        if valid(actor):
            self.frozen.append((actor, actor.is_actor_tick_enabled()))
            actor.set_actor_tick_enabled(False)

    def prepare(self):
        self.require(not prop(self.event, 'failed'), 'Native F3 storm failed before fixture isolation')
        nuts = [n for n in prop(self.event, 'nuts') if valid(n) and prop(n, 'nut_kinetics_enabled')
                and not prop(n, 'fragment') and prop(n, 'body').is_simulating_physics()]
        self.require(len(nuts) >= 2, 'Two actual physics-enabled F3 nuts are required')
        self.nut, self.other = nuts[:2]
        self.freeze_tick(self.event)
        for cls in (u.MCGameDirector, u.MCNutLandingShadow):
            for actor in self.actors(cls):
                self.freeze_tick(actor)
        self.report['fixture_frozen_ticks'] = [path(a) for a, _ in self.frozen]
        for food in self.actors(u.MCFoodActor):
            body = prop(food, 'body')
            saved = (food, body, body.is_simulating_physics(), body.get_collision_enabled(),
                     body.is_gravity_enabled(), bool(prop(food, 'hidden')))
            self.food_restore.append(saved)
            if food in (self.nut, self.other):
                body.set_enable_gravity(False)
                body.set_physics_linear_velocity(u.Vector())
                body.set_physics_angular_velocity_in_radians(u.Vector())
                self.bind_contacts(food)
            else:
                body.set_simulate_physics(False)
                body.set_collision_enabled(u.CollisionEnabled.NO_COLLISION)
                food.set_actor_hidden_in_game(True)
                self.freeze_tick(food)
        self.require(all(self.nut_state(n)['enabled'] and not self.nut_state(n)['fragment'] for n in (self.nut, self.other)),
                     'Fixture substituted a fragment or non-kinetic nut')
        surface_center, _, _ = u.SystemLibrary.get_component_bounds(prop(self.tongue, 'surface'))
        body_extent = u.SystemLibrary.get_component_bounds(prop(self.nut, 'body'))[1]
        candidates = sorted(self.actors(u.MCArenaTooth),
            key=lambda tooth: abs(int(prop(prop(tooth, 'state'), 'tooth_id'))-3))
        for tooth in candidates:
            if float(prop(prop(tooth, 'state'), 'health')) < 90:
                continue
            body = prop(tooth, 'body')
            center, extent, _ = u.SystemLibrary.get_component_bounds(body)
            transform = body.get_world_transform()
            toward = transform.inverse_transform_direction(surface_center-center)
            local_normal = u.Vector(1. if toward.x >= 0 else -1., 0, 0) if abs(toward.x) >= abs(toward.y) else u.Vector(0, 1. if toward.y >= 0 else -1., 0)
            normal = normalized(transform.transform_direction(local_normal))
            _, contact = self.closest(body, center+normal*1200.)
            floor = self.floor(contact+normal*(float(body_extent.length())+100.))
            if floor is None or contact.z-float(body_extent.z) < floor.z+6:
                continue
            self.tooth, self.contact, self.normal = tooth, contact, normal
            break
        self.require(valid(self.tooth), 'No sufficiently high actual tooth face has a supported clear approach')
        self.report['fixture_tooth'] = dict(tooth=self.tooth_state(), face_cm=vec(self.contact),
            inward_normal=vec(self.normal), nut_bounds_extent_cm=vec(body_extent),
            actual_tooth_collision='PhysicalBody closest-point face, not visual mesh AABB')
        # Move the non-player camera observer away; no health or invulnerability edits.
        safe = self.floor(surface_center)
        self.require(safe is not None, 'Missing central safe observer floor')
        self.move_to(safe)
        self.park(self.other, surface_center+u.Vector(0, 0, 750.))
        self.begin_tooth_case('weak', .42)

    def park(self, nut, location):
        body = prop(nut, 'body')
        body.set_physics_linear_velocity(u.Vector())
        body.set_physics_angular_velocity_in_radians(u.Vector())
        nut.set_actor_location_and_rotation(location, u.Rotator(), False, True)

    def clear_box_path(self, nut, start, end, permitted=None):
        """Conservative actual-bounds sweep verifies setup, never supplies the impact."""
        _, extent, _ = u.SystemLibrary.get_component_bounds(prop(nut, 'body'))
        result = u.SystemLibrary.box_trace_single(self.world, start, end, extent,
            u.Rotator(), u.TraceTypeQuery.TRACE_TYPE_QUERY1, False,
            [self.nut, self.other, self.hero], u.DrawDebugTrace.NONE, True)
        hit = self.native_hit(result)
        okay = not hit or not hit['blocking_hit'] or hit['actor'] == permitted
        self.require(okay and (not hit or not hit['initial_overlap']),
            'Fixture path starts penetrating or is obstructed by '+str(path(hit['actor']) if hit else None))
        return dict(start_cm=vec(start), end_cm=vec(end), conservative_box_half_cm=vec(extent),
            blocking_actor=path(hit['actor']) if hit else None,
            permitted_terminal_actor=path(permitted), clear=True)

    def begin_tooth_case(self, label, multiplier):
        body = prop(self.nut, 'body')
        _, extent, _ = u.SystemLibrary.get_component_bounds(body)
        radius = sum(abs(getattr(self.normal, k))*abs(getattr(extent, k)) for k in ('x', 'y', 'z'))
        self.launch_point = self.contact+self.normal*(radius+75.)
        self.park(self.nut, self.launch_point)
        self.clip = label
        self.case = dict(name=label, before_nut=self.nut_state(self.nut), before_tooth=self.tooth_state(),
                         contacts_start=len(self.report['contacts']), shots=[], passed=False)
        self.report['cases'].append(self.case)
        settings = prop(self.nut, 'settings')
        threshold = float(prop(settings, 'nut_tooth_min_energy'))
        speed = math.sqrt(2.*threshold/float(body.get_mass()))*100.*multiplier
        self.case.update(threshold_j=threshold, speed_cm_s=speed,
                         input_normal_energy_j=.5*float(body.get_mass())*(speed/100.)**2,
                         cooldown_seconds=float(prop(settings, 'hit_cooldown')))
        self.case['clear_start'] = self.clear_box_path(self.nut, self.launch_point,
            self.launch_point+u.Vector(0,0,.1))
        self.case['clear_approach'] = self.clear_box_path(self.nut, self.launch_point,
            self.contact, self.tooth)
        side = u.Vector(-self.normal.y, self.normal.x, 0)
        target = self.contact+self.normal*float(radius*.5)
        self.frame_camera(target+self.normal*580.+side*230.+u.Vector(0, 0, 260.), target)
        prop(self.camera, 'camera_component').set_editor_property('FieldOfView', 62.)
        self.stage('align_'+label)

    def launch(self, label):
        body = prop(self.nut, 'body')
        self.park(self.nut, self.launch_point)
        body.set_physics_linear_velocity(self.normal*(-self.case['speed_cm_s']))
        body.wake_all_rigid_bodies()
        shot = dict(label=label, native_game_time=float(u.GameplayStatics.get_time_seconds(self.world)),
                    position_cm=vec(self.launch_point), velocity_cm_s=vec(body.get_physics_linear_velocity()))
        self.case['shots'].append(shot)
        return shot

    def begin_chain(self):
        center, _, _ = u.SystemLibrary.get_component_bounds(prop(self.tongue, 'surface'))
        floor = self.floor(center)
        self.require(floor is not None, 'Missing supported central nut-to-nut fixture footprint')
        axis = u.Vector(1, 0, 0)
        a_extent = u.SystemLibrary.get_component_bounds(prop(self.nut, 'body'))[1]
        b_extent = u.SystemLibrary.get_component_bounds(prop(self.other, 'body'))[1]
        target = floor+u.Vector(0, 0, max(float(a_extent.z), float(b_extent.z))+300.)
        self.park(self.other, target)
        self.park(self.nut, target-axis*(float(a_extent.x+b_extent.x)+110.))
        self.clip = 'nut_to_nut'
        self.case = dict(name=self.clip, before_nut=self.nut_state(self.nut), before_other=self.nut_state(self.other),
            contacts_start=len(self.report['contacts']), passed=False, launch_velocity_cm_s=[650.,0.,0.])
        self.report['cases'].append(self.case)
        self.case['clear_start'] = self.clear_box_path(self.nut, self.nut.get_actor_location(),
            self.nut.get_actor_location()+u.Vector(0,0,.1))
        self.case['clear_approach'] = self.clear_box_path(self.nut, self.nut.get_actor_location(), target)
        self.chain_direction = axis
        self.frame_camera(target+u.Vector(-330., -620., 290.), target+u.Vector(-100., 0, 0))
        prop(self.camera, 'camera_component').set_editor_property('FieldOfView', 68.)
        self.stage('align_chain')

    def screenshot(self, name, sequence=False):
        evidence = self.camera_evidence()
        self.require(self.pc.get_view_target() == self.camera and evidence['location_error_cm'] < 2.
            and evidence['rotation_error_degrees'] < 2., 'Actual PlayerCameraManager is not at the contact review camera')
        target = self.output/('frames' if sequence else 'screenshots')/name
        target.parent.mkdir(parents=True, exist_ok=True)
        self.console('Shot SHOWUI filename="'+str(target).replace('\\', '/')+'" -nosuffix')
        record = dict(file=str(target), phase=self.phase, clip_window=self.clip,
            time=time.monotonic()-self.started, native_game_time=float(u.GameplayStatics.get_time_seconds(self.world)),
            camera_evidence=evidence, viewport_size=list(self.pc.get_viewport_size()))
        self.report['frames' if sequence else 'screenshots'].append(record)
        if sequence:
            self.report['clips'].setdefault(self.clip, []).append(record)

    def verify_frames(self):
        for frame in self.report['frames']+self.report['screenshots']:
            file = Path(frame['file'])
            self.require(file.is_file(), 'Missing genuine PNG: '+str(file))
            data = file.read_bytes()
            self.require(data[:8] == b'\x89PNG\r\n\x1a\n' and data[-12:] == b'\x00\x00\x00\x00IEND\xaeB`\x82', 'Incomplete PNG')
            dimensions = (int.from_bytes(data[16:20], 'big'), int.from_bytes(data[20:24], 'big'))
            self.require(dimensions in ((1280,720), (1286,760)), 'Shot captured the wrong window size')
            frame.update(width=dimensions[0], height=dimensions[1], sha256=hashlib.sha256(data).hexdigest())
        if self.capture_hz:
            for label in ('weak', 'strong_and_cooldown', 'nut_to_nut'):
                frames = self.report['clips'].get(label, [])
                self.require(len(frames) >= 4, 'Too few actual capture frames for '+label)
                self.require(all(b['time']-a['time'] >= 1./6.-.006 for a,b in zip(frames,frames[1:])), 'Capture exceeded6Hz wall time')

    def tick(self, _delta):
        if self.done:
            return
        try:
            wall = time.monotonic()
            self.require(wall-self.started < 80., 'Impact fixture exceeded80 wall seconds')
            worlds = list(u.EditorLevelLibrary.get_pie_worlds(True))
            if self.world is None:
                if not worlds:
                    return
                self.require(len(worlds) == 1 and u.SystemLibrary.is_server(worlds[0]), 'Expected one solo authority PIE')
                pc = u.GameplayStatics.get_player_controller(worlds[0], 0)
                if pc is None or pc.get_controlled_pawn() is None:
                    return
                self.world, self.pc, self.hero = worlds[0], pc, pc.get_controlled_pawn()
                self.report['world'] = path(self.world)
                self.stage('first_perk')
                return
            self.require(self.owned_pie_is_active(worlds) and len(worlds) == 1, 'Owned PIE stopped or replaced')
            now = float(u.GameplayStatics.get_time_seconds(self.world))
            self.require(not self.contact_errors, 'Contact observer failed: '+repr(self.contact_errors))
            if self.phase == 'first_perk':
                self.require(wall-self.phase_wall < 25., 'Normal first-perk UI did not open')
                if self.pc.is_reward_menu_open():
                    self.pc.choose_reward_perk(0)
                    self.stage('perk_resolution')
                return
            if self.phase == 'perk_resolution':
                if not self.pc.is_reward_menu_open():
                    self.pc.request_dev_action(u.MCDevAction.NUT_ENCOUNTER, -1)
                    self.stage('await_two_nuts')
                return
            if self.phase == 'await_two_nuts':
                self.require(wall-self.phase_wall < 15., 'Real F3 did not provide two kinetic whole nuts')
                self.hero = self.pc.get_controlled_pawn()
                self.tongue = next(iter(self.actors(u.MCTongue)), None)
                events = [e for e in self.actors(u.MCNutRainEvent) if e.actor_has_tag('MC_DevKeyEvent')]
                if len(events) == 1 and int(prop(events[0], 'nuts_spawned')) >= 2:
                    self.event = events[0]
                    self.prepare()
                return
            if self.phase == 'flush':
                if wall-self.phase_wall >= .8:
                    self.verify_frames()
                    self.require(all(c['passed'] for c in self.report['cases']) and len(self.report['cases']) == 3, 'A required physical case failed')
                    self.report['checks'].update(real_f3_nuts=True, actual_weak_contact_without_tooth_damage=True,
                        actual_strong_contact_with_native_damage_counter=True, actual_repeat_contact_rejected_inside_cooldown=True,
                        nut_to_nut_real_contact_transfers_velocity_without_auto_break=True, genuine_camera_pngs=True)
                    self.finish()
                return
            self.keep_review_camera()
            evidence = self.camera_evidence()
            camera_ready = evidence['location_error_cm'] < 2. and evidence['rotation_error_degrees'] < 2.
            if camera_ready and self.clip and wall >= self.next_sample:
                self.next_sample = wall+.05
                self.report['samples'].append(dict(native_game_time=now, wall_seconds=wall-self.started, clip=self.clip,
                    nut=self.nut_state(self.nut), other=self.nut_state(self.other), tooth=self.tooth_state()))
            if camera_ready and self.clip and self.capture_hz and wall >= self.next_frame:
                self.next_frame = wall+1./self.capture_hz
                self.screenshot('%05d.png' % len(self.report['frames']), True)
            if self.phase.startswith('align_'):
                if not camera_ready or wall-self.phase_wall < .3:
                    return
                self.screenshot(self.clip+'_before.png')
                if self.phase == 'align_chain':
                    prop(self.nut, 'body').set_physics_linear_velocity(self.chain_direction*650.)
                    prop(self.nut, 'body').wake_all_rigid_bodies()
                    self.case['launch_game_time'] = now
                    self.stage('chain')
                else:
                    self.launch('initial')
                    self.stage('weak_contact' if self.clip == 'weak' else 'strong_contact')
                return
            relevant = [c for c in self.report['contacts'][self.case['contacts_start']:]
                        if c['nut'] == path(self.nut) and c['other'] == path(self.tooth)]
            if self.phase == 'weak_contact':
                self.require(now-self.phase_at < 3., 'Weak shot did not reach the actual tooth')
                if relevant and now-relevant[0]['native_game_time'] >= .65:
                    after, tooth = self.nut_state(self.nut), self.tooth_state()
                    self.require(after['impacts'] == self.case['before_nut']['impacts'] and abs(tooth['health']-self.case['before_tooth']['health']) < .001,
                                 'Weak physical contact damaged the tooth')
                    self.case.update(passed=True, after_nut=after, after_tooth=tooth, actual_tooth_contacts=len(relevant))
                    self.screenshot('weak_after.png')
                    self.begin_tooth_case('strong_and_cooldown', 2.6)
            elif self.phase in ('strong_contact', 'cooldown_contact'):
                self.require(now-self.phase_at < 3., 'Strong/repeat shot did not produce a real contact')
                delta = self.nut_state(self.nut)['impacts']-self.case['before_nut']['impacts']
                if self.first_strong is None and delta == 1:
                    accepted = next((c for c in relevant if c['native_counter'] > self.case['before_nut']['impacts']), None)
                    self.require(accepted is not None, 'Native damage counter rose without the observed actual tooth contact')
                    self.first_strong = accepted
                    self.require(accepted['native_energy_j'] > self.case['threshold_j'] and accepted['tooth']['health'] < self.case['before_tooth']['health'],
                                 'Strong physical contact lacked energy or native tooth HP loss')
                if self.first_strong is not None:
                    self.require(delta == 1, 'Strong contact produced duplicate damage within the cooldown')
                    elapsed = now-self.first_strong['native_game_time']
                    if self.phase == 'strong_contact' and elapsed >= .14:
                        shot = self.launch('repeat_inside_native_cooldown')
                        self.case['repeat_launch_delta_seconds'] = shot['native_game_time']-self.first_strong['native_game_time']
                        self.stage('cooldown_contact')
                    if self.phase == 'cooldown_contact':
                        repeats = [c for c in relevant if c['native_game_time'] > self.case['shots'][-1]['native_game_time']]
                        if repeats and elapsed >= min(.65, self.case['cooldown_seconds']*.85):
                            self.require(all(c['native_game_time']-self.first_strong['native_game_time'] < self.case['cooldown_seconds'] for c in repeats),
                                         'Repeated contact arrived outside native cooldown; test would be inconclusive')
                            after_tooth = self.tooth_state()
                            self.require(abs(after_tooth['health']-self.first_strong['tooth']['health']) < .001,
                                         'Repeat contact caused additional HP loss inside cooldown')
                            self.case.update(passed=True, after_nut=self.nut_state(self.nut), after_tooth=after_tooth,
                                first_accepted_contact=self.first_strong, actual_tooth_contacts=len(relevant), repeated_contacts_inside_cooldown=len(repeats))
                            self.screenshot('strong_cooldown_after.png')
                            self.begin_chain()
            elif self.phase == 'chain':
                self.require(now-self.phase_at < 3., 'Real nut-to-nut contact/transfer did not occur')
                contacts = [c for c in self.report['contacts'][self.case['contacts_start']:]
                    if {c['nut'], c['other']} == {path(self.nut), path(self.other)}]
                velocity = prop(self.other, 'body').get_physics_linear_velocity()
                forward = float(velocity.x*self.chain_direction.x+velocity.y*self.chain_direction.y+velocity.z*self.chain_direction.z)
                self.case['maximum_other_forward_velocity_cm_s'] = max(self.case.get('maximum_other_forward_velocity_cm_s', 0.), forward)
                if contacts and now-contacts[0]['native_game_time'] >= .6:
                    a, b = self.nut_state(self.nut), self.nut_state(self.other)
                    self.require(self.case['maximum_other_forward_velocity_cm_s'] > 50., 'Stationary nut did not receive physical momentum')
                    self.require(a['phase'] != str(u.MCFoodPhase.DISPOSED) and b['phase'] != str(u.MCFoodPhase.DISPOSED)
                        and abs(a['health']-self.case['before_nut']['health']) < .001
                        and abs(b['health']-self.case['before_other']['health']) < .001, 'Nut-to-nut impact auto-broke a whole nut')
                    self.case.update(passed=True, after_nut=a, after_other=b, actual_nut_contacts=len(contacts))
                    self.screenshot('nut_to_nut_after.png')
                    self.clip = None
                    self.stage('flush')
        except Exception:
            self.finish(traceback.format_exc())

    def finish(self, error=None):
        if self.done:
            return
        restore_errors = []
        if self.owned_pie_is_active():
            for body, callback in self.delegates:
                try:
                    if valid(body):
                        body.on_component_hit.remove_callable(callback)
                except Exception:
                    restore_errors.append(traceback.format_exc())
            for food, body, simulate, collision, gravity, hidden in reversed(self.food_restore):
                try:
                    if valid(body):
                        body.set_enable_gravity(gravity)
                        body.set_collision_enabled(collision)
                        body.set_simulate_physics(simulate)
                    if valid(food):
                        food.set_actor_hidden_in_game(hidden)
                except Exception:
                    restore_errors.append(traceback.format_exc())
            for actor, tick in reversed(self.frozen):
                try:
                    if valid(actor):
                        actor.set_actor_tick_enabled(tick)
                except Exception:
                    restore_errors.append(traceback.format_exc())
            if valid(self.event):
                try:
                    self.pc.request_dev_action(u.MCDevAction.STOP_NUT_ENCOUNTER, -1)
                except Exception:
                    restore_errors.append(traceback.format_exc())
        self.report['protected_packages_after'] = support['protected_packages']()
        self.report['dirty_packages_after'] = support['dirty_packages']()
        unchanged = self.report['protected_packages_after'] == self.report['protected_packages_before']
        dirty_preserved = self.report['dirty_packages_after'] == self.report['dirty_packages_before']
        self.report['checks'].update(protected_packages_unchanged=unchanged, user_dirty_packages_preserved=dirty_preserved)
        if restore_errors:
            self.report['fixture_restore_errors'] = restore_errors
            error = error or 'Physical fixture restoration failed'
        if not unchanged or not dirty_preserved:
            error = error or 'Saved packages or editor dirty state changed'
        super().finish(error)


def main():
    old = getattr(builtins, KEY, None)
    if old is not None and not getattr(old, 'done', True):
        raise RuntimeError('A physical nut-impact probe is already running')
    probe = NutImpactsSolo()
    setattr(builtins, KEY, probe)
    try:
        probe.start()
    except Exception:
        probe.finish(traceback.format_exc())
        raise


if __name__ == '__main__':
    main()
