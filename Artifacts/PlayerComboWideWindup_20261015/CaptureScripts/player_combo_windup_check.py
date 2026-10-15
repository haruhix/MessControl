"""Two short solo gameplay combos to inspect the wider second-stroke wind-up."""
import unreal as u
import json, math, time, traceback, builtins
from pathlib import Path

class PlayerComboCheck:
    def __init__(self):
        self.out=Path('D:/PROJECT/GAME/WaifuTD/MessControl/Artifacts/PlayerComboWideWindup_20261015')
        self.frames=self.out/'CapturedFrames';self.frames.mkdir(exist_ok=True)
        self.world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_game_world();assert self.world
        self.pc=u.GameplayStatics.get_player_controller(self.world,0)
        assert isinstance(self.pc,u.MCCombatFeedbackController)
        stations=u.GameplayStatics.get_all_actors_of_class(self.world,u.MCCombatFeedbackStation)
        for station in stations:
            station.set_running(False)
            for c in station.get_components_by_class(u.TextRenderComponent):c.set_hidden_in_game(True)
        self.pc.close_test_panel();self.pc.view_current()
        self.hero=u.GameplayStatics.get_player_pawn(self.world,0)
        self.station=next(s for s in stations if not s.get_editor_property('bLegacyFeedback'))
        self.enemy=self.station.get_editor_property('Enemy');assert self.enemy
        h=self.hero.get_actor_location();e=self.enemy.get_actor_location();gap=math.hypot(e.x-h.x,e.y-h.y)
        self.hero.set_actor_location(u.Vector(e.x-(e.x-h.x)*150/gap,e.y-(e.y-h.y)*150/gap,h.z),False,True)
        self.hero.character_movement.stop_movement_immediately()
        self.hero.set_primary_input_held(False)
        self.hero.inventory.server_select(u.MCToolSlot.KNIFE)
        self.feedback=self.enemy.get_editor_property('Feedback')
        self.engine=next(o for o in u.ObjectIterator(u.load_class(None,'/Script/Engine.Engine')) if o.get_path_name().startswith('/Engine/Transient.'))
        self.perf=u.get_default_object(u.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'))
        self.old={'fixed':self.engine.get_editor_property('bUseFixedFrameRate'),'fps':self.engine.get_editor_property('FixedFrameRate'),'throttle':self.perf.get_editor_property('bThrottleCPUWhenNotForeground')}
        self.engine.set_editor_property('FixedFrameRate',60.);self.engine.set_editor_property('bUseFixedFrameRate',True)
        self.perf.set_editor_property('bThrottleCPUWhenNotForeground',False)
        lib=u.get_default_object(u.GameplayStatics)
        self.capture=lib.call_method('BeginDeferredActorSpawnFromClass',args=(self.world,u.SceneCapture2D,u.Transform(),u.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN,None,u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        lib.call_method('FinishSpawningActor',args=(self.capture,u.Transform(),u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        self.component=self.capture.get_editor_property('capture_component2d')
        self.target=u.RenderingLibrary.create_render_target2d(self.world,960,540,u.TextureRenderTargetFormat.RTF_RGBA8,u.LinearColor(.025,.03,.045,1),False,False)
        for n,v in dict(texture_target=self.target,capture_source=u.SceneCaptureSource.SCS_FINAL_COLOR_LDR,capture_every_frame=False,capture_on_movement=False,always_persist_rendering_state=True).items():self.component.set_editor_property(n,v)
        for cmd in ['mc.Combo.Debug 1','mc.Audio.ExperimentLog 1']:u.SystemLibrary.execute_console_command(self.world,cmd,self.pc)
        self.started=time.monotonic();self.finished=False;self.busy=False;self.last=-1
        self.rows=[];self.captures=[];self.checks=[];self.input=[]
        self.case='knife_combo';self.begin('knife_combo')
        self.handle=u.register_slate_post_tick_callback(self.tick)
        print('MC_PLAYER_COMBO_CHECK_STARTED')
    def now(self):return u.GameplayStatics.get_time_seconds(self.world)
    def serial(self):return self.feedback.get_editor_property('LastHit').get_editor_property('Serial')
    def begin(self,name):
        self.case=name;self.at=self.now();self.fired=set()
        self.enemy.set_editor_property('Health',100.)
        self.base=self.serial();self.hero.set_primary_input_held(False)
        if name=='pickaxe_combo':self.hero.inventory.server_select(u.MCToolSlot.PICKAXE)
        u.log('MC_PLAYER_COMBO_CASE '+name)
    def face(self):
        d=self.enemy.get_actor_location()-self.hero.get_actor_location()
        self.hero.set_actor_rotation(u.Rotator(pitch=0,yaw=math.degrees(math.atan2(d.y,d.x)),roll=0),False)
    def press(self,key,held=False):
        if key in self.fired:return
        self.fired.add(key);self.hero.set_primary_input_held(True)
        if not held:self.hero.set_primary_input_held(False)
        self.input.append({'case':self.case,'time':self.now()-self.at,'held':held})
    def capture_frame(self,age):
        manager=self.pc.player_camera_manager
        self.capture.set_actor_location_and_rotation(manager.get_camera_location(),manager.get_camera_rotation(),False,True)
        self.component.set_editor_property('fov_angle',manager.get_fov_angle())
        self.component.capture_scene()
        name=f'{self.case}_{len(self.captures):04d}.png'
        u.RenderingLibrary.export_render_target(self.world,self.target,str(self.frames),name)
        path=self.frames/name;assert path.is_file()
        self.captures.append({'case':self.case,'time':age,'file':str(path)})
    def record(self,age):
        hand=self.hero.mesh.get_socket_location('hand_r')-self.hero.get_actor_location()
        forward=self.hero.get_actor_forward_vector();right=self.hero.get_actor_right_vector()
        self.rows.append({'case':self.case,'time':age,'health':self.enemy.get_editor_property('Health'),
            'serial':self.serial(),'flash':self.feedback.get_hit_flash(),
            'hand_local':[hand.dot(forward),hand.dot(right),hand.z],
            'body_rotation':list(self.hero.mesh.get_socket_rotation('body').to_tuple()), **self.tool_sample()})
        if self.case in ['knife_combo','pickaxe_combo'] and age<2.45:self.capture_frame(age)
    def tool_sample(self):
        tool=next(c for c in self.hero.get_components_by_class(u.StaticMeshComponent) if c.get_name()=='InventoryTool');transform=tool.get_world_transform()
        forward=self.hero.get_actor_forward_vector();right=self.hero.get_actor_right_vector()
        def local(v):return [v.dot(forward),v.dot(right),v.z]
        edge=u.MathLibrary.transform_direction(transform,u.Vector(0,0,-1))
        blade=u.MathLibrary.transform_direction(transform,u.Vector(1,0,0))
        normal=u.MathLibrary.transform_direction(transform,u.Vector(0,1,0))
        tip=u.MathLibrary.transform_location(transform,u.Vector(52,0,-24) if self.case!='pickaxe_combo' else u.Vector(59.5,0,-32.8))
        return {'cutting_direction':local(edge),'blade_axis':local(blade),'blade_normal':local(normal),'tool_tip_local':local(tip-self.hero.get_actor_location())}
    def check(self,name,expected_hits,expected_health):
        hits=self.serial()-self.base;health=self.enemy.get_editor_property('Health')
        passed=hits==expected_hits and abs(health-expected_health)<.01
        self.checks.append({'name':name,'passed':passed,'accepted_hits':hits,'health':health})
        assert passed,name
    def finish(self,error=None):
        if self.finished:return
        self.finished=True;u.unregister_slate_post_tick_callback(self.handle)
        self.hero.set_primary_input_held(False)
        self.engine.set_editor_property('bUseFixedFrameRate',self.old['fixed']);self.engine.set_editor_property('FixedFrameRate',self.old['fps'])
        self.perf.set_editor_property('bThrottleCPUWhenNotForeground',self.old['throttle'])
        for cmd in ['mc.Combo.Debug 0','mc.Audio.ExperimentLog 0']:u.SystemLibrary.execute_console_command(self.world,cmd,self.pc)
        self.capture.destroy_actor()
        data={'passed':error is None and all(c['passed'] for c in self.checks),'error':error,'checks':self.checks,
            'input':self.input,'samples':self.rows,'captures':self.captures,'restored_engine_settings':self.old,
            'scope':'Solo PIE; real SetPrimaryInputHeld press/release edges. Fixed 60 Hz simulation. No multiplayer, cook or package.',
            'capture':'Genuine SceneCapture2D at the gameplay camera transform and FOV; HUD and camera postprocess are not captured.'}
        (self.out/'SoloCheck.json').write_text(json.dumps(data,indent=2),encoding='utf-8')
        print('MC_PLAYER_COMBO_CHECK_RESULT',json.dumps({'passed':data['passed'],'error':error,'checks':self.checks,'frames':len(self.captures)}))
    def tick(self,dt):
        if self.busy or self.finished:return
        self.busy=True
        try:
            assert time.monotonic()-self.started<150,'Combo check timed out'
            now=self.now();age=now-self.at
            if now<=self.last:return
            self.last=now;self.face();self.record(age)
            if self.case in ['knife_combo','pickaxe_combo']:
                for i,t in enumerate([.20,.30,.40]):
                    if age>=t:self.press(i)
                if age>=2.7:
                    self.check(self.case+' three rapid clicks cause exactly three strikes',3,25. if self.case=='knife_combo' else 0.)
                    if self.case=='knife_combo':self.begin('pickaxe_combo')
                    else:self.finish()
        except Exception:self.finish(traceback.format_exc())
        finally:self.busy=False

_player_combo_check=PlayerComboCheck()
builtins.mc_player_combo_check=_player_combo_check
