"""Short solo check of real locomotion after a fresh editor-module load.

Movement, character tick, procedural animation and material updates stay live.
Only a local cosmetic preview is used; no purchases or profile changes are made.
"""
import builtins
import json
import shutil
import time
import traceback
from pathlib import Path
import unreal as u

ROOT = Path(u.Paths.project_dir()).resolve()
OUT = ROOT / 'Saved/HeadCosmetics20261024/WalkFollow'
ART = ROOT / 'Artifacts/HeadCosmetics20261024/WalkFollow'
OUT.mkdir(parents=True, exist_ok=True)
ART.mkdir(parents=True, exist_ok=True)
LS = u.get_editor_subsystem(u.LevelEditorSubsystem)
assert not LS.is_in_play_in_editor()
SETTINGS = u.load_object(None, '/Script/UnrealEd.Default__LevelEditorPlaySettings')
PERF = u.get_default_object(u.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings'))
SOLO = {'playNetMode': 'PIE_Standalone', 'playNumberOfClients': 1,
        'runUnderOneProcess': True, 'bLaunchSeparateServer': False}
BEFORE = {'play': json.loads(u.ToolsetLibrary.get_object_properties(SETTINGS, list(SOLO))),
          'throttle': PERF.get_editor_property('bThrottleCPUWhenNotForeground'),
          'profiles': {}}
PROFILES = ROOT / 'Saved/HubProfiles/PIE'
for source in PROFILES.glob('*'):
    if source.is_file():
        destination = OUT / 'BeforeProfiles' / source.name
        destination.parent.mkdir(exist_ok=True)
        shutil.copy2(source, destination)
        BEFORE['profiles'][source.name] = str(destination)
(OUT / 'BeforeWalking.json').write_text(json.dumps(BEFORE, indent=2))
assert u.ToolsetLibrary.set_object_properties(SETTINGS, json.dumps(SOLO))
PERF.set_editor_property('bThrottleCPUWhenNotForeground', False)

GROUPS = [
    ('ArtistCap', 'SquareGlasses'), ('CowboyHat', 'Brows'),
    ('ConeHat', None), ('Feather', None), ('FootballHelmet', None),
]
ITEMS = {item['slug']: item for item in json.loads(
    (OUT.parent / 'Integration.json').read_text(encoding='utf8'))['items']}


class Check:
    def __init__(self):
        self.start = time.monotonic()
        self.phase = 'travel'
        self.sent = False
        self.index = 0
        self.samples = []
        self.cpd = []
        self.trace_steps = set()
        self.inside = False
        self.callback = u.register_slate_post_tick_callback(self.tick)

    def finish(self, error=None):
        u.unregister_slate_post_tick_callback(self.callback)
        if hasattr(self, 'cosmetics'):
            self.cosmetics.restore_authoritative_loadout()
        result = {'status': 'failed' if error else 'passed', 'error': error,
                  'phase': self.phase, 'elapsed': time.monotonic() - self.start,
                  'samples': self.samples, 'cpd_samples': self.cpd,
                  'fixtures': ['Real pawn AddMovementInput; movement and animation stay enabled.',
                               'Local PreviewLoadout on actual pawn; no purchase or ownership mutation.',
                               'Native screenshot callback guarded against Slate reentrancy.'],
                  'fresh_editor_process': True, 'network_test': False}
        (OUT / 'WalkingValidation.json').write_text(
            json.dumps(result, ensure_ascii=False, indent=2), encoding='utf8')
        u.log('HEAD_COSMETICS_WALKING ' + result['status'])
        LS.editor_request_end_play()

    def tick(self, dt):
        if self.inside:
            return
        self.inside = True
        try:
            self.run(dt)
        except Exception:
            self.finish(traceback.format_exc())
        finally:
            self.inside = False

    def run(self, dt):
        assert time.monotonic() - self.start < 150, self.phase
        worlds = u.EditorLevelLibrary.get_pie_worlds(False)
        if not worlds:
            return
        if self.phase == 'travel':
            world = worlds[0]
            if u.GameplayStatics.get_current_level_name(world, True) != 'Hub':
                if not self.sent:
                    u.GameplayStatics.open_level(world, '/Game/Maps/Hub')
                    self.sent = True
                return
            self.world = world
            self.phase = 'ready'
            return
        now = u.GameplayStatics.get_time_seconds(self.world)
        if self.phase == 'ready':
            preload = next(s for s in u.ObjectIterator(u.MCWorldPreloadSubsystem)
                           if s.get_world() == self.world)
            assert not preload.has_failed()
            if not preload.is_ready():
                return
            self.hero = u.GameplayStatics.get_player_pawn(self.world, 0)
            if not isinstance(self.hero, u.MCToothCharacter):
                return
            self.cosmetics = self.hero.get_editor_property('Cosmetics')
            self.body = self.hero.mesh
            self.phase = 'equip'
        if self.phase == 'equip':
            loadout = u.MCCharacterCosmeticLoadout()
            self.active = [ITEMS[s] for s in GROUPS[self.index] if s]
            for item in self.active:
                loadout = u.MCCosmeticCatalog.with_equipped_item(loadout, item['slot'], item['id'])
            assert self.cosmetics.preview_loadout(loadout)
            assert self.cosmetics.is_component_tick_enabled()
            assert self.hero.is_actor_tick_enabled()
            assert self.body.is_component_tick_enabled()
            self.phase = 'walk'
            self.step_at = now
            self.trace_steps = set()
            self.origin = self.hero.get_actor_location()
            return
        if self.phase == 'walk':
            elapsed = now - self.step_at
            # A short out-and-back route with a real 180-degree movement turn.
            direction = u.Vector(1, 0, 0) if elapsed < 1.6 else u.Vector(-1, 0, 0)
            self.hero.add_movement_input(direction, 1.0, True)
            head = self.body.get_socket_transform('head_x', u.RelativeTransformSpace.RTS_WORLD)
            actor = self.hero.get_actor_transform()
            stretch = self.body.get_material(0).get_scalar_parameter_value('BodyStretch')
            parts = self.cosmetics.get_accessory_parts()
            assert len(parts) == len(self.active)
            errors = []
            for part in parts:
                assert part.get_attach_parent() == self.body
                assert str(part.get_attach_socket_name()) == 'head_x'
                expected = u.MathLibrary.compose_transforms(part.get_relative_transform(), head)
                errors.append((part.get_world_location() - expected.translation).length())
                assert errors[-1] < .01, errors
            self.samples.append({'group': self.index, 'items': [i['id'] for i in self.active],
                                 'time': now, 'elapsed': elapsed,
                                 'speed_cm_s': self.hero.get_velocity().length(),
                                 'actor': list(actor.translation.to_tuple()),
                                 'head_in_actor': list(actor.inverse_transform_location(head.translation).to_tuple()),
                                 'head_rotation': list(head.rotation.to_tuple()),
                                 'stretch': stretch, 'attachment_errors_cm': errors,
                                 'distance_from_start_cm': (actor.translation - self.origin).length()})
            for step, threshold in enumerate([.7, 1.3, 2.1, 2.7]):
                if elapsed >= threshold and step not in self.trace_steps:
                    self.trace_steps.add(step)
                    frame = self.body.get_world_transform()
                    for part in parts:
                        label = str(self.index) + ':' + str(step) + ':' + part.get_name()
                        row = {'label': label, 'part': part.get_path_name(),
                               'expected': [stretch, *frame.translation.to_tuple(), 0.,
                                            *frame.rotation.to_tuple(), *frame.scale3d.to_tuple(), 0.]}
                        self.cpd.append(row)
                        u.log('WALK_CPD_SAMPLE ' + json.dumps(row))
                        u.SystemLibrary.execute_console_command(self.world,
                            'getall StaticMeshComponent CustomPrimitiveDataInternal OUTER=' +
                            self.hero.get_path_name() + ' NAME=' + part.get_name())
                    if self.index == 0 and step in [0, 1, 2]:
                        u.AutomationLibrary.take_high_res_screenshot(
                            1600, 900, str(ART / ('ArtistCap_Walk_' + str(step) + '.png')))
                    break
            if elapsed < 3.3:
                return
            moving = [s for s in self.samples if s['group'] == self.index and s['speed_cm_s'] > 50]
            assert len(moving) >= 8, ('No actual walking', self.index, len(moving))
            assert max(s['distance_from_start_cm'] for s in moving) > 100
            assert max(s['stretch'] for s in moving) - min(s['stretch'] for s in moving) > .01
            self.index += 1
            if self.index < len(GROUPS):
                self.phase = 'equip'
            else:
                assert {i for s in self.samples for i in s['items']} == {i['id'] for i in ITEMS.values()}
                self.phase = 'done'
                self.finish()


builtins.mc_head_cosmetics_walking = Check()
LS.editor_request_begin_play()
print('HEAD_COSMETICS_WALKING_STARTED')
