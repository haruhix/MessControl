"""Owned solo sandbox check; preserves unrelated dirty packages. Editor Python."""
import builtins,hashlib,json,os,time,traceback
from pathlib import Path
import unreal as u

EVIDENCE=Path('E:/CodexWork/MessControl/Validation/VFXPracticeSandbox_20261030')
OUT=Path(globals().get('MC_PRACTICE_OUTPUT',EVIDENCE))
assert OUT.is_relative_to(Path('E:/CodexWork/MessControl/Validation'))
OUT.mkdir(parents=True,exist_ok=True)
MAP='/Game/Tests/VFXPractice/L_VFXPractice'
PLAY=dict(playNetMode='PIE_Standalone',playNumberOfClients=1,runUnderOneProcess=True,
    bLaunchSeparateServer=False,newWindowWidth=1280,newWindowHeight=720,clientFixedFPS=[0],serverFixedFPS=0,AutoRecompileBlueprints=False)
STATION_SECONDS=float(globals().get('MC_PRACTICE_STATION_SECONDS',5.5))
def prop(obj,name):return obj.get_editor_property(name)
def v(p):return [float(p.x),float(p.y),float(p.z)]

class PracticeSolo:
    def __init__(self):
        self.levels=u.get_editor_subsystem(u.LevelEditorSubsystem)
        assert not self.levels.is_in_play_in_editor() and not u.EditorLevelLibrary.get_pie_worlds(True)
        assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages()
        self.play=u.load_object(None,'/Script/UnrealEd.Default__LevelEditorPlaySettings')
        self.old_play=json.loads(u.ToolsetLibrary.get_object_properties(self.play,list(PLAY)))
        self.perf=u.get_default_object(u.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'))
        self.old_perf=prop(self.perf,'bThrottleCPUWhenNotForeground')
        self.old_idle=u.SystemLibrary.get_console_variable_int_value('t.IdleWhenNotForeground')
        self.old_maxfps=u.SystemLibrary.get_console_variable_float_value('t.MaxFPS')
        self.before=json.loads((EVIDENCE/'Before.json').read_text(encoding='utf8'))
        assert os.getpid()==self.before['pid']
        self.root=Path('E:/DEVGAME/MessControl')
        self.source_bp=self.root/'Content/Blueprints/BP_PlayerCharacter.uasset'
        self.source_hash=hashlib.sha256(self.source_bp.read_bytes()).hexdigest()
        self.world=None;self.pc=None;self.phase='waiting';self.started=time.monotonic();self.samples=[];self.events=[]
        self.done=False;self.callback=None;self.owned=None;self.at=0;self.stage=0;self.next_sample=0
        self.report=dict(status='running',samples=self.samples,events=self.events,checks={},source_bp_disk_before=self.source_hash,
            input_method='Native lab controller navigation/reset/pause methods; explicit close spectator viewpoints for this VFX study, not physical key injection or gameplay outcomes.',old_play=self.old_play)

    def write(self):
        (OUT/'SoloReport.json').write_text(json.dumps(self.report,ensure_ascii=False,indent=2),encoding='utf8')

    def start(self):
        assert u.ToolsetLibrary.set_object_properties(self.play,json.dumps(PLAY))
        assert not prop(self.play,'AutoRecompileBlueprints')
        self.perf.set_editor_property('bThrottleCPUWhenNotForeground',False)
        assert u.EditorLoadingAndSavingUtils.load_map(MAP)
        self.editor_world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
        assert self.editor_world.get_path_name()==MAP+'.L_VFXPractice'
        for name in ('BP_PracticeStation','BP_PracticeBootstrap'):
            bp=u.load_asset('/Game/Tests/VFXPractice/'+name)
            u.BlueprintEditorLibrary.compile_blueprint(bp)
            assert u.EditorAssetLibrary.save_loaded_asset(bp)
        u.SystemLibrary.execute_console_command(self.editor_world,'t.IdleWhenNotForeground 0')
        u.SystemLibrary.execute_console_command(self.editor_world,'t.MaxFPS 60')
        # Preview authoring is opt-in; a gameplay check must not resave other
        # study assets while someone is learning in their editors.
        for name in (('Burst','Trail','HealSeed','Impact') if globals().get('MC_PRACTICE_CONFIGURE_SAVED_PREVIEWS',False) else ()):
            asset=u.load_asset('/Game/Tests/VFXPractice/Effects/NS_Practice_'+name)
            # Production Asleep scalability reacts poorly to finite study bursts.
            # These small, proximity-gated copies use no shared EffectType.
            asset.set_editor_property('effect_type',None)
            point=u.Vector(-400,-500,500)
            assert u.MCNiagaraPreviewEditorLibrary.configure_niagara_preview(asset,point,u.MathLibrary.find_look_at_rotation(point,u.Vector()))
            assert u.EditorAssetLibrary.save_loaded_asset(asset)
        self.callback=u.register_slate_post_tick_callback(self.tick)
        request=u.ToolsetRegistry.execute_tool('EditorToolset.EditorAppToolset','StartPIE',json.dumps({'options':dict(bSimulate=False,playMode='PlayMode_InEditorFloating',warmupSeconds=0.)}))
        assert request and not prop(request,'Error'),str(prop(request,'Error')) if request else 'No PIE request'
        self.write()

    def shot(self,name):
        target=OUT/'Screenshots'/name;target.parent.mkdir(exist_ok=True)
        assert u.MCGameplayUIEditorLibrary.capture_pie_game_window(self.world,str(target))
        self.events.append(dict(event='screenshot',file=str(target),seconds=self.now))

    def sample(self):
        rows=[]
        for s in self.stations:
            fx=next(c for c in s.get_components_by_class(u.NiagaraComponent) if c.get_name()=='PracticeFX')
            rows.append(dict(name=s.get_actor_label(),running=bool(prop(s,'bRunning')),cycles=int(prop(s,'Cycles')),
                active=bool(fx.is_active()),location=v(fx.get_world_location()),status=str(prop(s,'Status'))))
        return dict(seconds=self.now,released=bool(prop(self.bootstrap,'bReleased')),failed=bool(prop(self.bootstrap,'bFailed')),
            status=str(prop(self.bootstrap,'Status')),ready=self.prep.is_ready(),lab_running=bool(prop(self.lab,'bRunning')),
            controller_tick=self.pc.is_actor_tick_enabled(),station_index=int(prop(self.pc,'StationIndex')),stations=rows)

    def select(self,index):
        self.pc.call_method('ServerSelectStation',args=(index,))
        row=prop(self.lab,'Results')[index];station=prop(row,'Station')
        target=station.get_actor_location()+u.Vector(0,0,75)
        view=target+u.Vector(400,-550,330)
        self.pc.get_controlled_pawn().set_actor_location(view,False,True)
        self.pc.set_control_rotation(u.MathLibrary.find_look_at_rotation(view,target))
        self.pc.call_method('ServerResetCurrent')
        self.close_shot=False
        self.events.append(dict(event='select_and_close_spectator_view',index=index,view=v(view),target=v(target),seconds=self.now))

    def tick(self,dt):
        if self.done:return
        try:
            if time.monotonic()-self.started>190:raise RuntimeError('Sandbox check timed out')
            worlds=list(u.EditorLevelLibrary.get_pie_worlds(True))
            if not worlds:return
            if self.world is None:
                assert len(worlds)==1
                candidate=worlds[0];assert 'L_VFXPractice' in candidate.get_path_name()
                controller=u.GameplayStatics.get_player_controller(candidate,0)
                if not controller:return
                self.world=candidate;self.pc=controller;self.owned=self.world.get_path_name();self.started=time.monotonic()
                self.lab=next(iter(u.GameplayStatics.get_all_actors_of_class(self.world,u.MCVFXLab)))
                self.stations=list(u.GameplayStatics.get_all_actors_with_tag(self.world,'MCVFXLabStation'))
                self.bootstrap=next(iter(u.GameplayStatics.get_all_actors_of_class(self.world,u.load_class(None,'/Game/Tests/VFXPractice/BP_PracticeBootstrap.BP_PracticeBootstrap_C'))))
                self.prep=next(s for s in u.ObjectIterator(u.MCWorldPreloadSubsystem) if s.get_world()==self.world)
                assert len(self.stations)==8
                self.report['world']=self.owned
            assert any(w.get_path_name()==self.owned for w in worlds),'Owned PIE replaced'
            self.now=float(u.GameplayStatics.get_time_seconds(self.world))
            if self.now>=self.next_sample:
                self.samples.append(self.sample());self.next_sample=self.now+.15
                if len(self.samples)%15==0:self.write()
            if self.phase=='waiting':
                assert not prop(self.bootstrap,'bFailed'),str(prop(self.bootstrap,'Status'))
                if not prop(self.bootstrap,'bReleased'):
                    assert not prop(self.lab,'bRunning') and not any(prop(s,'bRunning') for s in self.stations)
                    return
                assert self.prep.is_ready() and self.pc.is_actor_tick_enabled()
                self.phase='capture_wait';self.at=self.now
                (OUT/'Ready.json').write_text(json.dumps(dict(process_id=os.getpid(),world=self.owned)),encoding='utf8')
                self.write();return
            if self.phase=='capture_wait':
                if not (OUT/'StartCapture.gate').exists():return
                self.phase='tour';self.at=self.now
                self.select(0);return
            elapsed=self.now-self.at
            if self.phase=='tour' and elapsed>=.18 and not self.close_shot:
                self.shot('Burst_'+str(self.stage)+'.png');self.close_shot=True
            if self.phase=='tour' and elapsed>=STATION_SECONDS:
                self.shot('Station_'+str(self.stage)+'.png')
                self.stage+=1;self.at=self.now
                if self.stage<8:self.select(self.stage)
                else:
                    self.select(1);self.phase='trail';self.at=self.now
            elif self.phase=='trail' and elapsed>=3.0:
                self.shot('TrailClose.png');self.pc.call_method('ServerToggleRunning');self.phase='paused';self.at=self.now
            elif self.phase=='paused' and elapsed>=3.0:
                assert not prop(self.lab,'bRunning') and not any(prop(s,'bRunning') for s in self.stations)
                self.shot('Paused.png');self.pc.call_method('ServerToggleRunning');self.pc.call_method('ServerResetCurrent')
                current=prop(prop(self.lab,'Results')[1],'Station')
                assert int(prop(current,'Cycles'))==1
                self.report['checks']['reset_observed_cycles_one']=True
                self.phase='reset';self.at=self.now
            elif self.phase=='reset' and elapsed>=3.0:
                self.pc.toggle_lab_navigation();assert self.pc.is_lab_navigation_open()
                self.phase='menu';self.at=self.now
            elif self.phase=='menu' and elapsed>=1.0:
                self.shot('Navigator.png');self.pc.toggle_lab_navigation();assert not self.pc.is_lab_navigation_open()
                self.report['checks'].update(eight_stations=True,readiness_gate=True,pause=True,reset=True,navigation_panel=True)
                self.report['checks']['all_stations_ran']=all(any(r['name']==s.get_actor_label() and r['running'] and r['cycles']>0 for sample in self.samples for r in sample['stations']) for s in self.stations)
                self.report['checks']['all_stations_observed_active']=all(any(r['name']==s.get_actor_label() and r['active'] for sample in self.samples for r in sample['stations']) for s in self.stations)
                self.report['checks']['readiness_observed_before_release']=any(not s['released'] and not s['lab_running'] and not any(r['running'] for r in s['stations']) for s in self.samples)
                trail=[r['location'] for sample in self.samples for r in sample['stations'] if r['name']=='Practice_02' and r['running']]
                self.report['checks']['trail_moves']=len(trail)>3 and max(p[0] for p in trail)-min(p[0] for p in trail)>100
                self.report['checks']['no_gameplay_day_scheduler']=u.GameplayStatics.get_game_mode(self.world).get_class().get_name()=='MCVFXLabGameMode'
                assert all(self.report['checks'].values()),self.report['checks']
                self.phase='drain';self.at=self.now
                (OUT/'CaptureEndReady.json').write_text('{}',encoding='utf8');self.write()
            elif self.phase=='drain' and (OUT/'FinishCapture.gate').exists():self.finish()
        except Exception:self.finish(traceback.format_exc())

    def finish(self,error=None):
        if self.done:return
        self.done=True;errors=[]
        if self.callback is not None:u.unregister_slate_post_tick_callback(self.callback)
        try:
            if self.world and any(w.get_path_name()==self.owned for w in u.EditorLevelLibrary.get_pie_worlds(True)):self.levels.editor_request_end_play()
        except Exception:errors.append(traceback.format_exc())
        for fn in (lambda:u.ToolsetLibrary.set_object_properties(self.play,json.dumps(self.old_play)),
                   lambda:self.perf.set_editor_property('bThrottleCPUWhenNotForeground',self.old_perf),
                   lambda:u.SystemLibrary.execute_console_command(self.editor_world,'t.IdleWhenNotForeground '+str(self.old_idle)),
                   lambda:u.SystemLibrary.execute_console_command(self.editor_world,'t.MaxFPS '+str(self.old_maxfps))):
            try:fn()
            except Exception:errors.append(traceback.format_exc())
        self.report['source_bp_disk_after']=hashlib.sha256(self.source_bp.read_bytes()).hexdigest()
        self.report['checks']['source_bp_disk_unchanged']=self.report['source_bp_disk_after']==self.source_hash
        self.report['restored_settings']=not errors
        self.report.update(status='failed' if error or errors else 'passed',error=error,restore_errors=errors)
        self.write();print('VFX_PRACTICE_SOLO_FINISHED',self.report['status'])

old=getattr(builtins,'mc_practice_solo',None);assert old is None or old.done
probe=PracticeSolo();builtins.mc_practice_solo=probe
try:probe.start()
except Exception:probe.finish(traceback.format_exc());raise
