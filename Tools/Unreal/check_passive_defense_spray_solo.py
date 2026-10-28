"""Bounded owned-solo proof for the current defense/spray passives.

Require an idle saved L_Mouth Editor on the matching normal DLL, launched with
-MCHubOwned -MCHubTransientProfile -NoVoice. Start only after root coordinates
the saved catalog and native tests. No Editor launch/build/package/source edits.

Disclosed fixtures: review_rogue_gameplay('level') stops directors and initializes
the real player. Each isolated phase creates one native ordinary MCMouthSurface
on the actual tongue with initial CoffeeLeft=CoffeeTotal. Player placement is
initial phase setup only. Masks, damage, contacts, shader readiness and native
timers are never written. TryAcquirePerk/RemovePerk are the normal authoritative
loadout APIs. Actual SetPrimaryInputHeld, camera-orbit/zoom, AddMovementInput and
RequestDashInput handlers drive gameplay. No direct spray/clean/defense helper
or coating/cue spawn is called. Exact numerical effects belong to native tests.

No F3 page clicks are claimed. These are direct normal TryAcquirePerk diagnostic
grants, alongside real tool/input handlers. Camera cutaways happen only after
primary release, preserving real Hero-view targeting while spraying.

All frames/reports/concat stay on E:. No network, microphone, profile/package
saves or earned full run. Only this script's owned PIE is stopped/restored.
Cancel: builtins.mc_defense_spray_solo.finish('cancelled explicitly').
"""
import builtins
import hashlib
import json
import math
import runpy
import time
import traceback
from pathlib import Path
from datetime import datetime, timezone
import unreal as u

ROOT = Path(u.Paths.project_dir()).resolve()
OUT = Path(globals().get('MC_DEFENSE_SPRAY_OUTPUT',
    'E:/CodexWork/MessControl/Validation/PassiveDefenseSpray/'+datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ'))).resolve()
KEY = 'mc_defense_spray_solo'
assert ROOT.as_posix().lower() == 'e:/devgame/messcontrol' and OUT.drive.upper() == 'E:'
assert not OUT.exists(), 'Preserve old evidence; supply a NEW MC_DEFENSE_SPRAY_OUTPUT'
assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages(), 'Preserve unsaved maps'
assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages(), 'Preserve unsaved assets'
command = u.SystemLibrary.get_command_line().lower()
assert '-mchubowned' in command and '-mchubtransientprofile' in command, 'Owned transient-profile launch required'
assert '-novoice' in command, 'NoVoice launch required for this isolated solo probe'
previous = getattr(builtins, KEY, None)
assert previous is None or getattr(previous, 'done', True), 'An owned defense/spray probe is active'
SOURCES = ['Source/MessControl/Private/MCDefenseSprayPassives.cpp',
    'Source/MessControl/Public/MCDefenseSprayPassives.h',
    'Source/MessControl/Private/MCInventoryComponent.cpp',
    'Source/MessControl/Private/MCToothMovementComponent.cpp',
    'Source/MessControl/Private/MCToothStatusComponent.cpp',
    'Source/MessControl/Private/MCResultPassives.cpp',
    'Source/MessControl/Public/MCResultPassives.h',
    'Source/MessControl/Private/MCMouthLiquid.cpp',
    'Source/MessControl/Private/MCArenaToothGrime.cpp']
dll = ROOT/'Binaries/Win64/UnrealEditor-MessControl.dll'
assert dll.is_file() and dll.stat().st_mtime_ns >= max((ROOT/s).stat().st_mtime_ns for s in SOURCES), 'Normal DLL predates the inspected passive sources'
support = runpy.run_path(str(ROOT/'Tools/Unreal/check_approved_environment_solo.py'),
    run_name='defense_spray_support', init_globals={'MC_ENV_OUTPUT': OUT, 'MC_ENV_CAPTURE_HZ': 0})
Base, prop, vec = (support[key] for key in ('ApprovedEnvironmentSolo', 'prop', 'vec'))
support['PLAY']['clientFixedFPS'] = [0]


def valid(obj):
    return obj is not None and u.SystemLibrary.is_valid(obj)


def path(obj):
    return obj.get_path_name() if valid(obj) else None


def normalized_angle(value):
    return (value+180.) % 360.-180.


class DefenseSpraySolo(Base):
    def __init__(self):
        super().__init__()
        self.report.update(scope=__doc__, checks={}, stages=[], input_requests=[], fixture_actors=[],
            samples=[], recipe_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            source_hashes={s: hashlib.sha256((ROOT/s).read_bytes()).hexdigest() for s in SOURCES},
            api_evidence=[
                'MCToothCharacter.h UFUNCTION: SetPrimaryInputHeld, ApplyCameraOrbitInput, ZoomCamera, RequestDashInput, IsDashing',
                'MCPerkComponent.h UFUNCTION: TryAcquirePerk, RemovePerk, GetPerkTable, passive_perk_ids',
                'MCInventoryComponent.h UFUNCTION: ServerSelect; reflected SprayReadyAt/SprayBurstUntil',
                'MCGameplayUIEditorLibrary::CapturePIEGameWindow via ApprovedEnvironmentSolo.screenshot',
                'MCRogueGameplayReview.cpp level fixture; MCInventoryAim.cpp requires Hero view target for local camera targeting'],
            limitations=[
                'Disclosed initial director-stop, stain and player-placement fixtures; not an earned run.',
                'Normal native input handlers are invoked; physical mouse/keyboard and actual F3 clicks are not asserted.',
                'One standalone authority player; no multiplayer/client packet ordering, microphone or packaged/PSO certification.',
                'Mask decrease and HP observations prove gameplay occurrence; exact percentage/duration balance is covered by native tests.',
                'Captured amber edge, particles, framing and movement require actual frame/video review before visual acceptance.'])
        self.report.pop('fixture_tuning', None)
        self.report.pop('tuning_reason', None)
        self.busy = False
        self.mode_at = self.frame_due = self.sample_due = 0.
        self.inventory = self.perks = self.status = self.mist = None
        self.patch = self.center = self.phase_start = self.hazard = None
        self.last_mask = None
        self.mask_after_release = self.health_before_risk = None
        self.phase_kind = None
        self.aim_stable_at = None
        self.dash_hidden_seen = self.dash_started_seen = self.dash_returned_seen = False

    def write(self):
        self.output.mkdir(parents=True, exist_ok=True)
        (self.output/'Report.json').write_text(json.dumps(self.report, ensure_ascii=False, indent=2, allow_nan=False), encoding='utf8')

    def check(self, name, condition):
        self.report['checks'][name] = bool(condition)
        self.require(condition, name)

    def enter(self, phase):
        self.phase = phase
        self.mode_at = time.monotonic()-self.started
        self.report['stages'].append(dict(name=phase, wall_seconds=self.mode_at,
            game_seconds=float(u.GameplayStatics.get_time_seconds(self.world))))
        self.write()

    def input_snapshot(self):
        loading = [w for w in u.ObjectIterator(u.MCLoadingWidget)
            if w.get_owning_player() == self.pc and w.is_in_viewport()
            and w.get_visibility() not in (u.SlateVisibility.HIDDEN, u.SlateVisibility.COLLAPSED)]
        return dict(world_ready=bool(self.pc.is_world_presentation_ready()), reward_menu_open=bool(self.pc.is_reward_menu_open()),
            loading_visible=bool(loading), mouse_cursor=bool(prop(self.pc, 'show_mouse_cursor')),
            move_ignored=bool(self.pc.is_move_input_ignored()), look_ignored=bool(self.pc.is_look_input_ignored()),
            boss_intro=bool(self.pc.is_boss_intro_playing()))

    def require_input_ready(self, action):
        observed = self.input_snapshot()
        self.report.setdefault('input_gate_observations', []).append(dict(action=action, phase=self.phase, **observed))
        self.require(observed['world_ready'] and not any(observed[k] for k in (
            'reward_menu_open', 'loading_visible', 'mouse_cursor', 'move_ignored', 'look_ignored', 'boss_intro')),
            'Normal input gates remain released before '+action)

    def primary_input(self, active):
        if active:
            self.require_input_ready('held primary')
        super().primary_input(active)

    def grant_only(self, ident):
        self.primary_input(False)
        for old in list(prop(self.perks, 'passive_perk_ids')):
            self.require(self.perks.remove_perk(old), 'Normal removal of prior isolated passive '+str(old))
        self.require(self.perks.try_acquire_perk(ident), 'Normal TryAcquirePerk accepted '+ident)
        self.check('Isolated ownership '+ident, [str(x) for x in prop(self.perks, 'passive_perk_ids')] == [ident])
        self.report['input_requests'].append(dict(method='UMCPerkComponent.TryAcquirePerk', id=ident,
            provenance='Normal authoritative loadout acquisition; diagnostic grant, not F3 click or earned reward'))

    def choose_center(self):
        origin, _ = self.tongue.get_actor_bounds(False)
        for dx, dy in ((0, 0), (350, 0), (-350, 0), (0, 300), (0, -300)):
            at = self.floor(origin+u.Vector(dx, dy, 0))
            if at is None:
                continue
            supports = [self.floor(at+u.Vector(x, y, 0)) for x, y in ((-420, 0), (420, 0), (0, 300), (0, -300))]
            if all(p is not None and abs(p.z-at.z)<90. for p in supports):
                return at
        raise RuntimeError('No actual supported interior tongue footprint for this probe')

    def spawn_stain(self, label):
        if valid(self.patch):
            # Remove only the previous, script-created initial fixture between phases.
            self.report.setdefault('fixture_cleanup', []).append(path(self.patch))
            self.patch.destroy_actor()
        point = self.floor(self.center)
        self.require(point is not None, 'Actual tongue support for initial native stain')
        pose = u.Transform(location=point+u.Vector(0, 0, 5))
        self.patch = self.statics.call_method('BeginDeferredActorSpawnFromClass', args=(self.world,
            u.MCMouthSurface.static_class(), pose, u.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN,
            None, u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        self.require(self.patch is not None, 'Native ordinary stain allocated')
        self.patch.set_editor_properties(dict(randomize_liquid_size=False, liquid_half_size=180., ulcer=False, paint_tile=False))
        self.statics.call_method('FinishSpawningActor', args=(self.patch, pose, u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        status = prop(self.patch, 'status')
        state = prop(status, 'state')
        total = int(prop(state, 'coffee_total'))
        self.require(total>0, 'Native positive stain CoffeeTotal')
        state.set_editor_property('coffee_left', total)
        status.set_editor_property('state', state)  # Initial dirt fixture only. Never written again.
        mask = list(prop(self.patch, 'wipe_mask'))
        self.require(len(mask)==4096 and all(int(v)==255 for v in mask), 'Native untouched initial mask')
        self.report['fixture_actors'].append(dict(label=label, actor=path(self.patch), position=vec(self.patch.get_actor_location()),
            half_size_cm=180., supporting_native_tongue=path(self.tongue), support_query='Actual tongue Surface.LineTraceComponent',
            initial_coffee_left=total, initial_mask_sha256=hashlib.sha256(bytes(mask)).hexdigest(),
            initialization='Native ordinary stain on real tongue; initial CoffeeLeft only, never a mask/effect write'))
        self.last_mask = mask
        # Native spray shoulder offset is140cm sideways. A140cm target distance
        # degenerates to a vertical orbit aim (clamped at85degrees) once the
        # shoulder fully settles.200cm leaves a real aim solution within range.
        stance = self.floor(point-u.Vector(200, 0, 0))
        self.require(stance is not None, 'Actual floor at initial stance')
        self.phase_start = self.move_to(stance, 0.)
        self.report['fixture_actors'][-1]['initial_hero_position'] = vec(self.phase_start)
        self.report['fixture_actors'][-1]['stance_distance_cm'] = 200.
        self.pc.set_view_target_with_blend(self.hero, 0.)
        self.inventory.server_select(u.MCToolSlot.SPRAY)
        self.aim_stable_at = None

    def aim_at_stain(self):
        # Native camera orbit input changes the actual camera. Do not forge
        # SprayViewOrigin/Direction, control rotation or a spray hit receipt.
        camera = prop(self.hero, 'camera')
        eye, view = camera.get_world_location(), camera.get_world_rotation()
        wanted = u.MathLibrary.find_look_at_rotation(eye, self.patch.get_actor_location())
        yaw, pitch = normalized_angle(wanted.yaw-view.yaw), normalized_angle(wanted.pitch-view.pitch)
        sensitivity = float(prop(self.hero, 'camera_orbit_sensitivity'))
        yaw_step = max(-8., min(8., yaw*.25))
        pitch_step = max(-8., min(8., pitch*.25))
        self.hero.apply_camera_orbit_input(u.Vector2D(yaw_step/max(.02, sensitivity), pitch_step/max(.02, sensitivity)))
        error = max(abs(yaw), abs(pitch))
        wall = time.monotonic()-self.started
        if error < 1.2:
            self.aim_stable_at = wall if self.aim_stable_at is None else self.aim_stable_at
        else:
            self.aim_stable_at = None
        self.report['last_actual_camera_aim'] = dict(eye=vec(eye), desired_point=vec(self.patch.get_actor_location()),
            angular_error_degrees=error, real_hero_view_target=True)
        return self.aim_stable_at is not None and wall-self.aim_stable_at>.35

    def mask(self):
        return list(prop(self.patch, 'wipe_mask')) if valid(self.patch) else None

    @staticmethod
    def decreased(before, after):
        return before is not None and after is not None and any(int(b)<int(a) for a, b in zip(before, after))

    def hp(self):
        return float(prop(prop(self.status, 'state'), 'health'))

    def coatings(self):
        return [a for a in self.actors(u.MCSprayCoating) if a.get_owner()==self.hero]

    def walk_toward(self, point, tolerance=9.):
        at = self.hero.get_actor_location()
        dx, dy = point.x-at.x, point.y-at.y
        distance = math.hypot(dx, dy)
        if distance <= tolerance:
            return True
        self.hero.add_movement_input(u.Vector(dx/distance, dy/distance, 0), min(.35, max(.12, distance/180.)), False)
        return False

    def capture(self, wall):
        if wall < self.frame_due or self.phase in ('readiness', 'arrival_release'):
            return
        self.frame_due = wall+(1/12. if self.phase in ('houdini_dash', 'houdini_return') else .25)
        self.screenshot('%05d.png' % len(self.report['frames']), sequence=True)

    def sample_actual(self, wall):
        if wall < self.sample_due:
            return
        self.sample_due = wall+.1
        coatings = self.coatings()
        self.report['samples'].append(dict(phase=self.phase, wall_seconds=wall,
            game_seconds=float(u.GameplayStatics.get_time_seconds(self.world)), hero=vec(self.hero.get_actor_location()),
            hp=self.hp(), is_dashing=bool(self.hero.is_dashing()), hidden=bool(prop(self.hero, 'hidden')),
            body_state=str(prop(self.hero, 'tooth_physics').call_method('GetBodyState')),
            requested_primary=self.primary, spray_ready_at=float(prop(self.inventory, 'spray_ready_at')),
            spray_burst_until=float(prop(self.inventory, 'spray_burst_until')), spray_active=bool(self.mist.is_active()),
            selected=str(prop(self.inventory, 'selected')), coating_paths=[path(a) for a in coatings],
            native_mask_sha256=hashlib.sha256(bytes(self.mask())).hexdigest() if valid(self.patch) else None))

    def finish(self, error=None):
        super().finish(error)
        frames = self.report.get('frames', [])
        if frames:
            lines = ['ffconcat version 1.0']
            for index, frame in enumerate(frames):
                filename = Path(frame['file']).as_posix().replace("'", "'\\''")
                lines.append("file '"+filename+"'")
                if index+1<len(frames):
                    lines.append('duration %.6f' % max(.001, frames[index+1]['time']-frame['time']))
            (self.output/'Gameplay.ffconcat').write_text('\n'.join(lines)+'\n', encoding='utf8')
            self.report['video_recipe'] = dict(concat=str(self.output/'Gameplay.ffconcat'),
                output=str(self.output/'Gameplay.mp4'), time_domain='Actual recorded positive game-time intervals, no invented interpolation',
                command='ffmpeg -f concat -safe 0 -i Gameplay.ffconcat -fps_mode vfr -c:v libx264 -pix_fmt yuv420p -movflags +faststart Gameplay.mp4')
            self.write()

    def tick(self, delta):
        if self.done or self.busy:
            return
        self.busy = True
        try:
            wall = time.monotonic()-self.started
            self.require(wall<120., 'Bounded120-second owned solo probe')
            if self.pie_start is not None:
                self.require(not prop(self.pie_start, 'Error'), 'Asynchronous StartPIE error: '+str(prop(self.pie_start, 'Error')))
            worlds = list(u.EditorLevelLibrary.get_pie_worlds(True))
            if self.world is None:
                if not worlds:
                    return
                self.require(len(worlds)==1 and u.SystemLibrary.is_server(worlds[0]), 'One standalone authority PIE')
                pc = u.GameplayStatics.get_player_controller(worlds[0], 0)
                hero = pc.get_controlled_pawn() if pc else None
                if hero is None:
                    return
                self.require(isinstance(hero, u.MCToothCharacter), 'Real gameplay tooth pawn')
                self.world, self.pc, self.hero = worlds[0], pc, hero
                self.engine.set_editor_property('bUseFixedFrameRate', False)
                self.enter('readiness')
                return
            self.require(len(worlds)==1 and self.owned_pie_is_active(worlds), 'Owned PIE remains active and unreplaced')
            age = wall-self.mode_at
            if self.phase == 'readiness':
                self.require(age<45., 'Natural world-presentation readiness bounded45s')
                if age<2. or not self.pc.is_world_presentation_ready():
                    return
                result = u.MCRoguelikeEditorLibrary.review_rogue_gameplay(self.world, 'level')
                self.report['native_fixture'] = result
                self.require(not result.startswith('REJECT'), 'Native level fixture accepted')
                self.inventory = self.hero.get_component_by_class(u.MCInventoryComponent)
                self.status = self.hero.get_component_by_class(u.MCToothStatusComponent)
                self.perks = u.GameplayStatics.get_player_state(self.world, 0).get_component_by_class(u.MCPerkComponent)
                self.tongue = self.actors(u.MCTongue)[0]
                mist = [c for c in self.hero.get_components_by_class(u.NiagaraComponent) if c.get_name()=='TreatmentSprayNiagara']
                self.require(len(mist)==1, 'Actual owned treatment Niagara')
                self.mist = mist[0]
                self.enter('arrival_release')
                return
            if self.phase == 'arrival_release':
                observed = self.input_snapshot()
                self.report['arrival_observed'] = observed
                self.require(age<15., 'Natural arrival/input release bounded15s')
                if age<1. or not observed['world_ready'] or any(observed[k] for k in ('reward_menu_open', 'loading_visible', 'mouse_cursor', 'move_ignored', 'look_ignored', 'boss_intro')):
                    return
                self.center = self.choose_center()
                self.grant_only('B2_Softener')
                self.spawn_stain('Softener')
                self.hero.zoom_camera(6.)
                self.enter('softener_aim')
                return
            self.capture(wall)
            self.sample_actual(wall)
            if self.phase in ('softener_aim', 'lasting_aim', 'antiseptic_aim'):
                self.require(age<10., 'Real camera aiming converges without a forged target')
                if self.aim_at_stain():
                    self.last_mask = self.mask()
                    self.primary_input(True)
                    self.enter(self.phase.replace('_aim', '_spray'))
            elif self.phase == 'softener_spray' and age>1.:
                self.check('Softener spray emits through actual held input', self.mist.is_active())
                self.primary_input(False)
                self.check('Softener alone does not erase dirt', self.mask()==self.last_mask)
                self.screenshot('01_softener_sprayed_place.png')
                self.inventory.server_select(u.MCToolSlot.BRUSH)
                self.frame_camera(self.center+u.Vector(-100,-340,280), self.center)
                self.primary_input(True)
                self.enter('softener_brush')
            elif self.phase == 'softener_brush' and age>2.4:
                self.primary_input(False)
                self.check('Native held brush actually cleans the treated stain', self.decreased(self.last_mask, self.mask()))
                self.screenshot('02_softener_real_brush.png')
                self.grant_only('B2_LastingFormula')
                self.spawn_stain('LastingFormula')
                self.enter('lasting_aim')
            elif self.phase == 'lasting_spray' and age>1.:
                self.check('Lasting Formula actual spray emits', self.mist.is_active())
                self.primary_input(False)
                self.mask_after_release = self.mask()
                self.check('Only native spray created a coating', bool(self.coatings()))
                self.hazard = self.coatings()[0]
                self.hazard_point = self.hazard.get_actor_location()
                _, extent = self.hazard.get_actor_bounds(False)
                self.hazard_radius = max(float(extent.x), float(extent.y))
                self.check('Amber footprint has a bounded visible radius', 5.<self.hazard_radius<250.)
                self.report['hazard_visual'] = dict(actor=path(self.hazard), center=vec(self.hazard_point),
                    measured_world_bounds_radius_cm=self.hazard_radius,
                    material=path(prop(self.hazard, 'surface').get_material(0)),
                    origin='Actual native spray only; no direct coating spawn/helper')
                self.frame_camera(self.center+u.Vector(-90,-360,300), self.center)
                self.enter('lasting_released')
            elif self.phase == 'lasting_released' and age>1.:
                self.check('Coating persists after primary release', valid(self.hazard))
                self.check('Residual composition cleans actual mask after release', self.decreased(self.mask_after_release, self.mask()))
                self.screenshot('03_residue_after_release.png')
                self.health_before_risk = self.hp()
                self.enter('walk_into_coating')
            elif self.phase == 'walk_into_coating':
                self.require(age<4., 'Normal movement reaches actual amber footprint')
                reached = self.walk_toward(self.hazard_point)
                if reached or self.hp()<self.health_before_risk:
                    self.hero.get_movement_component().stop_movement_immediately()
                    self.enter('stand_in_coating')
            elif self.phase == 'stand_in_coating' and age>1.:
                self.check('Standing in own actual coating lowers HP', self.hp()<self.health_before_risk)
                self.report['self_risk'] = dict(before=self.health_before_risk, inside_after=self.hp(),
                    hero=vec(self.hero.get_actor_location()), coating=vec(self.hazard_point))
                self.screenshot('04_amber_footprint_actual_hp_loss.png')
                self.exit_point = self.hazard_point-u.Vector(self.hazard_radius+100.,0,0)
                self.enter('walk_out_of_coating')
            elif self.phase == 'walk_out_of_coating':
                self.require(age<4., 'Normal movement exits visible hazard footprint')
                if self.walk_toward(self.exit_point):
                    self.hero.get_movement_component().stop_movement_immediately()
                    self.safe_health = self.hp()
                    self.enter('outside_coating')
            elif self.phase == 'outside_coating' and age>.75:
                self.check('Outside visible disk stops coating HP pulses', abs(self.hp()-self.safe_health)<.01)
                self.screenshot('05_outside_amber_no_hp_loss.png')
                self.grant_only('B2_Antiseptic')
                self.check('Removing Lasting Formula clears its coatings', not self.coatings())
                self.spawn_stain('Antiseptic')
                self.enter('antiseptic_aim')
            elif self.phase == 'antiseptic_spray' and age>.55:
                self.check('Antiseptic held burst emits', self.mist.is_active())
                self.primary_input(False)
                self.report['antiseptic_deadlines'] = dict(game_seconds=float(u.GameplayStatics.get_time_seconds(self.world)),
                    ready_at=float(prop(self.inventory, 'spray_ready_at')), burst_until=float(prop(self.inventory, 'spray_burst_until')))
                self.check('Antiseptic actually removes native dirt', self.decreased(self.last_mask, self.mask()))
                self.enter('antiseptic_cooldown')
            elif self.phase == 'antiseptic_cooldown' and age>.65:
                now = float(u.GameplayStatics.get_time_seconds(self.world))
                self.check('Actual Antiseptic readiness deadline remains in future', float(prop(self.inventory, 'spray_ready_at'))>now+1.)
                self.check('Spray emission stops during reload', not self.mist.is_active())
                self.primary_input(True)
                self.screenshot('06_antiseptic_hud_reticle_cooldown.png')
                self.enter('antiseptic_repress')
            elif self.phase == 'antiseptic_repress' and age>.5:
                self.check('Fresh primary press cannot bypass Antiseptic reload', not self.mist.is_active())
                self.primary_input(False)
                self.grant_only('B2_HoudiniDash')
                self.inventory.server_select(u.MCToolSlot.BRUSH)
                floor = self.floor(self.center-u.Vector(360,0,0))
                self.require(floor is not None, 'Supported clear initial dash stance')
                self.move_to(floor, 0.)
                self.frame_camera(floor+u.Vector(-30,-470,220), floor+u.Vector(220,0,70))
                self.enter('houdini_settle')
            elif self.phase == 'houdini_settle' and age>.8:
                self.require(age<5., 'Normal physics returns to actionable standing before dash')
                if prop(self.hero, 'tooth_physics').call_method('GetBodyState') != u.MCBodyState.STANDING:
                    return
                self.check('Houdini baseline is visible', not bool(prop(self.hero, 'hidden')))
                self.screenshot('07_houdini_before_dash.png')
                self.require_input_ready('dash request')
                self.hero.request_dash_input()
                self.report['input_requests'].append(dict(method='MCToothCharacter.RequestDashInput',
                    meaning='Same normal action handler as a fresh Ctrl request, not StartDash/root-motion helper'))
                self.enter('houdini_dash')
            elif self.phase == 'houdini_dash':
                if self.hero.is_dashing():
                    self.dash_started_seen = True
                    if bool(prop(self.hero, 'hidden')) and not self.dash_hidden_seen:
                        self.dash_hidden_seen = True
                        self.screenshot('08_houdini_during_real_dash_hidden.png')
                if age>.75:
                    self.check('Normal request executes a real dash', self.dash_started_seen)
                    self.check('The actual dash makes the pawn invisible', self.dash_hidden_seen)
                    self.enter('houdini_return')
            elif self.phase == 'houdini_return' and age>.6:
                self.check('Dash ends normally', not self.hero.is_dashing())
                self.check('Pawn visibility returns after dash', not bool(prop(self.hero, 'hidden')))
                self.screenshot('09_houdini_after_dash_visible.png')
                self.finish()
        except Exception:
            self.finish(traceback.format_exc())
        finally:
            self.busy = False


probe = DefenseSpraySolo()
setattr(builtins, KEY, probe)
try:
    probe.start()
except Exception:
    probe.finish(traceback.format_exc())
    raise
