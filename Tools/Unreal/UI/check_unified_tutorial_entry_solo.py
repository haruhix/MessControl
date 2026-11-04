"""Observe native Tutorial entry in an existing owned solo PIE; run BEFORE travel.

Optional globals: MC_UNIFIED_TUTORIAL_OUTPUT (fresh E: directory),
MC_UNIFIED_TUTORIAL_PID (owned Editor PID), MC_UNIFIED_TUTORIAL_CAPTURE_PNG=True.
The parent starts/travels PIE and drives physical F3 separately. This observer
only reads reflected state/actual UMG and captures at most three real frames.
No stage/ACK/spawn/pawn/profile/input mutators. Default limit 45 wall seconds; explicit loading allowance stays bounded at 120.
Cancel/finish only this observer: builtins.mc_unified_tutorial_observer.cancel()
/builtins.mc_unified_tutorial_observer.finish(). Wrappers/callback are released.
"""
import builtins
import hashlib
import json
import os
from pathlib import Path
import time
import traceback

import unreal as u

KEY = 'mc_unified_tutorial_observer'
OUT = Path(globals().get('MC_UNIFIED_TUTORIAL_OUTPUT',
    'E:/CodexWork/MessControl/Validation/UIOverlayWorld_20261104/Tutorial/Take01')).resolve()
PID = globals().get('MC_UNIFIED_TUTORIAL_PID')
IGNORE_WORLD_HASH = globals().get('MC_UNIFIED_TUTORIAL_IGNORE_WORLD_HASH')
CAPTURE = bool(globals().get('MC_UNIFIED_TUTORIAL_CAPTURE_PNG', True))
MAX_SECONDS = min(120., max(15.,float(globals().get('MC_UNIFIED_TUTORIAL_SECONDS',45.))))
LABELS = ('TutorialStageTitle', 'TutorialDialogue', 'TutorialAction',
          'TutorialOwnProgress', 'TutorialTeamProgress', 'TutorialTimer')


def path(obj):
    return obj.get_path_name() if obj is not None else None


def tree(owner):
    for name in ('WidgetTree_0', 'WidgetTree'):
        found = u.find_object(owner, name)
        if isinstance(found, u.WidgetTree) and found.get_outer() == owner:
            return found
    found = [item for item in u.ObjectIterator(u.WidgetTree) if item.get_outer() == owner]
    assert len(found) == 1, ('Missing owned WidgetTree', path(owner))
    return found[0]


def node(owned_tree, name):
    found = u.find_object(owned_tree, name)
    assert isinstance(found, u.Widget) and found.get_outer() == owned_tree, name
    return found


class TutorialObserver:
    def __init__(self):
        assert OUT.drive.upper() == 'E:' and not OUT.exists(), 'Fresh E: output only'
        assert PID is None or int(PID) == os.getpid(), 'Wrong owned Editor PID'
        assert '-mchubtransientprofile' in u.SystemLibrary.get_command_line().lower(), 'Transient profile required'
        assert getattr(builtins, KEY, None) is None, 'An observer already owns this slot'
        assert hasattr(u, 'MCUnifiedTutorialWidget'), 'Current approved DLL required'
        OUT.mkdir(parents=True)
        self.started = time.monotonic()
        self.next_sample = self.next_write = self.started
        self.world = self.director = self.pc = self.pawn = None
        self.source = self.view = self.root = self.source_tree = self.view_tree = None
        self.source_nodes = self.view_nodes = None
        self.identity = None
        self.stage_times = {}
        self.captured = set()
        self.mismatch_since = None
        self.brush_ready_at = None
        self.callback = None
        self.report = dict(status='WAITING_FOR_TUTORIAL', passed=False, pid=os.getpid(),
            sourceSha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            startedUTC=time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()),
            scope='Read-only natural Loading/Intro/Brush tutorial and externally driven F3 continuity',
            samples=[], transitions=[], frames=[], checks={}, limitations=[
                'No forced stage, load ACK, targets, completion, actor movement or input.',
                'Loading may be shorter than the 0.1s observer interval; observed stages are reported exactly.',
                'This does not complete real Brush/FoodCut/CoffeeCleanup actions or reach Toothpick.',
                'First visible preparation in a warm Editor does not prove packaged/cold readiness.'])
        self.write()
        self.callback = u.register_slate_post_tick_callback(self.tick)
        setattr(builtins, KEY, self)
        u.log('MC_UNIFIED_TUTORIAL_OBSERVER_ARMED ' + str(OUT))

    def write(self):
        (OUT / 'Report.json').write_text(json.dumps(self.report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')

    def bind_views(self):
        if self.source is not None:
            return True
        sources = [x for x in u.ObjectIterator(u.MCTutorialWidget)
            if not isinstance(x, u.MCUnifiedTutorialWidget) and x.get_world() == self.world
            and x.get_owning_player() == self.pc and x.is_in_viewport()]
        roots = [x for x in u.ObjectIterator(u.MCUnifiedUIWidget)
            if x.get_world() == self.world and x.get_owning_player() == self.pc and x.is_in_viewport()]
        views = []
        if len(roots)==1:
            attached=u.find_object(tree(roots[0]),'UnifiedTutorial')
            if isinstance(attached,u.MCUnifiedTutorialWidget):views=[attached]
        assert len(sources) <= 1 and len(roots) <= 1 and len(views) <= 1, 'Ambiguous original/new owners'
        if not sources or not roots or not views:
            return False
        self.source, self.root, self.view = sources[0], roots[0], views[0]
        self.source_tree, self.view_tree = tree(self.source), tree(self.view)
        self.source_nodes = {name: node(self.source_tree, name) for name in LABELS}
        self.view_nodes = {name: node(self.view_tree, name) for name in LABELS}
        self.source_root = node(self.source_tree, 'TutorialRoot')
        self.new_card = node(self.view_tree, 'TutorialCard')
        self.switcher = node(tree(self.root), 'InterfaceScreens')
        self.identity = {name: path(getattr(self, name)) for name in
            ('world', 'director', 'pc', 'pawn', 'source', 'source_tree', 'root', 'view', 'view_tree')}
        self.report['identity'] = self.identity
        return True

    def sample(self, now):
        stage = self.director.get_editor_property('stage')
        stage_name = str(stage)
        start = float(self.director.get_editor_property('stage_started_at'))
        end = float(self.director.get_editor_property('stage_ends_at'))
        clock = float(u.GameplayStatics.get_game_state(self.world).get_server_world_time_seconds())
        sample = dict(wall=now-self.started, stage=stage_name, startedAt=start, endsAt=end,
            serverClock=clock, remaining=max(0., end-clock) if end > 0 else None,
            directorTitle=str(self.director.get_editor_property('title')),
            directorInstruction=str(self.director.get_editor_property('instruction')),
            players=[dict(player=path(p.get_editor_property('player_state')),
                loaded=bool(p.get_editor_property('loaded')), ready=bool(p.get_editor_property('ready')),
                progress=int(p.get_editor_property('stage_progress')), required=int(p.get_editor_property('stage_required')),
                target=path(p.get_editor_property('goal_target'))) for p in self.director.get_editor_property('players')],
            teamLeft=int(self.director.get_editor_property('team_tasks_left')),
            teamTotal=int(self.director.get_editor_property('team_tasks_total')),
            version=str(u.MCGameplayUIFactory.get_presentation_version()), ready=self.pc.is_world_presentation_ready())
        previous = self.report['samples'][-1] if self.report['samples'] else None
        if previous:
            assert clock + .001 >= previous['serverClock'], 'World clock rewound'
            if previous['stage'] == stage_name:
                assert start == previous['startedAt'] and end == previous['endsAt'], 'Presentation reset stage timer'
        if not previous or previous['stage'] != stage_name:
            self.report['transitions'].append(dict(stage=stage_name, wall=sample['wall'], startedAt=start, endsAt=end))
        if self.bind_views():
            for name, expected in self.identity.items():
                assert path(getattr(self, name)) == expected, ('Retained owner replaced', name)
            assert self.pc.get_controlled_pawn() == self.pawn, 'Owned pawn replaced'
            source_text = {name: str(label.get_text()) for name, label in self.source_nodes.items()}
            new_text = {name: str(label.get_text()) for name, label in self.view_nodes.items()}
            sample.update(source=source_text, new=new_text, sourceRootVisibility=str(self.source_root.get_visibility()),
                sourceVisible=self.source.is_visible(), viewVisible=self.view.is_visible(),
                newCardVisible=self.new_card.is_visible(), rootScreen=self.switcher.get_active_widget_index(),
                timerVisible=self.source_nodes['TutorialTimer'].is_visible(),
                newTimerVisible=self.view_nodes['TutorialTimer'].is_visible())
            is_new = u.MCGameplayUIFactory.get_presentation_version() == u.MCUIVersion.NEW
            active_new = is_new and sample['ready'] and sample['viewVisible'] and sample['newCardVisible']
            same = source_text == new_text and sample['timerVisible'] == sample['newTimerVisible']
            hidden = self.source_root.get_visibility() == u.SlateVisibility.HIDDEN
            sample['textsMatch'] = same
            if active_new:
                if not same or not hidden or sample['rootScreen'] != 1:
                    self.mismatch_since = self.mismatch_since or now
                    assert now-self.mismatch_since < 1.25, ('New/source continuity mismatch', sample)
                else:
                    self.mismatch_since = None
                    if stage == u.MCTutorialStage.INTRO:
                        self.report['checks']['introNewMirrorsHiddenOriginal'] = True
                    if stage == u.MCTutorialStage.BRUSH_TOOTH:
                        self.report['checks']['brushNewMirrorsHiddenOriginal'] = True
                        if self.brush_ready_at is None:
                            self.brush_ready_at = now
                            self.report['status'] = 'OBSERVING_BRUSH_F3_WINDOW'
                            self.report['brushReady'] = True
                            u.log('MC_UNIFIED_TUTORIAL_BRUSH_READY')
                    if CAPTURE and stage_name not in self.captured and len(self.captured) < 3:
                        self.captured.add(stage_name)
                        dest = OUT / ('Stage_%02d.png' % len(self.captured))
                        requested = time.monotonic()
                        assert u.MCGameplayUIEditorLibrary.capture_pie_game_window(self.world, str(dest)), 'Owned window capture failed'
                        self.report['frames'].append(dict(file=str(dest), stage=stage_name, wall=sample['wall'], captureSeconds=time.monotonic()-requested))
        self.report['samples'].append(sample)

    def tick(self, delta):
        try:
            now = time.monotonic()
            if now-self.started >= MAX_SECONDS:
                self.finish()
                return
            if now < self.next_sample:
                return
            self.next_sample = now + .1
            worlds = u.EditorLevelLibrary.get_pie_worlds(True)
            assert len(worlds) <= 1, 'External/multiple PIE worlds are preserved'
            matches = [w for w in worlds if hash(w)!=IGNORE_WORLD_HASH and u.GameplayStatics.get_current_level_name(w, True) in ('L_Tutorial','L_Mouth') and any(u.GameplayStatics.get_all_actors_of_class(w,u.MCTutorialDirector))]
            if self.world is None:
                if not matches:
                    return
                assert len(matches) == 1 and u.SystemLibrary.is_standalone(matches[0]), 'One actual solo Tutorial only'
                candidate = matches[0]
                directors = u.GameplayStatics.get_all_actors_of_class(candidate, u.MCTutorialDirector)
                pc = u.GameplayStatics.get_player_controller(candidate, 0)
                pawn = pc.get_controlled_pawn() if pc else None
                if not directors or not pc or not pawn:
                    return
                assert len(directors) == 1, 'Ambiguous real tutorial director'
                self.world, self.director, self.pc, self.pawn = candidate, directors[0], pc, pawn
                self.report['status'] = 'OBSERVING_NATIVE_TUTORIAL'
            assert self.world in worlds and self.world in matches, 'Owned Tutorial PIE ended/travelled'
            self.sample(now)
            if now >= self.next_write:
                self.write()
                self.next_write = now+.5
            if self.brush_ready_at is not None and now-self.brush_ready_at >= 15:
                self.finish()
        except Exception:
            self.report['error'] = traceback.format_exc()
            self.finish(error=True)

    def cancel(self):
        self.report['cancelled'] = True
        self.finish(error=True)

    def finish(self, error=False):
        if self.callback is not None:
            u.unregister_slate_post_tick_callback(self.callback)
            self.callback = None
        checks = self.report['checks']
        samples = self.report['samples']
        checks['loadingObserved'] = any(s['stage'] == str(u.MCTutorialStage.LOADING) for s in samples)
        checks['naturalIntroToBrushObserved'] = bool(checks.get('introNewMirrorsHiddenOriginal') and checks.get('brushNewMirrorsHiddenOriginal'))
        checks['currentAndNewObserved'] = any(s['version'] == str(u.MCUIVersion.CURRENT) for s in samples) and any(s['version'] == str(u.MCUIVersion.NEW) for s in samples)
        self.report['entryPassed'] = not error and checks['naturalIntroToBrushObserved']
        self.report['f3ContinuityObserved'] = checks['currentAndNewObserved']
        self.report['passed'] = self.report['entryPassed']
        self.report['status'] = 'PASS' if self.report['passed'] else 'FAIL' if error else 'INCOMPLETE'
        self.report['wallSeconds'] = time.monotonic()-self.started
        self.report['cleanup'] = dict(callbackUnregistered=True, stateMutations=False, pieStartStop=False)
        self.write()
        u.log('MC_UNIFIED_TUTORIAL_OBSERVER_DONE ' + self.report['status'])
        if getattr(builtins, KEY, None) is self:
            delattr(builtins, KEY)
        for name in ('world', 'director', 'pc', 'pawn', 'source', 'view', 'root', 'source_tree', 'view_tree',
                     'source_nodes', 'view_nodes', 'source_root', 'new_card', 'switcher', 'identity'):
            setattr(self, name, None)


TutorialObserver()
