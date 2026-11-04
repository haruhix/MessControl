"""Readonly observer for existing owned standalone PIE world-name/WorldLabel camera and F3 QA.

Run ONLY after the parent releases its app lease and Build06/classes/assets are ready.
This script never starts/stops PIE, sets camera/pawn/UI, presses keys, changes culture,
spawns, applies damage, travels, saves packages/preferences, calls TakeWidget or forces GC.
The parent drives normal camera/movement and physical F3 panel/button externally.
Only this bounded <=60s Slate callback and E: reports/PNG captures are owned here.

Cancel: builtins.mc_world_camera_qa.cancel().
Optional report-only case marker/capture: builtins.mc_world_camera_qa.mark('near').
No FGeometry reflection: actual native reflected WorldLabel diagnostics, native getters,
real leaf text and actual game-window captures are recorded. Artistic/pixel acceptance
and exact rig-head socket mapping are explicitly separate from these source checks.
"""
import builtins
import hashlib
import json
import math
import os
import sys
import time
import traceback
from pathlib import Path
import unreal as u

sys.dont_write_bytecode = True
os.environ['TEMP'] = os.environ['TMP'] = 'E:/CodexTemp/MessControl-UIHubReward'
KEY = 'mc_world_camera_qa'
MAX_SECONDS = min(60., max(5., float(globals().get('MC_WORLD_CAMERA_SECONDS', 60.))))
OUT = Path(globals().get('MC_WORLD_CAMERA_OUTPUT',
    'E:/CodexWork/MessControl/Validation/UIOverlayWorld_20261104/Camera/' + time.strftime('%Y%m%dT%H%M%SZ', time.gmtime())))
assert OUT.resolve().drive.upper() == 'E:', 'Output must remain on E:'
OUT.mkdir(parents=True, exist_ok=True)
CAPTURE_PNG = bool(globals().get('MC_WORLD_CAMERA_CAPTURE_PNG', True))


def live(obj):
    return obj is not None and u.SystemLibrary.is_valid(obj)


def path(obj):
    return obj.get_path_name() if live(obj) else None


def prop(obj, name):
    return obj.get_editor_property(name)


def vector(value):
    return [float(value.x), float(value.y), float(value.z)]


def xy(value):
    return [float(value.x), float(value.y)]


def owned_tree(owner):
    for name in ('WidgetTree_0', 'WidgetTree'):
        found = u.find_object(owner, name)
        if isinstance(found, u.WidgetTree) and found.get_outer() == owner:
            return found
    matches = [x for x in u.ObjectIterator(u.WidgetTree) if x.get_outer() == owner]
    assert len(matches) == 1, ('Missing/ambiguous tree', path(owner))
    return matches[0]


def named(owner, name, cls=u.Widget):
    found = u.find_object(owned_tree(owner), name)
    return found if isinstance(found, cls) else None


def version_name():
    actual = u.MCGameplayUIFactory.get_presentation_version()
    if actual == u.MCUIVersion.NEW:
        return 'NEW'
    if actual == u.MCUIVersion.CURRENT:
        return 'CURRENT'
    raise AssertionError('Unexpected typed UI version: ' + str(actual))


def projection(pc, anchor):
    result = u.GameplayStatics.project_world_to_screen(pc, anchor, True)
    if isinstance(result, u.Vector2D):
        return {'success': True, 'pixel': xy(result)}
    if result is None:
        return {'success': False, 'pixel': None}
    if isinstance(result, tuple):
        points = [x for x in result if isinstance(x, u.Vector2D)]
        flags = [x for x in result if isinstance(x, bool)]
        assert len(points) == 1 and len(flags) <= 1, 'Unexpected typed projection result'
        return {'success': all(flags), 'pixel': xy(points[0])}
    raise AssertionError('Unexpected ProjectWorldToScreen return type: ' + str(type(result)))


class WorldCameraObserver:
    def __init__(self):
        self.callback = None
        self.started = time.monotonic()
        self.last_sample = self.last_discovery = self.last_write = -1.
        self.world = self.pc = self.camera = None
        self.labels = []
        self.names = []
        self.version = None
        self.version_since = self.started
        self.current_flags = {}
        self.scale_ranges = {}
        self.occluded_sources = set()
        self.clear_after_occluded = set()
        self.behind_sources = set()
        self.issue_runs = {}
        self.frames = 0
        self.report = {'status': 'PREFLIGHT', 'scope': __doc__, 'passed': False,
            'samples': [], 'markers': [], 'captures': [], 'transitions': [], 'violations': [],
            'inputOwned': False, 'stateMutations': [], 'hardwareInputByObserver': False,
            'nativeGeometryPayloadRead': False, 'sourceScriptSHA256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            'maxSeconds': MAX_SECONDS, 'limitations': ['No FGeometry reflected-field assertions.',
                'RigBone is native-only; exact head-socket anchoring requires native telemetry or retained camera frames.',
                'Collision-based occlusion has <=.2s native cache latency; renderer-only noncolliding surfaces need visual review.',
                'No cold shader/PSO/readiness claim from a warm Editor run.']}

    def write(self):
        self.report['wallSeconds'] = time.monotonic() - self.started
        (OUT / 'WorldCameraReport.json').write_text(json.dumps(self.report, ensure_ascii=False, indent=2), encoding='utf-8')

    def capture(self, label):
        if not CAPTURE_PNG or self.frames >= 12:
            return
        self.frames += 1
        target = OUT / (str(self.frames).zfill(2) + '_' + ''.join(c if c.isalnum() else '_' for c in label)[:36] + '.png')
        ok = bool(u.MCGameplayUIEditorLibrary.capture_pie_game_window(self.world, str(target)))
        self.report['captures'].append({'label': label, 'file': str(target), 'captured': ok, 'wall': time.monotonic() - self.started})

    def mark(self, label):
        self.report['markers'].append({'label': str(label), 'wall': time.monotonic() - self.started})
        self.capture(str(label))
        self.write()

    def discover(self):
        self.labels = [x for x in u.ObjectIterator(u.MCWorldLabelPresenterComponent) if live(x) and x.get_world() == self.world]
        self.labels.sort(key=lambda x: (0 if 'MCHubReadyZone' in path(x.get_owner()) else 1, path(x)))
        self.labels = self.labels[:24]
        self.names = [x for x in u.ObjectIterator(u.MCPlayerNameComponent) if live(x) and x.get_world() == self.world][:8]

    def issue(self, key, bad, payload, now):
        if not bad:
            self.issue_runs.pop(key, None)
            return
        first = self.issue_runs.setdefault(key, now)
        if now - first >= .4 and not any(x['key'] == key for x in self.report['violations']):
            self.report['violations'].append({'key': key, 'payload': payload, 'wall': now - self.started})
            self.capture('Violation_' + key)

    def label_sample(self, component, ver, settled, now):
        source = prop(component, 'SourceText')
        if not live(source) or source.get_world() != self.world:
            return {'component': path(component), 'source': None}
        view = component.get_user_widget_object()  # Getter only; never InitWidget/TakeWidget.
        caption = named(view, 'WorldLabelText', u.TextBlock) if isinstance(view, u.MCWorldLabelWidget) else None
        flags = {k: bool(prop(source, k)) for k in ('bRenderInMainPass', 'bRenderInDepthPass', 'bRenderCustomDepth', 'CastShadow')}
        data = {'component': path(component), 'source': path(source), 'owner': path(source.get_owner()),
            'sourceText': str(prop(source, 'Text')), 'sourceVisible': source.is_visible(),
            'sourceHidden': bool(prop(source, 'bHiddenInGame')), 'sourceFlags': flags,
            'sourceLocation': vector(source.get_world_location()), 'worldSize': float(prop(source, 'WorldSize')),
            'componentVisible': component.is_visible(), 'view': path(view),
            'copy': str(caption.get_text()) if caption is not None else None,
            'scale': float(prop(component, 'LastPerspectiveScale')),
            'projectedLinePixels': float(prop(component, 'LastProjectedLinePixels')),
            'projectionInView': bool(prop(component, 'bLastProjectionInView')),
            'occlusionValid': bool(prop(component, 'bOcclusionCacheValid')),
            'occluded': bool(prop(component, 'bLastOccluded')),
            'projection': projection(self.pc, source.get_world_location())}
        if view is not None:
            data['renderScale'] = xy(prop(view, 'RenderTransform').scale)
            data['desiredSize'] = xy(view.get_desired_size())
        eye = self.camera.get_camera_location();rot = self.camera.get_camera_rotation()
        yaw, pitch = math.radians(float(rot.yaw)), math.radians(float(rot.pitch))
        forward = (math.cos(pitch) * math.cos(yaw), math.cos(pitch) * math.sin(yaw), math.sin(pitch))
        delta = [data['sourceLocation'][i] - vector(eye)[i] for i in range(3)]
        data['cameraFrontDot'] = sum(delta[i] * forward[i] for i in range(3))
        key = path(component)
        if ver == 'NEW' and settled:
            if data['cameraFrontDot'] <= 0:
                self.behind_sources.add(key)
                self.issue('BehindVisible:' + key, data['componentVisible'], data, now)
            if data['occlusionValid'] and data['occluded']:
                self.occluded_sources.add(key)
            if key in self.occluded_sources and data['occlusionValid'] and not data['occluded'] and data['componentVisible']:
                self.clear_after_occluded.add(key)
        if ver == 'CURRENT' and settled:
            if key not in self.current_flags:
                self.current_flags[key] = flags.copy()
            self.issue('CurrentFlags:' + key, flags != self.current_flags[key], data, now)
            self.issue('CurrentDuplicate:' + key, data['componentVisible'], data, now)
        if ver == 'NEW' and settled and data['componentVisible']:
            self.issue('NewVisibility:' + key, not (data['projectionInView'] and data['occlusionValid'] and not data['occluded']), data, now)
            self.issue('NewLease:' + key, any(flags.values()), data, now)
            self.issue('NewCopy:' + key, data['copy'] != data['sourceText'], data, now)
            self.issue('NewScale:' + key, not (math.isfinite(data['scale']) and data['scale'] > 0 and data.get('renderScale') and abs(data['renderScale'][0] - data['scale']) < .001), data, now)
            lo, hi = self.scale_ranges.get(key, (data['scale'], data['scale']))
            self.scale_ranges[key] = (min(lo, data['scale']), max(hi, data['scale']))
        return data

    def name_sample(self, component, ver, settled, now):
        owner = component.get_owner()
        view = component.get_user_widget_object()
        if not isinstance(view, u.MCPlayerNameWidget):
            return {'component': path(component), 'view': path(view)}
        state = prop(owner, 'PlayerState') if isinstance(owner, u.Pawn) else None
        expected = state.get_player_name() if state is not None and state.get_pawn() == owner else ''
        current = named(view, 'PlayerNameText', u.TextBlock)
        new = named(view, 'WorldNameV2Copy', u.TextBlock)
        switcher = named(view, 'WorldPresentationVersions', u.WidgetSwitcher)
        data = {'component': path(component), 'owner': path(owner), 'view': path(view),
            'expectedName': expected, 'displayedName': component.get_displayed_name(),
            'currentCopy': str(current.get_text()) if current else None, 'newCopy': str(new.get_text()) if new else None,
            'branch': switcher.get_active_widget_index() if switcher else None,
            'optIn': bool(prop(view, 'bUseVersionedWorldPresentation')), 'componentVisible': component.is_visible(),
            'worldLocation': vector(component.get_world_location()), 'heightAboveHead': float(prop(component, 'HeightAboveHead')),
            'drawSize': xy(component.get_draw_size()), 'desiredSize': xy(view.get_desired_size()),
            'drawAtDesiredSize': bool(prop(component, 'bDrawAtDesiredSize')), 'pivot': xy(component.get_pivot()),
            'projection': projection(self.pc, component.get_world_location())}
        key = path(component)
        self.issue('NameSource:' + key, data['displayedName'] != expected or data['currentCopy'] != expected or data['newCopy'] != expected, data, now)
        if settled and data['optIn']:
            self.issue('NameBranch:' + key, data['branch'] != (1 if ver == 'NEW' else 0), data, now)
        return data

    def sample(self, now):
        ver = version_name()
        if ver != self.version:
            self.version = ver;self.version_since = now
            self.report['transitions'].append({'version': ver, 'wall': now - self.started})
            self.capture('Version_' + ver)
        settled = now - self.version_since >= .3
        eye = self.camera.get_camera_location();rot = self.camera.get_camera_rotation()
        data = {'wall': now - self.started, 'version': ver, 'settled': settled,
            'ready': self.pc.is_world_presentation_ready(), 'pawn': path(self.pc.get_controlled_pawn()),
            'camera': {'location': vector(eye), 'rotation': [float(rot.pitch), float(rot.yaw), float(rot.roll)], 'fov': float(self.camera.get_fov_angle())},
            'moveIgnored': self.pc.is_move_input_ignored(), 'lookIgnored': self.pc.is_look_input_ignored(),
            'labels': [self.label_sample(x, ver, settled, now) for x in self.labels if live(x) and x.get_world() == self.world],
            'names': [self.name_sample(x, ver, settled, now) for x in self.names if live(x) and x.get_world() == self.world]}
        self.report['samples'].append(data)

    def finish(self, status):
        if self.callback is not None:
            u.unregister_slate_post_tick_callback(self.callback);self.callback = None
        versions = {x['version'] for x in self.report['transitions']}
        evidence = {'hasCurrentAndNew': {'CURRENT', 'NEW'}.issubset(versions),
            'currentBaselineCount': len(self.current_flags), 'nameComponentCount': len(self.names),
            'visibleNewPerspectiveCount': len(self.scale_ranges),
            'perspectiveRanges': {k: {'min': lo, 'max': hi, 'ratio': hi / max(.0001, lo)} for k, (lo, hi) in self.scale_ranges.items()}}
        sequence = [x['version'] for x in self.report['transitions']]
        evidence.update(currentRestoreAfterNew=any(v == 'CURRENT' and 'NEW' in sequence[:i] for i, v in enumerate(sequence)),
            behindCameraSources=sorted(self.behind_sources), occludedSources=sorted(self.occluded_sources), clearAfterOccludedSources=sorted(self.clear_after_occluded),
            perspectiveChangeObserved=any(hi / max(.0001, lo) >= 1.25 for lo, hi in self.scale_ranges.values()))
        complete = (evidence['hasCurrentAndNew'] and evidence['currentBaselineCount'] > 0 and evidence['nameComponentCount'] > 0
            and evidence['visibleNewPerspectiveCount'] > 0 and evidence['currentRestoreAfterNew'] and evidence['perspectiveChangeObserved']
            and bool(self.behind_sources) and bool(self.clear_after_occluded))
        self.report.update(status=status if complete or status != 'COMPLETE' else 'COMPLETE_COVERAGE_INCOMPLETE', evidence=evidence,
            dataAssertionsPassed=not self.report['violations'], cameraCoverageComplete=complete,
            passed=status == 'COMPLETE' and complete and not self.report['violations'],
            artisticAcceptance=False, nativeHeadSocketMappingVerified=False,
            cameraWallCoverage='Native diagnostics and actual PNGs require parent review; no renderer-depth/pixel or artistic acceptance is inferred.')
        self.write()

    def cancel(self):
        self.finish('CANCELLED')

    def tick(self, _delta):
        try:
            now = time.monotonic()
            if now - self.started >= MAX_SECONDS:
                self.finish('COMPLETE');return
            if now - self.last_discovery >= 1.:
                self.discover();self.last_discovery = now
            if now - self.last_sample >= .2:
                self.sample(now);self.last_sample = now
            if now - self.last_write >= 1.:
                self.write();self.last_write = now
        except Exception:
            self.report['error'] = traceback.format_exc();self.finish('ERROR')

    def start(self):
        assert '-mchubtransientprofile' in u.SystemLibrary.get_command_line().lower(), 'Requires owned transient-profile run'
        worlds = u.EditorLevelLibrary.get_pie_worlds(True)
        assert len(worlds) == 1 and u.SystemLibrary.is_standalone(worlds[0]), 'Requires one existing standalone PIE world'
        self.world = worlds[0]
        self.pc = u.GameplayStatics.get_player_controller(self.world, 0)
        assert isinstance(self.pc, u.MCPlayerController) and self.pc.is_local_controller() and self.pc.is_world_presentation_ready()
        assert len(u.GameplayStatics.get_all_actors_of_class(self.world, u.MCPlayerController)) == 1
        self.camera = prop(self.pc, 'PlayerCameraManager');assert self.camera is not None
        assert u.GameplayStatics.get_current_level_name(self.world, True) in ('Hub', 'L_Mouth', 'L_Tutorial')
        assert hasattr(u, 'MCWorldLabelPresenterComponent') and hasattr(u, 'MCWorldLabelWidget'), 'Build06 WorldLabel classes unavailable'
        assert callable(getattr(u.MCGameplayUIEditorLibrary, 'capture_pie_game_window', None))
        self.discover();assert self.labels and self.names, 'Needs actual native label + player-name components; no fixture actors are spawned'
        self.report['status'] = 'OBSERVING'
        self.report['preflight'] = {'world': path(self.world), 'controller': path(self.pc),
            'initialVersion': version_name(), 'labelCount': len(self.labels), 'nameCount': len(self.names)}
        self.sample(time.monotonic());self.write()
        self.callback = u.register_slate_post_tick_callback(self.tick)


prior = getattr(builtins, KEY, None)
assert prior is None or getattr(prior, 'callback', None) is None, 'Existing observer owns this callback; cancel explicitly first'
observer = WorldCameraObserver()
setattr(builtins, KEY, observer)
try:
    observer.start()
except Exception:
    observer.report['error'] = traceback.format_exc();observer.finish('SKIPPED_PREFLIGHT')
