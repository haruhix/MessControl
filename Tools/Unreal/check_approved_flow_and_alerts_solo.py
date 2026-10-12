"""Short solo PIE map-transition probe and genuine HUD-frame capture.

Only the tutorial completion stage is advanced as a PIE fixture; lessons are
not claimed tested. The existing tutorial-finished delegate performs travel.
All editor/play/performance settings and the original map are restored.
"""
import builtins
import json
import time
import traceback
from pathlib import Path
import unreal as u

KEY = 'mc_approved_flow_probe'
SETTINGS = dict(playNetMode='PIE_Standalone', playNumberOfClients=1,
                runUnderOneProcess=True, bLaunchSeparateServer=False,
                newWindowWidth=1280, newWindowHeight=720)


def prop(obj, name):
    try:
        return obj.get_editor_property(name)
    except Exception:
        return obj.get_editor_property(''.join(part[:1].upper()+part[1:] for part in name.split('_')))


class FlowProbe:
    def __init__(self):
        self.levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
        assert not self.levels.is_in_play_in_editor(), 'Preserve existing PIE'
        assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages(), 'Preserve dirty maps'
        self.original = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world().get_path_name().split('.')[0]
        self.play = u.load_object(None, '/Script/UnrealEd.Default__LevelEditorPlaySettings')
        self.old_play = json.loads(u.ToolsetLibrary.get_object_properties(self.play, list(SETTINGS)))
        self.perf = u.get_default_object(u.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings'))
        self.old_throttle = prop(self.perf, 'bThrottleCPUWhenNotForeground')
        self.engine = next(o for o in u.ObjectIterator(u.load_class(None, '/Script/Engine.Engine'))
                           if o.get_path_name().startswith('/Engine/Transient.'))
        self.old_fixed = [prop(self.engine, 'bUseFixedFrameRate'), prop(self.engine, 'FixedFrameRate')]
        self.old_idle = u.SystemLibrary.get_console_variable_int_value('t.IdleWhenNotForeground')
        self.started = time.monotonic()
        self.stage = 'tutorial'
        self.callback = None
        self.pie_start = None
        self.world = self.pc = None
        self.travel_at = self.capture_at = None
        self.index = 0
        self.next_frame = 0.
        self.messages = set()
        self.out = Path(u.Paths.project_saved_dir())/'Checks/ApprovedFlowAndAlerts.json'
        self.frames = Path(u.Paths.project_saved_dir()).resolve()/'ApprovedMechanicsFrames/alarms'
        self.frames.mkdir(parents=True, exist_ok=True)
        self.report = dict(status='running', checks={}, frames=[], scope='Solo PIE; tutorial completion fixture, actual travel, first perk and actual HUD',
                           fixture='Tutorial Stage set to Complete after normal loading; lessons skipped for transition check',
                           original_map=self.original)

    def write(self):
        self.out.parent.mkdir(parents=True, exist_ok=True)
        self.out.write_text(json.dumps(self.report, indent=2, ensure_ascii=False), encoding='utf8')

    def start(self):
        assert self.levels.load_level('/Game/Maps/L_Tutorial')
        assert u.ToolsetLibrary.set_object_properties(self.play, json.dumps(SETTINGS))
        self.perf.set_editor_property('bThrottleCPUWhenNotForeground', False)
        self.engine.set_editor_property('bUseFixedFrameRate', True)
        self.engine.set_editor_property('FixedFrameRate', 30.)
        ew = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
        u.SystemLibrary.execute_console_command(ew, 't.IdleWhenNotForeground 0')
        self.callback = u.register_slate_post_tick_callback(self.tick)
        # Use the engine's explicit floating-window request. The subsystem's
        # EditorRequestBeginPlay always plays in the active editor viewport.
        self.pie_start = u.ToolsetRegistry.execute_tool(
            'EditorToolset.EditorAppToolset', 'StartPIE',
            json.dumps({'Options': {'bSimulate': False, 'PlayMode': 1, 'WarmupSeconds': 0.}}))
        assert self.pie_start is not None, 'StartPIE returned no async result'
        assert not prop(self.pie_start, 'Error'), str(prop(self.pie_start, 'Error'))
        self.write()
        u.log('MC_APPROVED_FLOW_STARTED')

    def finish(self, status):
        self.stage = 'closing'
        self.report['status'] = status
        self.report['passed'] = status == 'complete' and all(self.report['checks'].values())
        if self.levels.is_in_play_in_editor():
            self.levels.editor_request_end_play()
        self.write()

    def restore(self):
        u.ToolsetLibrary.set_object_properties(self.play, json.dumps(self.old_play))
        self.perf.set_editor_property('bThrottleCPUWhenNotForeground', self.old_throttle)
        self.engine.set_editor_property('bUseFixedFrameRate', self.old_fixed[0])
        self.engine.set_editor_property('FixedFrameRate', self.old_fixed[1])
        ew = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
        u.SystemLibrary.execute_console_command(ew, 't.IdleWhenNotForeground '+str(self.old_idle))
        assert self.levels.load_level(self.original)
        u.unregister_slate_post_tick_callback(self.callback)
        self.report['restored'] = True
        self.write()
        self.stage = 'done'
        u.log('MC_APPROVED_FLOW_FINISHED '+json.dumps(self.report['checks']))

    def tick(self, dt):
        try:
            if self.stage == 'closing':
                if not self.levels.is_in_play_in_editor():
                    self.restore()
                return
            if self.pie_start is not None:
                assert not prop(self.pie_start, 'Error'), 'StartPIE: '+str(prop(self.pie_start, 'Error'))
            if time.monotonic()-self.started > 180:
                raise RuntimeError('Solo flow timeout')
            worlds = u.EditorLevelLibrary.get_pie_worlds(False)
            if not worlds:
                return
            self.world = worlds[0]
            self.pc = u.GameplayStatics.get_player_controller(self.world, 0)
            gs = u.GameplayStatics.get_game_state(self.world)
            if not self.pc or not self.pc.get_controlled_pawn() or not gs:
                return
            now = float(u.GameplayStatics.get_time_seconds(self.world))
            level = u.GameplayStatics.get_current_level_name(self.world, True)
            if self.stage == 'tutorial':
                if now < 3.:
                    return
                self.report['checks']['separate_tutorial_map_active'] = level == 'L_Tutorial' and bool(prop(gs, 'b_tutorial_active'))
                directors = u.GameplayStatics.get_all_actors_of_class(self.world, u.MCTutorialDirector)
                if not directors or not prop(directors[0], 'players'):
                    return
                self.report['tutorial_stage_before'] = str(prop(directors[0], 'stage'))
                assert u.ToolsetLibrary.set_object_properties(directors[0], json.dumps({'Stage':'Complete'}))
                self.travel_at = time.monotonic()
                self.stage = 'travel'
                self.write()
                return
            if self.stage == 'travel':
                if level != 'L_Mouth' or now < 1.:
                    return
                self.report['checks']['tutorial_travel_main'] = True
                self.report['checks']['normal_map_skips_tutorial'] = not bool(prop(gs, 'b_tutorial_active'))
                if not self.pc.is_reward_menu_open():
                    if time.monotonic()-self.travel_at > 40:
                        raise RuntimeError('First-perk UI did not open after travel')
                    return
                self.report['checks']['first_perk_after_travel'] = True
                self.pc.choose_reward_perk(0)
                self.stage = 'perk'
                return
            if self.stage == 'perk':
                if self.pc.is_reward_menu_open():
                    return
                self.report['checks']['first_perk_resolved'] = True
                assert u.ToolsetLibrary.set_object_properties(gs, json.dumps({'bDevManualEvents':True}))
                director = prop(gs, 'single_day_director')
                if director:
                    director.set_actor_tick_enabled(False)
                self.report['capture_fixture'] = 'Single-day scheduler tick suspended after first-perk selection for isolated HUD demonstration'
                self.capture_at = now
                self.stage = 'capture'
                return
            if self.stage == 'capture':
                age = now-self.capture_at
                for at, event, message in [(0., 'Demo.Cola', 'ХОЛОДНАЯ КОЛА · БЕРЕГИСЬ ЛЬДА!'),
                                           (4.2, 'Demo.Boss', 'БОСС ПРОСНУЛСЯ · ДЕРЖИТЕСЬ ВМЕСТЕ!'),
                                           (8.4, 'Demo.Frost', 'МОРОЗ · ОЧИСТИ ЛЁД ЩЁТКОЙ!')]:
                    if age >= at and event not in self.messages:
                        self.pc.queue_gameplay_alert(message, 2.8, event)
                        self.messages.add(event)
                if age >= self.next_frame:
                    path = self.frames/('%06d.png'%self.index)
                    u.SystemLibrary.execute_console_command(self.world, 'Shot SHOWUI filename="'+path.as_posix()+'" -nosuffix', self.pc)
                    self.report['frames'].append(dict(file=path.as_posix(), time=age))
                    self.index += 1
                    self.next_frame = age+1./12.
                if age >= 12.6:
                    widgets = u.WidgetLibrary.get_all_widgets_of_class(self.world, u.MCGameplayAlertWidget, False)
                    self.report['checks']['alert_widget_exists'] = bool(widgets)
                    self.report['checks']['three_alerts_queued'] = len(self.messages) == 3
                    self.finish('complete')
        except Exception:
            self.report['error'] = traceback.format_exc()
            u.log_error('MC_APPROVED_FLOW_ERROR '+self.report['error'])
            if self.stage == 'closing':
                u.unregister_slate_post_tick_callback(self.callback)
                self.write()
            else:
                self.finish('failed')


old = getattr(builtins, KEY, None)
assert not old or old.stage == 'done', 'Existing probe still active'
probe = FlowProbe()
setattr(builtins, KEY, probe)
probe.start()
