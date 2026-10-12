"""Owned solo PIE check of native fragments, E stack pickup and instant delivery.

Run in the idle MessControl Editor on L_Mouth after the fragment changes load.
Reuses the guarded standalone start/settings cleanup from the approved
environment probe. No editor map/config/content is saved or replaced.

The real F3 DropFood path selects one authored Egg variant for this isolated
contact fixture. Only its Health
is lowered to 1 in this PIE fixture; primary input with the normal Knife or
Pickaxe must actually break it and create >=3 native fragments. Fragment mesh,
scale, count and collision are never authored by this check. The reflected
ServerToggleFoodCollection E-input RPC drives the existing stack (the C++
SetFoodCollectionInput helper itself is not reflected). Ordinary movement input
carries it into the actual throat delivery strip; no delivery helper is called.
The known five-fragment Egg must retain 25 points/25 team XP/five swallowed
pieces while producing exactly one source-identified native success burst.

A native loose fragment gets one physical velocity fixture through the standing
player before pickup: observe crossing, HP/body state and Pawn/floor responses.
Ragdoll-player collision and multiplayer are deferred, not claimed validated.

Latest Report.json: Saved/Checks/FragmentStackSolo. Actual screenshots, frame
timestamps and run report go into a fresh Runs directory. Optional globals:
MC_FRAGMENT_OUTPUT, MC_FRAGMENT_CAPTURE_HZ (default 6; 0 for key stills only).
MC_FRAGMENT_COLLISION=False skips the separate velocity fixture while retaining
the core native generation, E stack pickup and walking-delivery checks.
Cancel: builtins.mc_fragment_stack_solo.finish('Stopped by operator').
"""
import builtins
import datetime
import json
import math
import runpy
import time
import traceback
import uuid
from pathlib import Path

import unreal as u


KEY = 'mc_fragment_stack_solo'
BASE_OUTPUT = Path(globals().get('MC_FRAGMENT_OUTPUT',
                   Path(u.Paths.project_saved_dir())/'Checks/FragmentStackSolo')).resolve()
support = runpy.run_path(str(Path(u.Paths.project_dir())/'Tools/Unreal/check_approved_environment_solo.py'),
                        run_name='fragment_stack_probe_support',
                        init_globals={'MC_ENV_OUTPUT': BASE_OUTPUT,
                                      'MC_ENV_CAPTURE_HZ': globals().get('MC_FRAGMENT_CAPTURE_HZ', 6)})
OwnedSolo = support['ApprovedEnvironmentSolo']
prop = support['prop']
vec = support['vec']


def valid(obj):
    return obj is not None and u.SystemLibrary.is_valid(obj)


def path(obj):
    return obj.get_path_name() if valid(obj) else None


class FragmentStackSolo(OwnedSolo):
    def __init__(self):
        super().__init__()
        self.root_output = self.output
        run_id = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')+'_'+uuid.uuid4().hex[:8]
        self.output = self.root_output/'Runs'/run_id
        self.output.mkdir(parents=True, exist_ok=False)
        self.food = None
        self.parent = None
        self.fragments = []
        self.collection = self.throat = self.scheduler = self.identity = None
        self.scheduler_old_tick = None
        self.before_spawn = set()
        self.spawn_attempts = 0
        self.next_f3_request_at = 0.
        self.walking_since = None
        self.base = self.delivery_floor = None
        self.collision_piece = None
        self.collision_crossed = False
        self.collision_knocked_down = False
        self.first_delivery_at = None
        self.transit_seen = set()
        self.max_pieces = 0
        self.last_sample_at = -1.
        self.collision_enabled = bool(globals().get('MC_FRAGMENT_COLLISION', True))
        self.before_cut = self.before_delivery = None
        self.before_delivery_vfx = set()
        self.delivery_vfx_observed = {}
        self.report = dict(status='running', passed=False,
                           scope='One owned standalone PIE on L_Mouth; real F3, primary, E-equivalent and walking paths',
                           fixture='Parent Health=1, supported parent/pile placement, one loose fragment velocity; no fragment mesh/scale/count/collision changes',
                           limitations=['Ragdoll-player contact and multiplayer are not tested.',
                                        'No broad random spawn/balance run; this uses one isolated ordinary ingredient.'],
                           previous_play_settings=self.old_play, checks={}, samples=[], screenshots=[], frames=[],
                           input_events=[], source_evidence=['MCFoodActor::HitFood / SpawnFragments',
                           'UMCFoodCollectionComponent::Collect / TickComponent', 'AMCThroat::AcceptDelivery'])
        self.write()

    def write(self):
        # Base __init__ does not call this; its start/finish resolve to this
        # override after the fragment-specific evidence has been initialized.
        self.report['phase'] = self.phase
        text = json.dumps(self.report, ensure_ascii=False, indent=2, allow_nan=False)
        (self.output/'Report.json').write_text(text, encoding='utf8')
        (self.root_output/'Report.json').write_text(text, encoding='utf8')

    def stage(self, name):
        super().stage(name)
        self.walking_since = None

    def primary_input(self, active):
        self.hero.call_method('SetPrimaryInputHeld', args=(bool(active),))
        self.primary = bool(active)
        if self.world:
            self.report['input_events'].append(dict(input='SetPrimaryInputHeld', active=bool(active),
                game_seconds=float(u.GameplayStatics.get_time_seconds(self.world))))

    def collection_input(self, active):
        # Mirror the public C++ helper's desired-state guard, then use the same
        # actual E-input RPC it calls. Never invoke Collect/Toggle on components.
        collecting = bool(prop(prop(self.hero, 'food_collection'), 'collecting'))
        if collecting != bool(active):
            self.hero.call_method('ServerToggleFoodCollection')
            self.report['input_events'].append(dict(input='ServerToggleFoodCollection', active=bool(active),
                game_seconds=float(u.GameplayStatics.get_time_seconds(self.world))))

    def standing(self):
        return prop(self.hero, 'tooth_physics').call_method('GetBodyState') == u.MCBodyState.STANDING

    def settled_walking(self, now):
        ready = self.standing() and prop(self.hero.get_movement_component(), 'movement_mode') == u.MovementMode.MOVE_WALKING
        if not ready:
            self.walking_since = None
            return False
        if self.walking_since is None:
            self.walking_since = now
        return now-self.walking_since >= .8

    def health(self):
        return float(prop(prop(prop(self.hero, 'status'), 'state'), 'health'))

    def economy(self):
        state = u.GameplayStatics.get_game_state(self.world)
        progression = prop(state, 'progression')
        result = dict(points=int(prop(self.identity, 'points')), food_swallowed=int(prop(self.throat, 'food_swallowed')),
                      team_xp=int(prop(progression, 'total_experience')) if valid(progression) else None)
        return result

    def foods(self):
        return self.actors(u.MCFoodActor)

    def piece_state(self, food):
        if not valid(food):
            return dict(actor=None, disposed=True, destroyed=True)
        body = prop(food, 'body')
        return dict(actor=path(food), phase=str(prop(food, 'phase')), fragment=bool(prop(food, 'fragment')),
                    disposed=prop(food, 'phase') == u.MCFoodPhase.DISPOSED,
                    stack_carrier=path(prop(food, 'stack_carrier')), batch=int(prop(food, 'batch')),
                    position_cm=vec(food.get_actor_location()), simulation=bool(body.is_simulating_physics()),
                    pawn_response=str(body.get_collision_response_to_channel(u.CollisionChannel.ECC_PAWN)),
                    floor_response=str(body.get_collision_response_to_channel(u.CollisionChannel.ECC_WORLD_STATIC)))

    def sample(self, now):
        pieces = list(prop(self.collection, 'pieces')) if self.collection else []
        self.max_pieces = max(self.max_pieces, len(pieces))
        self.report['samples'].append(dict(game_seconds=now, phase=self.phase,
            hero_cm=vec(self.hero.get_actor_location()), health=self.health(),
            body_state=str(prop(self.hero, 'tooth_physics').call_method('GetBodyState')),
            movement_mode=str(prop(self.hero.get_movement_component(), 'movement_mode')),
            collecting=bool(prop(self.collection, 'collecting')) if self.collection else False,
            piece_count=len(pieces), piece_actors=[path(food) for food in pieces],
            fragments=[self.piece_state(food) for food in self.fragments],
            economy=self.economy() if self.throat else None,
            reward_menu_open=bool(self.pc.is_reward_menu_open())))

    def stop_move(self):
        self.hero.get_movement_component().stop_movement_immediately()

    def require_alive_standing(self):
        self.require(self.health() > 0., 'The fragment fixture player died')
        self.require(self.standing(), 'The fragment fixture is contaminated by a non-standing player')
        self.require(not self.pc.is_reward_menu_open(), 'A reward UI interrupted the isolated fragment fixture')

    def choose_floor(self):
        for point in (u.Vector(-100, 0, 0), u.Vector(-300, 0, 0), u.Vector(100, 0, 0)):
            floor = self.floor(point)
            if floor and all(self.floor(floor+offset) is not None for offset in
                             (u.Vector(220, 0, 0), u.Vector(-300, 0, 0), u.Vector(0, 150, 0))):
                return floor
        raise RuntimeError('No supported central tongue area for fragment fixture')

    def choose_delivery_floor(self):
        geometry = self.throat.get_actor_transform()
        center = prop(self.throat, 'zone_center')
        strip = bool(prop(self.throat, 'use_delivery_strip'))
        half = prop(self.throat, 'delivery_strip_half_extent') if strip else u.Vector2D(
            float(prop(self.throat, 'zone_radius')), float(prop(self.throat, 'zone_radius')))
        candidates = []
        for dx in (0., -.5*half.x, .5*half.x):
            for dy in (0., -.25*half.y, .25*half.y):
                floor = self.floor(geometry.transform_location(center+u.Vector(dx, dy, 0)))
                if floor:
                    candidates.append(floor)
        self.require(candidates, 'The actual throat marker has no supported tongue approach')
        # Prefer the near half of the real marker, well before the fatal aperture.
        return min(candidates, key=lambda p: (p-self.base).length())

    def request_parent(self):
        now = float(u.GameplayStatics.get_time_seconds(self.world))
        if now < self.next_f3_request_at:
            self.stage('f3_request_delay')
            return
        self.spawn_attempts += 1
        self.require(self.spawn_attempts <= 20, 'F3 did not yield the authored Egg contact fixture with >=3 fragments in twenty attempts')
        self.next_f3_request_at = now+.6
        self.before_spawn = {food.get_path_name() for food in self.foods()}
        self.report.setdefault('f3_requests', []).append(dict(attempt=self.spawn_attempts, game_seconds=now,
            input='RequestDevAction DropFood', minimum_request_gap_game_seconds=.6))
        self.pc.call_method('RequestDevAction', args=(u.MCDevAction.DROP_FOOD, -1))
        self.stage('find_parent')

    def prepare_parent(self):
        self.move_to(self.base)
        body = prop(self.parent, 'body')
        extent = body.get_scaled_box_extent()
        # Use the real collision footprint: a fixed 160cm centre could overlap
        # a giant whole ingredient. Leave a capsule-to-shape gap while the
        # nearest surface remains within the ordinary 180cm tool contact reach.
        capsule_radius = self.hero.capsule_component.get_scaled_capsule_radius()
        separation = float(extent.x)+float(capsule_radius)+40.
        floor = self.floor(self.base+u.Vector(separation, 0, 0))
        if floor is None:
            self.report.setdefault('skipped_f3_items', []).append(dict(item=str(prop(self.parent, 'item_name')),
                native_body_half_extent_cm=vec(extent), reason='No supported floor at collision-safe whole-food cutting placement'))
            self.parent.destroy_actor()
            self.parent = None
            self.request_parent()
            return
        self.parent.set_actor_location_and_rotation(floor+u.Vector(0, 0, extent.z+5), u.Rotator(), False, True)
        body.set_physics_linear_velocity(u.Vector(), False, 'None')
        body.set_physics_angular_velocity_in_degrees(u.Vector(), False, 'None')
        self.parent.set_editor_property('Health', 1.)
        self.cut_preparation_health = self.health()
        row = prop(self.parent, 'food_data')
        resistance = prop(row, 'resistance')
        hard = resistance == u.MCFoodResistance.HARD or (
            resistance == u.MCFoodResistance.AUTOMATIC and str(prop(self.parent, 'item_name')) in ('Carrot', 'Nut', 'Crust', 'Tartar'))
        slot = u.MCToolSlot.PICKAXE if hard else u.MCToolSlot.KNIFE
        self.hero.inventory.server_select(slot)
        self.report['parent'] = dict(actor=path(self.parent), item_name=str(prop(self.parent, 'item_name')),
            batch=int(prop(self.parent, 'batch')), authored_fragments=int(prop(row, 'fragments')),
            resistance=str(resistance), tool=str(slot), transient_health=1., native_spawn='F3 DropFood',
            native_body_half_extent_cm=vec(extent), capsule_radius_cm=float(capsule_radius),
            placement_separation_cm=separation, capsule_to_body_gap_cm=40.)
        self.before_cut = self.economy()
        self.before_fragment_paths = {food.get_path_name() for food in self.foods() if prop(food, 'fragment')}
        self.frame_camera(self.base+u.Vector(440, -390, 340), self.base+u.Vector(95, 0, 75))
        self.stage('settle_cut')

    def arrange_pile(self):
        self.require_alive_standing()
        self.move_to(self.base)
        for index, food in enumerate(self.fragments):
            body = prop(food, 'body')
            offset = u.Vector(90+12*(index//2), (1 if index%2 else -1)*(42+8*(index//2)), 0)
            floor = self.floor(self.base+offset)
            self.require(floor is not None, 'Native fragment pile has no floor')
            food.set_actor_location_and_rotation(floor+u.Vector(0, 0, body.get_scaled_box_extent().z+4),
                                               food.get_actor_rotation(), False, True)
            body.set_physics_linear_velocity(u.Vector(), False, 'None')
            body.set_physics_angular_velocity_in_degrees(u.Vector(), False, 'None')
        self.frame_camera(self.base+u.Vector(400, -300, 230), self.base+u.Vector(90, 0, 65))
        self.stage('settle_pile')

    def start_collision(self):
        self.require_alive_standing()
        self.move_to(self.base)
        self.collision_piece = self.fragments[0]
        body = prop(self.collision_piece, 'body')
        self.require(body.get_collision_response_to_channel(u.CollisionChannel.ECC_PAWN) == u.CollisionResponseType.ECR_IGNORE,
                     'Native loose fragment still blocks Pawn')
        self.require(body.get_collision_response_to_channel(u.CollisionChannel.ECC_WORLD_STATIC) == u.CollisionResponseType.ECR_BLOCK,
                     'Native fragment lost its floor/world-static collision')
        self.collision_health = self.health()
        start = self.hero.get_actor_location()+u.Vector(-260, 0, 0)
        self.collision_piece.set_actor_location_and_rotation(start, u.Rotator(), False, True)
        body.set_physics_linear_velocity(u.Vector(1300, 0, 90), False, 'None')
        body.set_physics_angular_velocity_in_degrees(u.Vector(), False, 'None')
        self.report['loose_fragment_collision_fixture'] = dict(actor=path(self.collision_piece),
            start_cm=vec(start), initial_velocity_cm_s=[1300., 0., 90.],
            player_cm=vec(self.hero.get_actor_location()), health_before=self.collision_health)
        self.stage('loose_collision')

    def observe_delivery(self, now):
        states = [self.piece_state(food) for food in self.fragments]
        for state in states:
            phase = state.get('phase', '')
            if 'ABSORBING' in phase or 'SWALLOWING' in phase:
                self.transit_seen.add(phase)
        disposed = sum(state['disposed'] for state in states)
        if disposed and self.first_delivery_at is None:
            self.first_delivery_at = now
            self.report['first_delivery_game_seconds'] = now
        self.track_delivery_feedback(now)
        if disposed == len(self.fragments):
            self.stop_move()
            pieces = list(prop(self.collection, 'pieces'))
            after = self.economy()
            count = len(self.fragments)
            self.report['delivery'] = dict(before=self.before_delivery, after=after,
                fragments=count, all_disposed=True, remaining_collection_pieces=len(pieces),
                observed_transit_phases=sorted(self.transit_seen),
                completion_span_game_seconds=now-self.first_delivery_at,
                method='Ordinary AddMovementInput walk into the actual throat marker; no delivery helper')
            self.report['delivery']['points_awarded'] = after['points']-self.before_delivery['points']
            self.report['delivery']['team_xp_awarded'] = (after['team_xp']-self.before_delivery['team_xp']
                if after['team_xp'] is not None else None)
            self.report['delivery']['food_swallowed_delta'] = after['food_swallowed']-self.before_delivery['food_swallowed']
            self.require(not pieces, 'Disposed fragments remain in the native collection Pieces array')
            self.require(not self.transit_seen, 'An absorbing/swallowing transit was observed for native fragments')
            self.require(now-self.first_delivery_at <= .15, 'Fragment delivery was not immediate across the native stack')
            self.require(after['food_swallowed']-self.before_delivery['food_swallowed'] == count,
                         'Actual throat batch count did not increase once per fragment')
            self.require(after['points'] > self.before_delivery['points'], 'Actual delivery awarded no player points')
            if after['team_xp'] is not None:
                self.require(after['team_xp'] > self.before_delivery['team_xp'], 'Actual delivery awarded no team XP')
            self.require(count == 5, 'The established Egg fixture must yield its five authored native fragments')
            self.require(self.report['delivery']['points_awarded'] == 25, 'Five-fragment Egg delivery no longer awards 25 player points')
            self.require(self.report['delivery']['team_xp_awarded'] == 25, 'Five-fragment Egg delivery no longer awards 25 team XP')
            self.report['checks'].update(all_native_fragments_instantly_disposed=True,
                native_stack_empty_after_delivery=True, no_observable_absorption_or_swallow_transit=True,
                throat_count_once_per_fragment=True, actual_delivery_points=True)
            self.report['checks']['five_fragment_rewards_preserved_25_points_25_xp'] = True
            if after['team_xp'] is not None:
                self.report['checks']['actual_delivery_team_xp'] = True
            self.screenshot('04_fragments_delivered_stack_empty.png')
            self.stage('capture_flush')
            return True
        return False

    def track_delivery_feedback(self, now):
        # NotifyTaskFeedback spawns its Stars AMCReactionVFX at Point+110cm,
        # or the hero+90cm when no point is supplied. Effect is not a readable
        # Python property: retain source-derived identity plus public birth,
        # location, zero AppliedDamage and actor-ID evidence instead.
        for actor in self.actors(u.MCReactionVFX):
            actor_path = path(actor)
            if actor_path in self.before_delivery_vfx or actor_path in self.delivery_vfx_observed:
                continue
            created_at = now-float(actor.get_game_time_since_creation())
            self.delivery_vfx_observed[actor_path] = dict(actor=actor_path,
                observed_game_seconds=now, created_at_game_seconds=created_at,
                position_cm=vec(actor.get_actor_location()),
                applied_damage=float(actor.get_editor_property('AppliedDamage')),
                owner=path(actor.get_owner()))
        self.report['new_delivery_vfx_observed'] = list(self.delivery_vfx_observed.values())

    def check_single_delivery_feedback(self):
        self.require(self.first_delivery_at is not None, 'No actual fragment disposal frame was observed')
        anchors = [self.hero.get_actor_location()+u.Vector(0, 0, 90)]
        anchors.extend(food.get_actor_location()+u.Vector(0, 0, 110) for food in self.fragments if valid(food))
        matched = []
        for observed in self.delivery_vfx_observed.values():
            point = observed['position_cm']
            nearest = min(math.dist(point, vec(anchor)) for anchor in anchors)
            born_at_disposal = abs(observed['created_at_game_seconds']-self.first_delivery_at) <= .2
            if born_at_disposal and observed['applied_damage'] <= 0. and nearest <= 60.:
                matched.append(dict(observed, distance_to_success_anchor_cm=nearest))
        self.report['single_stack_feedback'] = dict(
            observed_native_success_burst_count=len(matched), matched_actors=matched,
            snapshot_vfx_ids_before_walk=sorted(self.before_delivery_vfx),
            disposal_game_seconds=self.first_delivery_at,
            identification='Source-derived success Stars identity: new AMCReactionVFX born at immediate disposal, zero AppliedDamage, near native success-feedback anchors',
            source='AMCToothCharacter::NotifyTaskFeedback -> AMCReactionVFX::Spawn(Stars)',
            private_effect_enum_read=False)
        self.require(len(matched) == 1, 'One delivered fragment stack must create exactly one native success burst; observed '+str(len(matched)))
        self.report['checks']['one_native_success_burst_per_delivered_stack'] = True

    def tick(self, _delta):
        if self.done:
            return
        try:
            self.require(time.monotonic()-self.started < 180., 'Short fragment probe exceeded its wall-time limit')
            if self.pie_start is not None:
                self.require(not prop(self.pie_start, 'Error'), 'StartPIE: '+str(prop(self.pie_start, 'Error')))
            worlds = list(u.EditorLevelLibrary.get_pie_worlds(True))
            if self.world is None:
                if not worlds:
                    return
                self.require(len(worlds) == 1 and u.SystemLibrary.is_standalone(worlds[0]),
                             'Expected exactly one standalone owned PIE world')
                pc = u.GameplayStatics.get_player_controller(worlds[0], 0)
                if pc is None or pc.get_controlled_pawn() is None:
                    return
                self.world, self.pc = worlds[0], pc
                self.report['world'] = self.world.get_path_name()
                self.stage('first_perk')
                return
            self.require(self.world in worlds, 'Owned PIE ended or was replaced')
            self.hero = self.pc.get_controlled_pawn()
            self.require(isinstance(self.hero, u.MCToothCharacter), 'The solo controller has no normal tooth hero')
            now = float(u.GameplayStatics.get_time_seconds(self.world))
            if self.last_game_time is not None and now <= self.last_game_time:
                return
            self.last_game_time = now
            age = now-self.phase_at
            if self.phase == 'first_perk':
                self.require(age < 25., 'Normal first-perk UI did not appear')
                if now >= 1. and self.pc.is_reward_menu_open():
                    self.pc.choose_reward_perk(0)
                    self.report['checks']['normal_first_perk_ready'] = True
                    self.stage('perk_resolution')
                return
            if self.phase == 'perk_resolution':
                self.require(age < 10., 'Normal first-perk choice did not resolve')
                if self.pc.is_reward_menu_open():
                    return
                self.report['checks']['normal_first_perk_resolved'] = True
                state = u.GameplayStatics.get_game_state(self.world)
                self.scheduler = prop(state, 'single_day_director')
                if valid(self.scheduler):
                    self.scheduler_old_tick = self.scheduler.is_actor_tick_enabled()
                    self.scheduler.set_actor_tick_enabled(False)
                state.set_editor_property('bDevManualEvents', True)
                self.identity = self.hero.get_editor_property('PlayerState')
                self.collection = prop(self.hero, 'food_collection')
                tongues = self.actors(u.MCTongue)
                throats = self.actors(u.MCThroat)
                self.require(tongues and throats, 'Actual tongue and throat delivery marker are required')
                self.tongue, self.throat = tongues[0], throats[0]
                self.base = self.choose_floor()
                self.delivery_floor = self.choose_delivery_floor()
                self.report['floor_fixture_cm'] = vec(self.base)
                self.report['actual_delivery_floor_cm'] = vec(self.delivery_floor)
                self.request_parent()
                return
            if now >= self.sample_at:
                self.sample_at = now+.1
                self.sample(now)
            if self.capture_hz and self.phase in ('cut', 'loose_collision', 'collect', 'stack_hold', 'walk_delivery', 'capture_flush') and now >= self.frame_at:
                self.frame_at = now+1./self.capture_hz
                self.screenshot('%05d.png'%len(self.report['frames']), sequence=True)
            if self.phase == 'f3_request_delay':
                if now >= self.next_f3_request_at:
                    self.request_parent()
            elif self.phase == 'find_parent':
                created = [food for food in self.foods() if food.get_path_name() not in self.before_spawn]
                if created:
                    self.require(len(created) == 1, 'The isolated F3 DropFood path created multiple ingredients')
                    food = created[0]
                    row = prop(food, 'food_data')
                    extent = prop(food, 'body').get_scaled_box_extent()
                    # Egg's narrow authored variant already established real
                    # contact in solo PIE. Large conservative vegetable bounds
                    # do not imply mesh contact at the player's chop heights.
                    manageable = (str(prop(food, 'item_name')) == 'Egg'
                                  and extent.x <= 120. and extent.y <= 300. and extent.z <= 300.)
                    if prop(row, 'kind') != u.MCFoodKind.FOOD or int(prop(row, 'fragments')) < 3 or not manageable:
                        self.report.setdefault('skipped_f3_items', []).append(dict(item=str(prop(food, 'item_name')),
                            kind=str(prop(row, 'kind')), authored_fragments=int(prop(row, 'fragments')),
                            native_body_half_extent_cm=vec(extent),
                            reason='Candidate is not the isolated authored Egg contact fixture',
                            rule='Egg, >=3 authored fragments, native half-extents X<=120 Y/Z<=300cm'))
                        food.destroy_actor()
                        self.request_parent()
                    else:
                        self.parent = food
                        self.stage('parent_landing')
                elif age >= .7:
                    self.report.setdefault('empty_f3_attempts', []).append(dict(attempt=self.spawn_attempts,
                        game_seconds=now, observed_wait_game_seconds=age,
                        reason='No food actor observed; native F3 SpawnMenuFoodEntry may reject a selected landing footprint',
                        source='MCDevCommands.cpp DropFood returns placement failure when SpawnMenuFoodEntry returns null'))
                    self.request_parent()
            elif self.phase == 'parent_landing':
                self.require(age < 15., 'The real incoming food did not land before the cutting fixture')
                if prop(self.parent, 'phase') == u.MCFoodPhase.FREE and self.standing():
                    self.prepare_parent()
            elif self.phase == 'settle_cut':
                self.require(age < 10., 'Cutting player did not settle Walking/Standing')
                if abs(self.health()-self.cut_preparation_health) > .01 or not self.standing():
                    self.primary_input(False)
                    self.report.setdefault('whole_fixture_interruptions', []).append(dict(
                        phase=self.phase, item=str(prop(self.parent, 'item_name')),
                        health_before=self.cut_preparation_health, health_after=self.health(),
                        body_state=str(prop(self.hero, 'tooth_physics').call_method('GetBodyState')),
                        action='Reject own whole-food fixture, wait for natural recovery, then retry native F3'))
                    self.parent.destroy_actor()
                    self.parent = None
                    self.stage('wait_recover_cut_retry')
                elif self.settled_walking(now):
                    self.report['cut_start_health'] = self.health()
                    self.primary_input(True)
                    self.stage('cut')
            elif self.phase == 'wait_recover_cut_retry':
                self.require(self.health()>0., 'The player died from the rejected whole-food fixture')
                self.require(age < 20., 'Player did not naturally recover from whole-food fixture impact')
                if self.settled_walking(now):
                    self.primary_input(False)
                    self.move_to(self.base)
                    self.request_parent()
            elif self.phase == 'cut':
                self.require(age < 5., 'Real primary tool input did not break the whole ingredient')
                created = [food for food in self.foods() if prop(food, 'fragment')
                           and food.get_path_name() not in self.before_fragment_paths
                           and int(prop(food, 'batch')) == int(prop(self.parent, 'batch'))]
                if prop(self.parent, 'phase') == u.MCFoodPhase.DISPOSED and len(created) >= 3:
                    self.primary_input(False)
                    self.fragments = created
                    self.report['native_fragment_actors'] = [path(food) for food in created]
                    self.report['checks']['real_primary_creates_native_fragments'] = True
                    economy = self.economy()
                    self.require(economy == self.before_cut, 'Whole-food cutting prematurely awarded delivery score/XP/count')
                    self.report['checks']['cutting_defers_rewards_until_delivery'] = True
                    self.screenshot('01_native_fragment_generation.png')
                    if self.collision_enabled:
                        self.stage('collision_settle')
                    else:
                        self.report['limitations'].append('Live fast-fragment crossing fixture skipped by MC_FRAGMENT_COLLISION=False.')
                        self.arrange_pile()
                elif not self.standing():
                    # Release the real shared primary state before native
                    # recovery so the next fixture cannot inherit a stale hold.
                    self.primary_input(False)
                    self.report.setdefault('whole_fixture_interruptions', []).append(dict(
                        phase=self.phase, item=str(prop(self.parent, 'item_name')),
                        health_before=self.cut_preparation_health, health_after=self.health(),
                        body_state=str(prop(self.hero, 'tooth_physics').call_method('GetBodyState')),
                        action='Release primary, reject own whole-food fixture and retry after natural recovery'))
                    self.parent.destroy_actor()
                    self.parent = None
                    self.stage('wait_recover_cut_retry')
            elif self.phase == 'collision_settle':
                self.require(age < 15., 'Player did not settle after native cutting')
                if self.settled_walking(now):
                    self.start_collision()
            elif self.phase == 'loose_collision':
                self.collision_knocked_down |= not self.standing()
                point = self.collision_piece.get_actor_location()
                if point.x > self.hero.get_actor_location().x+60:
                    self.collision_crossed = True
                if age >= .9:
                    floor = self.floor(point)
                    self.require(floor is not None, 'Fast native fragment left the supported tongue fixture')
                    self.require(self.collision_crossed, 'Fast native fragment did not cross the standing player')
                    self.require(abs(self.health()-self.collision_health) < .01, 'Loose fragment damaged the standing player')
                    self.require(not self.collision_knocked_down, 'Loose fragment knocked down the standing player')
                    self.require(point.z >= floor.z-5., 'Native fragment fell through the supporting floor')
                    self.report['checks'].update(loose_fragment_crosses_standing_player=True,
                        loose_fragment_no_player_hp_loss=True, loose_fragment_no_player_knockdown=True,
                        native_fragment_pawn_ignore_and_floor_block=True)
                    self.report['loose_fragment_collision_fixture'].update(end_cm=vec(point),
                        health_after=self.health(), floor_cm=vec(floor), crossed=True, knockdown=False)
                    self.screenshot('02_loose_fragment_player_collision.png')
                    self.arrange_pile()
            elif self.phase == 'settle_pile':
                self.require(age < 10., 'Player/pile did not settle before E pickup')
                if self.settled_walking(now):
                    self.collection_input(True)
                    self.stage('collect')
            elif self.phase == 'collect':
                self.require_alive_standing()
                self.require(age < 10., 'Public E-equivalent input and ordinary approach did not collect all native fragments')
                pieces = list(prop(self.collection, 'pieces'))
                if len(pieces) == len(self.fragments) and all(food in pieces for food in self.fragments):
                    self.require(len(pieces) >= 3, 'Native collection did not contain at least three fragments')
                    self.stop_move()
                    self.report['checks']['real_e_collects_native_fragment_stack'] = True
                    self.stage('stack_hold')
                else:
                    # Native loose fragments can separate under ordinary Chaos
                    # even after the starting fixture is arranged. Use walking
                    # to reach the remaining ingredients instead of changing
                    # CollectionReach, teleporting them, or invoking Collect.
                    remaining = [food for food in self.fragments if valid(food) and food not in pieces
                                 and prop(food, 'stack_carrier') is None
                                 and prop(food, 'phase') in (u.MCFoodPhase.FREE, u.MCFoodPhase.FALLING)]
                    self.require(remaining, 'Some native fragments are neither in the stack nor available for ordinary pickup')
                    closest = min(remaining, key=lambda food: (food.get_actor_location()-self.hero.get_actor_location()).length())
                    difference = closest.get_actor_location()-self.hero.get_actor_location()
                    distance_xy = math.hypot(difference.x, difference.y)
                    approach_gap = min(100., float(prop(self.collection, 'collection_reach'))*.55)
                    if distance_xy > approach_gap:
                        self.hero.add_movement_input(u.Vector(difference.x/distance_xy, difference.y/distance_xy, 0), 1., False)
                        self.report['collection_approach'] = dict(method='Ordinary AddMovementInput toward remaining loose fragment',
                            actual_collection_reach_cm=float(prop(self.collection, 'collection_reach')),
                            target=path(closest), gap_cm=approach_gap)
                    else:
                        self.stop_move()
                    position = self.hero.get_actor_location()
                    self.frame_camera(position+u.Vector(400, -300, 230), position+u.Vector(90, 0, 65))
            elif self.phase == 'stack_hold' and age >= 1.:
                self.require_alive_standing()
                pieces = list(prop(self.collection, 'pieces'))
                self.require(len(pieces) == len(self.fragments), 'The native fragment stack spilled before walking')
                self.require(all(prop(food, 'stack_carrier') == self.hero
                                 and not prop(food, 'body').is_simulating_physics()
                                 and prop(food, 'phase') in (u.MCFoodPhase.FREE, u.MCFoodPhase.CARRIED)
                                 for food in pieces),
                             'Native fragment pickup did not settle into its actual carrier stack')
                self.report['stack_state_basis'] = ('Collection membership + actual StackCarrier + physics disabled; '
                                                   'SetStackCarrier retains Free for the nonlegacy stack, Carried is not required')
                self.report['checks']['native_fragment_stack_settles'] = True
                self.report['settled_stack'] = [self.piece_state(food) for food in pieces]
                self.before_delivery = self.economy()
                self.before_delivery_vfx = {path(actor) for actor in self.actors(u.MCReactionVFX)}
                self.delivery_vfx_observed = {}
                self.screenshot('03_native_fragment_stack.png')
                self.stage('walk_delivery')
            elif self.phase == 'walk_delivery':
                self.require_alive_standing()
                self.require(age < 12., 'Ordinary walk did not deliver the native fragment stack')
                if self.observe_delivery(now):
                    return
                difference = self.delivery_floor-self.hero.get_actor_location()
                distance = math.hypot(difference.x, difference.y)
                if distance > 20.:
                    self.hero.add_movement_input(u.Vector(difference.x/distance, difference.y/distance, 0), 1., False)
                else:
                    self.stop_move()
                position = self.hero.get_actor_location()
                self.frame_camera(position+u.Vector(380, -340, 260), position+u.Vector(90, 0, 65))
            elif self.phase == 'capture_flush':
                self.track_delivery_feedback(now)
                if age >= .8:
                    self.check_single_delivery_feedback()
                    self.finish()
        except Exception:
            self.finish(traceback.format_exc())

    def finish(self, error=None):
        if self.done:
            return
        try:
            if valid(self.hero) and self.world in u.EditorLevelLibrary.get_pie_worlds(True):
                self.stop_move()
                self.collection_input(False)
            if valid(self.scheduler) and self.scheduler_old_tick is not None:
                self.scheduler.set_actor_tick_enabled(self.scheduler_old_tick)
        except Exception:
            error = error or traceback.format_exc()
        if not error and (not self.report['checks'] or not all(self.report['checks'].values())):
            error = 'Not all fragment checks passed'
        self.report['max_native_stack_pieces'] = self.max_pieces
        self.report['readable_preview_camera'] = 'Transient PIE CameraActor; native HUD and gameplay retained'
        super().finish(error)
        u.log('MC_FRAGMENT_STACK_SOLO_'+('FAIL ' if self.report['status'] == 'failed' else 'PASS ')+str(self.root_output/'Report.json'))


def main():
    previous = getattr(builtins, KEY, None)
    if previous is not None and not getattr(previous, 'done', True):
        raise RuntimeError('A previous fragment probe is still active; it was left untouched')
    probe = FragmentStackSolo()
    setattr(builtins, KEY, probe)
    try:
        probe.start()
    except Exception:
        probe.finish(traceback.format_exc())
        raise


if __name__ == '__main__':
    main()
