"""Bounded solo PIE katana probe in a fresh owned L_Mouth editor session.

Uses the existing F3 tool grant and native primary-input press/release. Records
gameplay camera cases and brief front/side replays in owned PIE.
Launch with -AbsLog on E: so accepted blade contacts can be matched to each
fixture and checked against the native food split records.
Optional runpy globals set the trace verbosity, exact Slate capture rate, and
an external recording callback that starts and stops capture of the owned PIE.
Cancel via builtins.mc_katana_solo.finish('cancelled').
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
OUTPUT = Path(globals().get('MC_KATANA_SOLO_OUTPUT',
                            'E:/CodexWork/MessControl/Validation/KatanaRuntime/' + RUN)).resolve()
TRACE_VERBOSITY = int(globals().get('MC_KATANA_SOLO_TRACE_VERBOSITY', 1))
CAPTURE_HZ = float(globals().get('MC_KATANA_SOLO_CAPTURE_HZ', 15.))
START_WINDOW_RECORDING = globals().get('MC_KATANA_SOLO_START_WINDOW_RECORDING')
COMMAND = u.SystemLibrary.get_command_line()
assert all(flag in COMMAND.lower() for flag in ('-mchubowned', '-mchubtransientprofile', '-novoice', '-mcdevui'))
LOG_ARGUMENT = re.search(r'-abslog\s*=\s*(?:"([^"]+)"|([^\s"]+))', COMMAND, re.IGNORECASE)
assert LOG_ARGUMENT, 'The owned Editor must specify its actual -AbsLog=E:...'
LOGFILE = Path(LOG_ARGUMENT.group(1) or LOG_ARGUMENT.group(2)).resolve()
assert LOGFILE.drive.upper() == 'E:' and LOGFILE.is_file(), 'Use a readable Unreal log on E:'
assert OUTPUT.drive.upper() == 'E:' and not OUTPUT.exists(), 'Use a fresh E: output directory'
assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages(), 'Preserve unsaved maps'
assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages(), 'Preserve unsaved packages'
support = runpy.run_path(str(ROOT / 'Tools/Unreal/check_approved_environment_solo.py'),
                         init_globals={'MC_ENV_OUTPUT': OUTPUT, 'MC_ENV_CAPTURE_HZ': 0})
Base, prop = support['ApprovedEnvironmentSolo'], support['prop']
support['PLAY']['clientFixedFPS'] = [0]


def xyz(value):
    return [float(value.x), float(value.y), float(value.z)]


def dot(a, b):
    return math.fsum(x*y for x,y in zip(a,b))


NUMBER = r'([-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?)'
VECTOR = rf'\(?X={NUMBER}\s+Y={NUMBER}\s+Z={NUMBER}\)?'
CONTACT = re.compile(rf'MC_KATANA_CONTACT\s+attack=(\d+)\s+stroke=(\d+)\s+target=(\S+).*?'
                     rf'point={VECTOR}.*?normal={VECTOR}', re.IGNORECASE)
STAGED = re.compile(r'MC_FOOD_ISLANDS_STAGED\s+source=(\S+)\s+item=(\S+)\s+batch=(\d+)\s+'
                    r'parts=(\d+)\s+ready=(\d+)')
CONTACT_BASE = re.compile(rf'\bbase={VECTOR}', re.IGNORECASE)
CONTACT_TIP = re.compile(rf'\btip={VECTOR}', re.IGNORECASE)
CONTACT_DIRECTION = re.compile(rf'\bdirection={VECTOR}', re.IGNORECASE)
FOOD_SERIES_TARGETS = (('food', 127., -6., 53., 76.), ('food', 153., -27., 102., 48.))
ENEMY_SERIES_TARGETS = (('enemy', 127., 0., 72., 39.),)
CASES = (
    ('food_single', False, None, (('food', 124., 0., 58., 82.),)),
    ('enemy_single', False, None, (('enemy', 124., 0., 62., 37.),)),
    ('outside_sweep', False, None, (('food', 58., -23., 44., 24.), ('enemy', 58., 23., 45., 20.))),
    ('food_series', True, None, FOOD_SERIES_TARGETS),
    ('enemy_series', True, None, ENEMY_SERIES_TARGETS),
    ('food_series_front_review', True, 'front', FOOD_SERIES_TARGETS),
    ('enemy_series_side_review', True, 'side', ENEMY_SERIES_TARGETS),
)


class KatanaSolo(Base):
    def __init__(self):
        super().__init__()
        self.report.update(scope=__doc__, phases=[], requests=[], accepted_variants=[], target_hits=[],
                           fixtures=[], contacts=[], cut_results=[],
                           camera='Native gameplay camera, plus charged front and side reviews. HUD visible.',
                           limitations=['Native input handlers and CMC input are exercised in solo PIE.',
                                        'Frame evidence is sampled and requires visual review.',
                                        'Target placements are chosen from the authored active blade arc.',
                                        'Enemy contact uses its gameplay body. Food uses its ingredient mesh.'])
        self.capture_at = self.sample_at = 0.
        self.marked = None
        self.targets = []
        self.input_started = None
        self.variants = []
        self.busy = False
        self.case_index = -1
        self.case_log_offset = LOGFILE.stat().st_size
        self.trace_debug = None
        self.single_source_pose = None
        self.case_failures = []
        self.window_recording = None

    def write(self):
        (self.output / 'Report.json').write_text(json.dumps(self.report, ensure_ascii=False, indent=2,
                                                           allow_nan=False), encoding='utf8')

    def now(self):
        return float(u.GameplayStatics.get_time_seconds(self.world))

    def enter(self, phase):
        self.phase, self.phase_at = phase, self.now()
        self.report['phases'].append({'phase': phase, 'time': self.phase_at,
                                      'entered_utc': datetime.datetime.now(datetime.timezone.utc).isoformat()})
        u.log('MC_KATANA_PROBE_PHASE '+phase)
        self.marked = None
        self.write()

    def check(self, label, condition):
        self.report['checks'][label] = bool(condition)
        self.require(condition, label)

    def check_case(self, label, condition):
        self.report['checks'][label] = bool(condition)
        if not condition:
            self.case_failures.append(label)

    def ready(self):
        return self.pc.is_world_presentation_ready() and not self.pc.is_reward_menu_open() \
            and not self.pc.is_move_input_ignored() and not self.pc.is_boss_intro_playing()

    def tool(self):
        return next(c for c in self.hero.get_components_by_class(u.StaticMeshComponent)
                    if c.get_name() == 'InventoryTool')

    def attack_id(self):
        return int(self.hero.get_katana_attack_id())

    def socket(self, name):
        mesh = prop(self.hero, 'mesh')
        self.require(mesh.get_bone_index(name) >= 0, 'Review rig contains ' + name)
        transform = mesh.get_socket_transform(name, u.RelativeTransformSpace.RTS_WORLD)
        return dict(location=xyz(transform.translation), rotation=[float(x) for x in transform.rotation.to_tuple()])

    def sample(self):
        attack = self.attack_id()
        mesh = prop(self.hero, 'mesh')
        tool = self.tool()
        asset = prop(tool, 'static_mesh')
        tool_pose = tool.get_world_transform()
        row = dict(phase=self.phase, time=self.now(), attack=attack, position=xyz(self.hero.get_actor_location()),
                   velocity=xyz(self.hero.get_velocity()), tool_mesh=asset.get_path_name() if asset else None,
                   tool_visible=bool(tool.is_visible()), tool_transform=str(tool_pose),
                   mouth_effort=float(mesh.get_morph_target('Mouth_Effort')),
                   blink=float(mesh.get_morph_target('Eyes_Blink')),
                   pose={name: self.socket(name) for name in ('root_x','hand_r','hand_l','foot_l','foot_r')})
        if attack and self.hero.is_katana_equipped():
            row['visible_blade'] = dict(base=xyz(tool_pose.transform_location(u.Vector(3.75,0,11))),
                                        tip=xyz(tool_pose.transform_location(u.Vector(-10,0,95.8))),
                                        frame=int(u.SystemLibrary.get_frame_count()) % 1000,
                                        captured_utc=datetime.datetime.now(datetime.timezone.utc).isoformat())
        row['target_health'] = {}
        for target, fixture in self.targets:
            if not u.SystemLibrary.is_valid(target):
                continue
            health = float(prop(target, 'health'))
            name = fixture['actor']
            row['target_health'][name] = health
            if health < fixture['last_health'] - .001:
                self.report['target_hits'].append(dict(phase=self.phase,time=self.now(),attack=attack,
                                                      target=name,before=fixture['last_health'],after=health))
            fixture['last_health'] = health
        if attack and attack != self.marked:
            self.marked = attack
            self.report['accepted_variants'].append(dict(phase=self.phase,time=self.now(),attack=attack))
            if attack in (5, 6, 7):
                self.variants.append(attack)
        if not attack:
            self.marked = None
        self.report['samples'].append(row)
        return row

    def frame_review(self, side=False):
        center = self.hero.get_actor_location() + u.Vector(0, 0, 18)
        if side:
            location = center + self.hero.get_actor_forward_vector() * 130 - self.hero.get_actor_right_vector() * 315
        else:
            location = center + self.hero.get_actor_forward_vector() * 335 - self.hero.get_actor_right_vector() * 95
        self.frame_camera(location + u.Vector(0, 0, 95), center)

    def shoot(self, held):
        self.primary_input(held)
        self.report['requests'].append(dict(phase=self.phase,held=held,time=self.now()))

    def place_target(self, cls, forward, lateral, height, yaw):
        point = (self.hero.get_actor_location() + self.hero.get_actor_forward_vector() * forward
                 + self.hero.get_actor_right_vector() * lateral)
        support_point = self.floor(point)
        self.require(support_point is not None, 'Fixture has actual tongue support')
        pose = u.Transform(location=support_point + u.Vector(0, 0, height), rotation=u.Rotator(yaw=yaw))
        actor = self.statics.call_method('BeginDeferredActorSpawnFromClass', args=(self.world,
            cls.static_class(), pose, u.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN,
            None, u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        self.require(actor is not None, 'Could not allocate fixture ' + str(cls))
        return actor, pose

    def spawn_target(self, kind, forward, lateral, height, size):
        food = kind == 'food'
        batch = 281028 + self.case_index * 10
        cls = u.MCFoodActor if food else u.MCNutEnemy
        target, pose = self.place_target(cls, forward, lateral, height, 23. if food else 0.)
        if food:
            mesh = next((item for path in ('SM_TomatoA', 'SM_Broccoli')
                         if (item := u.load_asset('/Game/Stylized_Vegetables/Meshes/'+path))
                         and prop(item,'allow_cpu_access')), None)
            self.require(mesh is not None, 'Fixture ingredient provides real cut mesh')
            scale = size / (2. * max(xyz(mesh.get_bounds().box_extent)))
            data = prop(target, 'food_data')
            data.set_editor_properties(dict(kind=u.MCFoodKind.FOOD,scale=u.Vector(scale,scale,scale),
                                            resistance=u.MCFoodResistance.SOFT,
                                            health=1000.,spoil_seconds=600.))
            target.set_editor_properties(dict(food_data=data,item_name='KatanaSweepFood',
                item_mesh=mesh,fragment=False,spoiled=False,batch=batch,health=1000.))
        else:
            settings = u.MCNutEnemySettings()
            settings.set_editor_properties(dict(MaxHealth=1000., BodyRadius=size, MoveSpeed=40.))
            mesh = u.load_asset('/Game/Gameplay/CoreLoop/SM_Walnut_Whole')
            self.require(mesh is not None, 'Saved walnut mesh is available')
            scale = size / mesh.get_bounds().box_extent.z
            target.set_editor_properties(dict(Tongue=self.tongue,Settings=settings,Health=1000.,
                                               NutMesh=mesh,MeshScale=u.Vector(scale,scale,scale)))
        self.statics.call_method('FinishSpawningActor', args=(target, pose,
                                                            u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        if food:
            body = prop(target, 'body')
            body.set_simulate_physics(False)
            if self.phase=='food_single':
                settings = prop(target, 'settings')
                settings.set_editor_property('max_auto_consume_dimensions', u.Vector(12,12,12))
                target.set_editor_property('Settings', settings)
                self.single_source_pose = target.get_actor_transform()
        else:
            target.set_actor_tick_enabled(False)
        health = float(prop(target, 'health'))
        if food:
            center = target.get_actor_location()
            mesh_extent = mesh.get_bounds().box_extent * scale
            angle = math.radians(23.)
            cosine, sine = math.cos(angle), math.sin(angle)
            extent = u.Vector(mesh_extent.x*cosine+mesh_extent.y*sine,
                              mesh_extent.x*sine+mesh_extent.y*cosine, mesh_extent.z)
        else:
            center, extent = target.get_actor_bounds(True)
        fixture = dict(case=self.phase,kind=kind,actor=target.get_name(),batch=batch if food else None,mesh=mesh.get_path_name(),
                       center=xyz(center),extent=xyz(extent),spawn=xyz(target.get_actor_location()),
                       input_offset_cm=[forward,lateral,height],initial_health=health,last_health=health)
        self.targets.append((target,fixture))
        self.report['fixtures'].append(fixture)

    def clear_targets(self):
        batches = {fixture['batch'] for _, fixture in self.targets if fixture['batch'] is not None}
        for target, _ in self.targets:
            if u.SystemLibrary.is_valid(target):
                target.destroy_actor()
        if batches:
            for piece in self.actors(u.MCFoodActor):
                if u.SystemLibrary.is_valid(piece) and prop(piece, 'batch') in batches:
                    piece.destroy_actor()
        self.targets.clear()

    def start_next_case(self):
        self.clear_targets()
        self.case_index += 1
        if self.case_index >= len(CASES):
            self.inventory.server_select(u.MCToolSlot.BRUSH)
            self.enter('swap')
            return
        name, _, camera, targets = CASES[self.case_index]
        self.move_to(self.origin)
        self.case_log_offset = LOGFILE.stat().st_size
        self.input_started = None
        self.enter(name)
        for target in targets:
            self.spawn_target(*target)
        if camera is not None:
            self.frame_review(side=camera=='side')
        self.write()

    def read_case_log(self):
        targets = {fixture['actor']: fixture for _, fixture in self.targets}
        with LOGFILE.open('rb') as stream:
            stream.seek(self.case_log_offset)
            lines = stream.read().decode('utf-8', errors='replace').splitlines()
        contacts, cuts = [], []
        fixture_batches = {fixture['batch'] for fixture in targets.values() if fixture['batch'] is not None}
        for line in lines:
            match = CONTACT.search(line)
            if match and match[3] in targets:
                contact = dict(case=self.phase,attack=int(match[1]),stroke=int(match[2]),target=match[3],
                               point=[float(v) for v in match.groups()[3:6]],
                               normal=[float(v) for v in match.groups()[6:9]],raw=line)
                for field, pattern in (('base', CONTACT_BASE), ('tip', CONTACT_TIP),
                                       ('direction', CONTACT_DIRECTION)):
                    if endpoint := pattern.search(line):
                        contact[field] = [float(value) for value in endpoint.groups()]
                contacts.append(contact)
            match = STAGED.search(line)
            if match and int(match[3]) in fixture_batches:
                cuts.append(dict(case=self.phase,target=match[1],batch=int(match[3]),parts=int(match[4]),
                                 ready=int(match[5]),raw=line))
        return contacts, cuts

    def verify_single_food_cap(self, split_contacts, cuts):
        # Use the source pose so detached pieces can move after the split.
        # FVector::ToString in the contact log rounds to three decimals.
        for contact in split_contacts:
            batches = {cut['batch'] for cut in cuts if cut['target']==contact['target']}
            for piece in self.actors(u.MCFoodActor):
                if prop(piece, 'batch') not in batches:
                    continue
                meshes = piece.get_components_by_class(u.ProceduralMeshComponent)
                for mesh in meshes:
                    count = mesh.get_num_sections()
                    if count<1:
                        continue
                    vertices, triangles, *_ = u.ProceduralMeshLibrary.get_section_from_procedural_mesh(mesh, count-1)
                    if len(triangles)<3:
                        continue
                    relative = mesh.get_relative_transform()
                    positions = (self.single_source_pose.transform_location(relative.transform_location(vertices[index]))
                                 for index in set(triangles))
                    deviations = [abs(dot([axis-point for axis,point in zip(xyz(vertex),contact['point'])],
                                          contact['normal'])) for vertex in positions]
                    record = dict(piece=piece.get_name(),vertices=len(deviations),max_offset_cm=max(deviations))
                    contact.setdefault('cap_measurements', []).append(record)
        measurements = [cap for contact in split_contacts for cap in contact.get('cap_measurements', ())]
        self.check_case('Installed single-cut food cap matches the world contact plane', bool(measurements)
                        and all(cap['max_offset_cm']<.08 for cap in measurements))

    def verify_case(self):
        contacts, cuts = self.read_case_log()
        self.report['contacts'].extend(contacts)
        self.report['cut_results'].extend(cuts)
        _, charge, _, _ = CASES[self.case_index]
        variants = [record['attack'] for record in self.report['accepted_variants'] if record['phase']==self.phase]
        self.check_case(self.phase+' accepted the requested action', bool(variants)
                        and all(attack in ({8} if charge else {5,6,7}) for attack in variants))
        if self.phase=='outside_sweep':
            self.check_case('Nearby food and enemy outside the active blade sweep were untouched', not contacts
                            and not cuts and all(u.SystemLibrary.is_valid(target)
                                and float(prop(target, 'health'))==fixture['initial_health']
                                for target,fixture in self.targets))
            return
        self.check_case(self.phase+' logged a geometric blade contact', bool(contacts))
        if not contacts:
            return
        actors = {fixture['actor']: fixture for _, fixture in self.targets}
        on_bounds = unit_normals = plane_follows_blade = point_on_plane = True
        for contact in contacts:
            target = actors[contact['target']]
            on_bounds &= all(abs(p-c)<=e+3. for p,c,e in zip(contact['point'],target['center'],target['extent']))
            unit_normals &= abs(math.hypot(*contact['normal'])-1.)<.025
            axis = [tip-base for base,tip in zip(contact['base'],contact['tip'])]
            length = math.hypot(*axis)
            offset = [point-base for base,point in zip(contact['base'],contact['point'])]
            contact['blade_plane_deviation'] = abs(dot(axis,contact['normal']))/max(length,.001)
            contact['blade_plane_offset_cm'] = abs(dot(offset,contact['normal']))
            plane_follows_blade &= length>1. and contact['blade_plane_deviation']<.08
            point_on_plane &= contact['blade_plane_offset_cm']<.75
        self.check_case(self.phase+' point lies on the target bounds', on_bounds)
        self.check_case(self.phase+' has a finite unit contact plane', unit_normals)
        self.check_case(self.phase+' plane follows the blade axis', plane_follows_blade)
        if self.phase.startswith('food'):
            split_targets = {cut['target'] for cut in cuts if cut['parts']>=2}
            split_contacts = [contact for contact in contacts if contact['target'] in split_targets]
            if self.phase=='food_single':
                self.check_case('One ordinary stroke did not recut its newly separated pieces', len(cuts)==1)
            self.check_case(self.phase+' contact lies in the moving blade plane', point_on_plane)
            self.check_case(self.phase+' committed a mesh split at accepted contact', bool(split_contacts))
            if self.phase=='food_single':
                self.verify_single_food_cap(split_contacts, cuts)
            else:
                self.check_case(self.phase+' cut distinct source actors from the same batch',
                                len({contact['target'] for contact in split_contacts})>=2
                                and len({cut['batch'] for cut in cuts})==1)
                angles = [math.degrees(math.acos(min(1., abs(dot(a['normal'],b['normal'])))))
                          for index,a in enumerate(split_contacts) for b in split_contacts[index+1:]
                          if a['stroke']!=b['stroke']]
                self.report.setdefault('charged_food_plane_angles_deg', {})[self.phase] = angles
                self.check_case(self.phase+' split food at different blade angles', any(angle>8. for angle in angles))
        else:
            self.check_case(self.phase+' applied damage to the struck enemy', all(
                float(prop(target, 'health'))<fixture['initial_health'] for target, fixture in self.targets))
        pairs = {(contact['target'],contact['stroke']) for contact in contacts}
        self.check_case(self.phase+' hits each target at most once per stroke', len(pairs)==len(contacts))
        if self.phase.startswith('enemy_series'):
            self.check_case(self.phase+' received all three distinct strokes', {contact['stroke'] for contact in contacts}=={0,1,2})

    def finish(self, error=None):
        if self.window_recording is not None:
            try:
                self.report['window_recording'] = self.window_recording()
            except Exception:
                self.report['window_recording_error'] = traceback.format_exc()
            self.window_recording = None
        if self.trace_debug is not None:
            try:
                self.console(f'mc.Katana.TraceDebug {self.trace_debug}')
            except Exception:
                error = error or traceback.format_exc()
            self.trace_debug = None
        super().finish(error)

    def tick(self, _delta):
        if self.done or self.busy:
            return
        self.busy = True
        try:
            self.require(time.monotonic() - self.started < 180., 'Solo probe reached its wall-time limit')
            if self.pie_start is not None:
                self.require(not prop(self.pie_start,'Error'), 'StartPIE: ' + str(prop(self.pie_start,'Error')))
            worlds = list(u.EditorLevelLibrary.get_pie_worlds(True))
            if self.world is None:
                if not worlds:
                    return
                self.require(len(worlds)==1 and u.SystemLibrary.is_server(worlds[0]), 'One authoritative solo PIE')
                pc = u.GameplayStatics.get_player_controller(worlds[0],0)
                if pc is None or pc.get_controlled_pawn() is None:
                    return
                self.world, self.pc, self.hero = worlds[0], pc, pc.get_controlled_pawn()
                self.require(isinstance(self.hero,u.MCToothCharacter), 'Gameplay tooth pawn')
                self.require(hasattr(self.hero,'get_katana_attack_id'), 'Katana native diagnostic API present in build')
                self.inventory = self.hero.get_component_by_class(u.MCInventoryComponent)
                self.tongue, = self.actors(u.MCTongue)
                self.engine.set_editor_property('bUseFixedFrameRate', False)
                self.enter('readiness')
                return
            self.require(len(worlds)==1 and self.owned_pie_is_active(worlds), 'Owned solo PIE stays active')
            now = self.now()
            age = now - self.phase_at
            if self.phase == 'readiness':
                if self.pc.is_reward_menu_open():
                    self.pc.choose_reward_perk(0)
                if age >= 1. and self.ready():
                    status = u.MCRoguelikeEditorLibrary.review_rogue_gameplay(self.world, 'level')
                    self.require(not status.startswith('REJECT'), status)
                    self.enter('arrival')
                return
            if self.phase == 'arrival':
                if self.pc.is_reward_menu_open():
                    self.pc.choose_reward_perk(0)
                if age < 1. or not self.ready():
                    return
                self.origin = self.floor(self.hero.get_actor_location())
                self.require(self.origin is not None, 'Native tongue support at probe origin')
                self.move_to(self.origin)
                self.pc.toggle_dev_panel()
                try:
                    self.pc.request_dev_action(u.MCDevAction.GRANT_TOOL_BOOSTER, 2)
                finally:
                    self.pc.toggle_dev_panel()
                self.inventory.server_select(u.MCToolSlot.KNIFE)
                self.trace_debug = u.SystemLibrary.get_console_variable_int_value('mc.Katana.TraceDebug')
                self.console(f'mc.Katana.TraceDebug {TRACE_VERBOSITY}')
                if START_WINDOW_RECORDING:
                    try:
                        self.window_recording = START_WINDOW_RECORDING(self.output)
                    except Exception:
                        self.report['window_recording_error'] = traceback.format_exc()
                self.slate_capture_hz = CAPTURE_HZ if self.window_recording else 15.
                self.enter('equip')
                return
            if now >= self.sample_at:
                self.sample_at = now + 1./30.
                self.sample()
            if self.slate_capture_hz>0. and now >= self.capture_at:
                self.capture_at = now + 1./self.slate_capture_hz
                self.screenshot('%04d.png' % len(self.report['frames']), sequence=True)
            if self.phase == 'equip' and age > .5:
                self.check('F3 grant and knife selection equipped katana', self.hero.is_katana_equipped())
                asset = prop(self.tool(), 'static_mesh')
                self.check('Actual katana static mesh visible', asset and 'katana' in asset.get_name().lower()
                           and self.tool().is_visible())
                self.enter('walk')
            elif self.phase in ('walk','moving_cut'):
                self.hero.add_movement_input(self.hero.get_actor_forward_vector(), 0.65, False)
                if self.phase == 'walk' and age > .45:
                    self.check('Normal movement remains active while holding katana', self.hero.get_velocity().length() > 10.)
                    self.shoot(True)
                    self.enter('moving_cut')
                elif self.phase == 'moving_cut' and self.primary and age > .09:
                    self.shoot(False)
                elif self.phase == 'moving_cut' and age > 1.1:
                    self.check('Tap during walking accepted a single cut', bool(self.variants))
                    during = [r for r in self.report['samples'] if r['phase']=='moving_cut' and r['attack']]
                    self.check('Player continues to move during accepted cut', any(math.hypot(*r['velocity'][:2]) > 10. for r in during))
                    self.start_next_case()
            elif 0 <= self.case_index < len(CASES) and self.phase == CASES[self.case_index][0]:
                _, charge, _, _ = CASES[self.case_index]
                hold_seconds = .65 if charge else .075
                if self.input_started is None and age > .15:
                    self.input_started = now
                    self.shoot(True)
                elif self.primary and now - self.input_started > hold_seconds:
                    self.shoot(False)
                elif age > (2.35 if charge else 1.5):
                    self.verify_case()
                    self.start_next_case()
            elif self.phase == 'swap' and age > .35:
                self.check('Normal brush selection releases katana animation ownership',
                           not self.hero.is_katana_equipped() and self.attack_id()==0)
                self.check('Visible mouth effort observed on an accepted action', any(
                    row['attack'] and row['mouth_effort'] > .05 for row in self.report['samples']))
                self.report['ordinary_variant_ids_observed'] = sorted(set(self.variants))
                self.finish('\n'.join(self.case_failures) or None)
        except Exception:
            self.finish(traceback.format_exc())
        finally:
            self.busy = False


if __name__ == '__main__':
    previous = getattr(builtins,'mc_katana_solo',None)
    assert previous is None or previous.done, 'Katana probe already owns PIE'
    probe = KatanaSolo()
    builtins.mc_katana_solo = probe
    try:
        probe.start()
    except Exception:
        probe.finish(traceback.format_exc())
        raise
