"""ROOT-RUN ONLY: two actual F3 Nutageddon openings in one owned solo PIE.

Uses the normal first-perk UI, then ?Seed=N in the transient GameMode.OptionsString
so RestartShift (called by F3) chooses each complete A/B order natively. No event
Start/SpawnNut/attack/damage helpers, fake arrays, clock jumps or native forces.
The observer is repositioned away from actual warnings; this is not a skill test.
Genuine Shot SHOWUI PNG requests are capped at8Hz by wall time. Root must select
the floating PIE window and visually approve its early frame before accepting.
Outputs stay on E:, no map loads/package saves/network peers/build/encoding.
Cancel: builtins.mc_nut_pattern_kinetics_solo.finish('Stopped by operator').
Non-__main__ imports are inert. Optional MC_NUT_PATTERN_OUTPUT, MC_NUT_PATTERN_HZ.
"""
import builtins
import datetime
import hashlib
import json
import math
import re
import runpy
import time
import traceback
import uuid
import zlib
from pathlib import Path

import unreal as u

KEY = 'mc_nut_pattern_kinetics_solo'
ROOT = Path(u.Paths.project_dir()).resolve()
OUTPUT = Path(globals().get('MC_NUT_PATTERN_OUTPUT', ROOT/'Saved/Checks/NutPatternKineticsSolo')).resolve()
if ROOT.drive.upper() != 'E:' or OUTPUT.drive.upper() != 'E:':
    raise RuntimeError('Project and all probe outputs must be on E:')
RUN = OUTPUT/'Runs'/(datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')+'_'+uuid.uuid4().hex[:8])
support = runpy.run_path(str(ROOT/'Tools/Unreal/check_nut_fight_redesign_solo.py'),
    run_name='nut_pattern_camera_support', init_globals={'MC_NUT_FIGHT_OUTPUT': RUN})
Base, prop, vec, valid = (support[k] for k in ('Base', 'prop', 'vec', 'valid'))
Camera = support['NutFightRedesignSolo']


def path(obj):
    return obj.get_path_name() if valid(obj) else None


def first_pattern_for_seed(seed):
    # Verified UE5.8 RandomStream.h GetFraction/RandHelper: the FIRST RandRange
    # after Event::Start's Initialize is the whole-pattern choice in BeginSeries.
    initial = (int(seed) & 0xffffffff) ^ 0x4E555453
    mutated = (initial*196314165+907633515) & 0xffffffff
    return int(((mutated >> 9)/8388608.)*2.)


def seeds_for_both_orders():
    result = {}
    for seed in range(1, 257):
        result.setdefault(first_pattern_for_seed(seed), seed)
    if set(result) != {0, 1}:
        raise RuntimeError('Could not choose deterministic native seeds for both orders')
    return [(order, result[order]) for order in (0, 1)]


def protected_packages():
    names = ('Content/Maps/L_Mouth.umap', 'Content/Gameplay/CoreLoop/DA_NutRain.uasset',
             'Content/Data/DA_FoodPhysics.uasset')
    return {name: hashlib.sha256((ROOT/name).read_bytes()).hexdigest()
            for name in names if (ROOT/name).is_file()}


def dirty_packages():
    return dict(maps=sorted(p.get_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_map_packages()),
                content=sorted(p.get_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()))


def inspect_png(data):
    """Verify all PNG chunks; preserve and report UE's non-pixel tail after IEND."""
    if not data.startswith(b'\x89PNG\r\n\x1a\n'):
        raise RuntimeError('Screenshot is not a genuine PNG')
    offset, dimensions, has_pixels = 8, None, False
    while offset+12 <= len(data):
        size = int.from_bytes(data[offset:offset+4], 'big')
        kind = data[offset+4:offset+8]
        end = offset+8+size
        if end+4 > len(data):
            raise RuntimeError('Truncated engine PNG')
        if zlib.crc32(data[offset+4:end]) & 0xffffffff != int.from_bytes(data[end:end+4], 'big'):
            raise RuntimeError('PNG chunk CRC mismatch')
        if offset == 8:
            if kind != b'IHDR' or size != 13:
                raise RuntimeError('Invalid PNG IHDR')
            dimensions = (int.from_bytes(data[offset+8:offset+12], 'big'), int.from_bytes(data[offset+12:offset+16], 'big'))
        has_pixels |= kind == b'IDAT'
        offset = end+4
        if kind == b'IEND':
            if size != 0 or not has_pixels:
                raise RuntimeError('PNG has invalid IEND or no image data')
            return dimensions, offset
    raise RuntimeError('Incomplete engine PNG: IEND missing')


def prune_unwritten_sequence_requests(report):
    """Exclude disclosed dropped requests/transient Editor-window captures.

    Every written PNG must have valid CRC. Raw files remain untouched; key
    stills, minimum clip coverage and all native gameplay checks stay strict.
    """
    frames = report.get('frames', [])
    prior = report.setdefault('missed_capture_requests', [])
    rejected = report.setdefault('rejected_window_captures', [])
    omit = set()
    for entry in frames:
        file = Path(entry['file'])
        if not file.is_file():
            prior.append(dict(entry, capture_kind='sequence', reason='Asynchronous Shot request was not written by the capture flush deadline'))
            omit.add(entry['file'])
            continue
        data = file.read_bytes()
        dimensions, png_end = inspect_png(data)
        if dimensions not in ((1280,720),(1286,760)):
            rejected.append(dict(entry, reason='Transient active Editor window rather than floating PIE client',
                width=dimensions[0], height=dimensions[1], sha256=hashlib.sha256(data).hexdigest(),
                png_bytes=png_end, trailing_bytes=len(data)-png_end, raw_file_preserved=True))
            omit.add(entry['file'])
    report['frames'] = [entry for entry in frames if entry['file'] not in omit]
    for clip, entries in report.get('clips', {}).items():
        report['clips'][clip] = [entry for entry in entries if entry['file'] not in omit]
    excluded = len(prior)+len(rejected)
    requested = len(report['frames'])+excluded
    ratio = excluded/requested if requested else 0.
    report['capture_request_accounting'] = dict(sequence_requests=requested,
        accepted_sequence_frames=len(report['frames']), missed_sequence_requests=len(prior),
        rejected_editor_window_frames=len(rejected), excluded_ratio=ratio, maximum_excluded_ratio=.1,
        policy='Disclosed absent requests and valid PNGs of wrong window excluded; raw files/timestamps/native checks preserved; stills strict')
    if ratio > .1:
        raise RuntimeError('More than10% of sequence requests were missing or captured the wrong window')


class NutPatternKineticsSolo(Base):
    overview = Camera.overview
    keep_review_camera = Camera.keep_review_camera
    camera_evidence = Camera.camera_evidence
    dodge = Camera.dodge

    def __init__(self):
        super().__init__()
        self.output = RUN
        self.capture_hz = max(0., min(8., float(globals().get('MC_NUT_PATTERN_HZ', 8.))))
        self.event = self.mode = self.gs = None
        self.old_options = None
        self.case_index = -1
        self.seed_cases = seeds_for_both_orders()
        self.case = self.clip = None
        self.dodge_at = self.capture_wall = self.sample_wall = 0.
        self.flush_wall = self.arrival_wall = self.phase_wall = time.monotonic()
        self.current_observed_launches = 0
        self.capture_tick = 0
        self.last_shot_tick = -1
        self.report.update(mode='nut_pattern_kinetics_solo', time_clock='wall_seconds',
            scope='Two actual F3 openings in one owned standalone authority PIE, native whole-pattern seeds',
            run_directory=str(RUN), clips={}, cases=[], frames=[], screenshots=[], samples=[], checks={},
            fixture_tuning={}, source_sha256={}, protected_packages_before=protected_packages(),
            dirty_packages_before=dirty_packages(), requested_orders=self.seed_cases,
            limitations=[
                'PIE GameMode.OptionsString Seed is the only pattern fixture; native F3 RestartShift, launch schedule, warning and collisions execute unchanged.',
                'Position-only observer dodging and a temporary review camera are disclosed visibility fixtures; no health edits or forces are applied.',
                'PIE uses its ordinary variable game clock (clientFixedFPS=0 and engine fixed-frame mode disabled); capture intervals use wall time. No clock jumps, dilation or invented motion.',
                'Nut-to-nut momentum and tooth damage occur only if the actual storm produces contacts; positive tooth counters are reported, not required or fabricated.',
                'Nut-to-nut contact has no exposed native counter; velocities/proximity alone are not proof of impulse transfer. Root must review the real video.',
                'This checks solo authority behavior, not client prediction, network replication, full boss combat, balance, cook or packaging.',
                'A valid camera transform/PNG size does not prove the mouth is visible: root visual review of early and final frames is required.'])
        for source in ('Tools/Unreal/probe_nut_pattern_kinetics_solo.py',
                       'Tools/Unreal/check_approved_environment_solo.py',
                       'Tools/Unreal/check_nut_fight_redesign_solo.py',
                       'Source/MessControl/Private/MCNutRainEvent.cpp',
                       'Source/MessControl/Private/MCNutRainProfile.cpp',
                       'Source/MessControl/Private/MCNutRainPattern.cpp',
                       'Source/MessControl/Private/MCFoodActor.cpp',
                       'Source/MessControl/Private/MCNutLandingShadow.cpp'):
            self.report['source_sha256'][source] = hashlib.sha256((ROOT/source).read_bytes()).hexdigest()

    def summons(self):
        return []  # The short opening ends before testing the wizard phase.

    def start(self):
        settings = Base.start.__globals__
        settings['PLAY'] = dict(settings['PLAY'], clientFixedFPS=[0])
        super().start()
        # StartPIE is deferred: remove the base fixed-frame mode before its
        # first game tick. Slow Shot captures then consume ordinary delta time.
        self.engine.set_editor_property('bUseFixedFrameRate', False)
        self.report['clock_configuration'] = dict(clientFixedFPS=[0], engine_fixed_frame_rate=False,
            native_game_clock='ordinary variable delta; no time jump or dilation',
            restoration='Base.finish restores original play and engine settings')
        self.write()

    def write(self):
        self.output.mkdir(parents=True, exist_ok=True)
        data = json.dumps(self.report, ensure_ascii=False, indent=2, allow_nan=False)
        (self.output/'Report.json').write_text(data, encoding='utf8')
        OUTPUT.mkdir(parents=True, exist_ok=True)
        (OUTPUT/'Report.json').write_text(data, encoding='utf8')

    def stage(self, name):
        self.phase_wall = time.monotonic()
        super().stage(name)

    def request_case(self):
        self.case_index += 1
        if self.case_index == len(self.seed_cases):
            self.clip = None
            self.flush_wall = time.monotonic()
            self.stage('capture_flush')
            return
        order, seed = self.seed_cases[self.case_index]
        clean_options = re.sub(r'(?i)\?Seed=[^?]*', '', self.old_options or '')
        options = clean_options+'?Seed='+str(seed)
        self.mode.set_editor_property('OptionsString', options)
        self.require(str(prop(self.mode, 'options_string')) == options, 'PIE seed-option fixture was not accepted')
        self.case = dict(expected_first_pattern=order, seed=seed, clip='pattern_A_then_B' if order == 0 else 'pattern_B_then_A',
                         launches=[], warning_observations={}, physics_samples=[], passed=False)
        self.report['cases'].append(self.case)
        self.current_observed_launches = 0
        self.pc.request_dev_action(u.MCDevAction.NUT_ENCOUNTER, -1)
        self.event = None
        self.clip = None
        self.dodge_at = 0.
        self.stage('find_event')

    def accept_event(self):
        events = [e for e in self.actors(u.MCNutRainEvent) if e.actor_has_tag('MC_DevKeyEvent')]
        self.require(len(events) == 1, 'Real F3 must create exactly one owned NutRainEvent')
        self.event = events[0]
        self.require(not prop(self.event, 'failed'), 'Native pattern event failed during F3 creation')
        self.gs = u.GameplayStatics.get_game_state(self.world)
        self.require(int(prop(self.gs, 'run_seed')) == self.case['seed'], 'F3 RestartShift did not use the requested native seed')
        actual_order = int(prop(self.event, 'first_pattern'))
        self.require(actual_order == self.case['expected_first_pattern'], 'Native first-pattern choice disagrees with the seed fixture')
        settings = prop(self.event, 'settings')
        patterns = [[list((float(v.x), float(v.y))) for v in prop(settings, name)] for name in ('pattern_a', 'pattern_b')]
        times = [float(v) for v in prop(self.event, 'planned_launch_times')]
        groups = [int(v) for v in prop(self.event, 'planned_pattern_indices')]
        zones = [int(v) for v in prop(self.event, 'planned_zone_indices')]
        coordinates = [[float(v.x), float(v.y)] for v in prop(self.event, 'planned_zone_coordinates')]
        self.require(len(patterns[0]) == 10 and len(patterns[1]) == 8, 'Expected reference groups of10 and8 zones')
        self.require(len(times) == len(groups) == len(zones) == len(coordinates) == 18, 'Native schedule must contain exactly18 zones')
        self.require(len(set(zip(groups, zones))) == 18, 'A native pattern zone repeats')
        first_count = len(patterns[actual_order])
        self.require(groups == [actual_order]*first_count+[1-actual_order]*(18-first_count), 'Whole groups were interleaved or repeated')
        self.require(zones == list(range(first_count))+list(range(18-first_count)), 'Authored zone order changed inside a group')
        self.require(all(coordinates[i] == patterns[groups[i]][zones[i]] for i in range(18)), 'Plan coordinates differ from the authored zones')
        interval, gap = float(prop(settings, 'pattern_drop_seconds')), float(prop(settings, 'pattern_series_gap_seconds'))
        self.require(abs(interval-.5) < 1.e-5 and abs(gap-.9) < 1.e-5, 'Actual profile does not use0.5s launch /0.9s handover')
        self.require(all(abs((times[i]-times[i-1])-(gap if i == first_count else interval)) < .001
                         for i in range(1, 18)), 'Native planned cadence is incorrect')
        self.case.update(actual_first_pattern=actual_order, event=path(self.event), patterns=patterns,
            planned_launch_seconds=times, planned_pattern_indices=groups, planned_zone_indices=zones,
            planned_zone_coordinates=coordinates, group_boundary_index=first_count,
            drop_seconds=interval, series_gap_seconds=gap, flight_seconds=float(prop(prop(settings, 'entry'), 'flight_seconds')),
            nominal_rain_seconds=float(prop(settings, 'rain_seconds')),
            rain_started_at=float(prop(self.event, 'stage_started_at')), tooth_before=self.teeth_state())
        self.tongue = next(iter(self.actors(u.MCTongue)), None)
        self.require(valid(self.tongue), 'No live tongue')
        self.hero = self.pc.get_controlled_pawn()
        self.require(isinstance(self.hero, u.MCToothCharacter), 'F3 did not create the normal player pawn')
        self.overview()
        self.stage('storm')

    def teeth_state(self):
        out = []
        for tooth in self.actors(u.MCArenaTooth):
            state = prop(tooth, 'state')
            out.append(dict(path=path(tooth), tooth_id=int(prop(state, 'tooth_id')), health=float(prop(state, 'health')),
                            damage_stage=str(tooth.damage_stage())))
        return out

    def observe_launches(self, now):
        count = int(prop(self.event, 'nuts_spawned'))
        actual = [float(v) for v in prop(self.event, 'actual_launch_times')]
        self.require(count <= 18 and len(actual) == count, 'Launch count and actual launch telemetry diverged')
        nuts, shadows = list(prop(self.event, 'nuts')), list(prop(self.event, 'landing_shadows'))
        landings, starts = list(prop(self.event, 'planned_landings')), list(prop(self.event, 'launch_locations'))
        self.require(all(len(v) == count for v in (nuts, shadows, landings, starts)), 'Each successful launch must own one food, warning and landing')
        for index in range(self.current_observed_launches, count):
            food, warning = nuts[index], shadows[index]
            self.require(valid(food) and valid(warning), 'A newly observed native launch lacks its real food/warning actor')
            self.require(prop(food, 'nut_kinetics_enabled'), 'Storm walnut did not enable its physical kinetic mode')
            anchor = self.tongue.get_actor_transform().transform_location(prop(warning, 'surface_anchor'))
            target = landings[index]
            xy_error = math.hypot(anchor.x-target.x, anchor.y-target.y)
            launch_error = math.hypot(starts[index].x-target.x, starts[index].y-target.y)
            self.require(xy_error < .1 and launch_error < .1, 'Real warning anchor/vertical launch differs from the planned landing')
            self.require(warning.get_owner() == self.event, 'Landing warning is not owned by the real event')
            self.require(warning.is_warning_active(), 'New warning expired before it could advertise the landing')
            self.require(abs(float(prop(warning, 'flight_seconds'))-self.case['flight_seconds']) < .001, 'Warning flight duration changed')
            self.case['launches'].append(dict(index=index, pattern=self.case['planned_pattern_indices'][index],
                zone=self.case['planned_zone_indices'][index], actual_launch_seconds=actual[index],
                observed_game_seconds=now, observed_wall_seconds=time.monotonic()-self.started,
                food=path(food), warning=path(warning), landing_cm=vec(target), launch_cm=vec(starts[index]),
                warning_anchor_cm=vec(anchor), warning_xy_error_cm=xy_error, vertical_launch_error_cm=launch_error,
                warning_radius_cm=float(prop(warning, 'radius')), warning_seconds_remaining=float(warning.seconds_to_impact()),
                mass_kg=float(prop(food, 'body').get_mass()), kinetic_mode=True))
            self.case['warning_observations'][path(warning)] = dict(index=index, active_seen=True, visible_seen=False, max_strength=0.)
        self.current_observed_launches = count
        for warning in shadows:
            if not valid(warning):
                continue
            record = self.case['warning_observations'].get(path(warning))
            if record is not None:
                record['visible_seen'] |= bool(prop(warning, 'shadow').is_visible())
                record['max_strength'] = max(record['max_strength'], float(warning.get_shadow_strength()))
                record['active_seen'] |= bool(warning.is_warning_active())

    def sample(self, now):
        observations = []
        for food in prop(self.event, 'nuts'):
            if not valid(food):
                continue
            body = prop(food, 'body')
            observations.append(dict(path=path(food), position_cm=vec(food.get_actor_location()),
                velocity_cm_per_second=vec(food.get_velocity()), angular_velocity_degrees=vec(body.get_physics_angular_velocity_in_degrees()),
                mass_kg=float(body.get_mass()), phase=str(prop(food, 'phase')),
                kinetic_mode=bool(prop(food, 'nut_kinetics_enabled')), simulating=bool(body.is_simulating_physics()),
                collision_enabled=str(body.get_collision_enabled()), nut_tooth_impacts=int(prop(food, 'nut_tooth_impacts')),
                last_tooth_impact_energy_joule=float(prop(food, 'last_nut_impact_energy'))))
        entry = dict(game_seconds=now, wall_seconds=time.monotonic()-self.started,
            case=self.case['clip'], event_stage=str(prop(self.event, 'stage')),
            series=int(prop(self.event, 'series_index')), completed_series=int(prop(self.event, 'completed_series')),
            nuts_spawned=int(prop(self.event, 'nuts_spawned')), player_health=float(prop(prop(self.hero, 'status'), 'state').health),
            nuts=observations, teeth=self.teeth_state(), camera_evidence=self.camera_evidence())
        self.report['samples'].append(entry)
        self.case['physics_samples'].append(entry)

    def screenshot(self, name, sequence=False):
        if self.last_shot_tick == self.capture_tick:
            self.report.setdefault('skipped_shot_requests', []).append(dict(name=name, sequence=sequence,
                clip=self.clip, reason='One Shot is already pending this Slate tick; preserve the original request'))
            return
        evidence = self.camera_evidence()
        self.require(self.pc.get_view_target() == self.camera and evidence['location_error_cm'] < 2.
                     and evidence['rotation_error_degrees'] < 2., 'Actual PlayerCameraManager is not at the review camera')
        target = self.output/('frames' if sequence else 'screenshots')/name
        target.parent.mkdir(parents=True, exist_ok=True)
        self.console('Shot SHOWUI filename="'+str(target).replace('\\', '/')+'" -nosuffix')
        self.last_shot_tick = self.capture_tick
        record = dict(file=str(target), phase=self.phase, clip_window=self.clip,
            time=time.monotonic()-self.started, time_clock='wall_seconds',
            native_game_time=float(u.GameplayStatics.get_time_seconds(self.world)), camera_evidence=evidence,
            viewport_size=list(self.pc.get_viewport_size()), case=self.case['clip'] if self.case else None,
            nuts_spawned=int(prop(self.event, 'nuts_spawned')) if valid(self.event) else None)
        self.report['frames' if sequence else 'screenshots'].append(record)
        if sequence:
            self.report['clips'].setdefault(self.clip, []).append(record)

    def finish_case(self, now):
        self.observe_launches(now)
        self.sample(now)
        launches = self.case['launches']
        self.require(len(launches) == 18, 'Native storm did not emit every reference zone')
        actual = [v['actual_launch_seconds'] for v in launches]
        boundary = self.case['group_boundary_index']
        gaps = [b-a for a, b in zip(actual, actual[1:])]
        self.require(all(gaps[i-1] >= (.9 if i == boundary else .5)-.002 for i in range(1, 18)),
                     'A hitch caused a native burst or compressed reaction interval')
        self.require(all(v['visible_seen'] and v['max_strength'] > 0 for v in self.case['warning_observations'].values()),
                     'A native landing warning was never visibly active')
        self.require(int(prop(self.event, 'completed_series')) == 2, 'Boss gate opened before exactly two complete series')
        self.require(int(prop(self.event, 'bosses_left')) == 1, 'Final landing did not lead to one native knight')
        max_impacts = max((sum(n['nut_tooth_impacts'] for n in sample['nuts']) for sample in self.case['physics_samples']), default=0)
        self.case.update(passed=True, actual_gaps_seconds=gaps,
            maximum_interval_stretch_seconds=max(gaps[i-1]-(.9 if i == boundary else .5) for i in range(1, 18)),
            native_tooth_impacts_observed=max_impacts, tooth_damage_proven_by_counter=max_impacts > 0,
            tooth_after=self.teeth_state(), completed_game_seconds=now,
            boss_arrival_seconds=self.case['boss_arrival_game_seconds']-self.case['rain_started_at'])
        self.clip = None
        self.pc.request_dev_action(u.MCDevAction.STOP_NUT_ENCOUNTER, -1)
        self.stage('between_cases')

    def verify_frames(self):
        prune_unwritten_sequence_requests(self.report)
        required = {'pattern_A_then_B', 'pattern_B_then_A'}
        self.require(set(self.report['clips']) == required, 'Both required storm clips must exist')
        self.require(all(case.get('early_frame') for case in self.report['cases']), 'A case never adopted the actual review camera')
        for record in self.report['frames']+self.report['screenshots']:
            data = Path(record['file']).read_bytes()
            (width, height), png_end = inspect_png(data)
            self.require((width, height) in ((1280, 720), (1286, 760)), 'Wrong viewport captured; root must select the floating PIE window')
            record.update(width=width, height=height, sha256=hashlib.sha256(data).hexdigest(),
                          png_bytes=png_end, trailing_bytes=len(data)-png_end)
        for clip, frames in self.report['clips'].items():
            self.require(len(frames) >= 16, 'Required storm clip lacks16 genuine frames: '+clip)
            self.require(all(b['time']-a['time'] >= 1./8.-.005 for a, b in zip(frames, frames[1:])), 'Capture exceeded8Hz wall time')

    def tick(self, _delta):
        if self.done:
            return
        self.capture_tick += 1
        try:
            wall = time.monotonic()
            self.require(wall-self.started < 150., 'Nut pattern probe exceeded150 wall seconds')
            worlds = list(u.EditorLevelLibrary.get_pie_worlds(True))
            if self.world is None:
                if not worlds:
                    return
                self.require(len(worlds) == 1 and u.SystemLibrary.is_server(worlds[0]), 'Expected one solo authority PIE')
                pc = u.GameplayStatics.get_player_controller(worlds[0], 0)
                if pc is None or pc.get_controlled_pawn() is None:
                    return
                self.world, self.pc = worlds[0], pc
                self.hero = pc.get_controlled_pawn()
                self.mode = u.GameplayStatics.get_game_mode(self.world)
                self.old_options = str(prop(self.mode, 'options_string'))
                self.report['world'] = path(self.world)
                self.report['original_pie_options'] = self.old_options
                self.stage('first_perk')
                return
            self.require(self.owned_pie_is_active(worlds) and len(worlds) == 1, 'Owned PIE stopped or was replaced')
            self.hero = self.pc.get_controlled_pawn()
            now = float(u.GameplayStatics.get_time_seconds(self.world))
            # The real menu may pause the game below one second. Its actual
            # open state is readiness; never wait for game time to reach1s.
            if self.phase == 'first_perk':
                self.require(wall-self.phase_wall < 25., 'Normal first-perk menu did not open')
                if self.pc.is_reward_menu_open():
                    self.pc.choose_reward_perk(0)
                    self.report['checks']['normal_first_perk_choice'] = True
                    self.stage('perk_resolution')
                return
            if self.phase == 'perk_resolution':
                self.require(wall-self.phase_wall < 10., 'Normal first-perk choice did not resolve')
                if not self.pc.is_reward_menu_open():
                    self.request_case()
                return
            if self.phase == 'between_cases':
                if wall-self.phase_wall >= .4:
                    self.request_case()
                return
            if self.phase == 'capture_flush':
                if wall-self.flush_wall >= .8:
                    self.verify_frames()
                    self.require(len(self.report['cases']) == 2 and all(c['passed'] for c in self.report['cases']), 'Both whole-pattern orders were not checked')
                    self.report['checks'].update(both_native_whole_pattern_orders=True, all18_zones_once_each_case=True,
                        planned_half_second_and_nine_tenths_cadence=True, actual_no_burst_or_skipped_launch=True,
                        real_owned_warnings_visible_at_actual_landings=True, native_one_knight_after_two_groups=True,
                        kinetic_mode_on_real_storm_nuts=True, actual_camera_and_genuine_png_verified=True)
                    self.finish()
                return
            if self.phase == 'find_event':
                self.require(wall-self.phase_wall < 10., 'F3 opening did not become ready')
                self.accept_event()
                return
            self.require(valid(self.event) and not prop(self.event, 'failed'), 'Native F3 storm failed')
            self.require(isinstance(self.hero, u.MCToothCharacter) and prop(self.hero, 'status').is_alive(), 'Observer died; native damage was not changed')
            self.keep_review_camera()
            self.dodge(now)
            self.keep_review_camera()
            self.observe_launches(now)
            if wall >= self.sample_wall:
                self.sample_wall = wall+.1
                self.sample(now)
            evidence = self.camera_evidence()
            correct_camera = self.pc.get_view_target() == self.camera and evidence['location_error_cm'] < 2. and evidence['rotation_error_degrees'] < 2.
            if correct_camera and not self.case.get('early_frame'):
                self.screenshot(self.case['clip']+'_early.png')
                self.case['early_frame'] = self.report['screenshots'][-1]
                self.clip = self.case['clip']
                # Shot has one pending request. Preserve this still instead of
                # replacing it with a sequence filename in the same Slate tick.
                self.capture_wall = wall+1./max(1., self.capture_hz)
                self.write()
            if self.capture_hz and self.clip and correct_camera and wall >= self.capture_wall:
                self.capture_wall = wall+1./self.capture_hz
                self.screenshot('%05d.png' % len(self.report['frames']), True)
            if len(self.report['frames']) >= 2 and not self.report.get('dimensions_checked'):
                candidate = Path(self.report['frames'][0]['file'])
                if candidate.is_file():
                    data = candidate.read_bytes()
                    self.require((int.from_bytes(data[16:20], 'big'), int.from_bytes(data[20:24], 'big')) in ((1280,720),(1286,760)), 'Shot captured the wrong window')
                    self.report['dimensions_checked'] = True
            if self.phase == 'storm' and prop(self.event, 'stage') == u.MCNutRainStage.ENEMIES:
                self.case['boss_arrival_game_seconds'] = now
                self.arrival_wall = wall
                self.stage('boss_arrival')
            elif self.phase == 'boss_arrival' and wall-self.arrival_wall >= 1.:
                self.finish_case(now)
            self.require(wall-self.phase_wall < 65., 'A native storm phase exceeded its bounded time')
        except Exception:
            self.finish(traceback.format_exc())

    def finish(self, error=None):
        if self.done:
            return
        restore_errors = []
        if self.owned_pie_is_active():
            if valid(self.event) and self.event.actor_has_tag('MC_DevKeyEvent'):
                try:
                    self.pc.request_dev_action(u.MCDevAction.STOP_NUT_ENCOUNTER, -1)
                except Exception:
                    restore_errors.append(traceback.format_exc())
            if valid(self.mode) and self.old_options is not None:
                try:
                    self.mode.set_editor_property('OptionsString', self.old_options)
                    self.require(str(prop(self.mode, 'options_string')) == self.old_options, 'PIE OptionsString restore failed')
                except Exception:
                    restore_errors.append(traceback.format_exc())
        self.report['protected_packages_after'] = protected_packages()
        self.report['dirty_packages_after'] = dirty_packages()
        unchanged = self.report['protected_packages_after'] == self.report['protected_packages_before']
        preserved = self.report['dirty_packages_after'] == self.report['dirty_packages_before']
        self.report['checks'].update(protected_saved_packages_unchanged=unchanged, user_dirty_packages_preserved=preserved)
        if not unchanged or not preserved:
            error = error or 'Saved packages or editor dirty state changed during the probe'
        if restore_errors:
            self.report['probe_restore_errors'] = restore_errors
            error = error or 'Probe fixture restoration failed'
        super().finish(error)


def main():
    previous = getattr(builtins, KEY, None)
    if previous is not None and not getattr(previous, 'done', True):
        raise RuntimeError('A nut pattern probe is already active')
    probe = NutPatternKineticsSolo()
    setattr(builtins, KEY, probe)
    try:
        probe.start()
    except Exception:
        probe.finish(traceback.format_exc())
        raise


if __name__ == '__main__':
    main()
