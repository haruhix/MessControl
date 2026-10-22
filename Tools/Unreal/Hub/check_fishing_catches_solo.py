"""Actual pond casts, random bites, automatic cube pickup and personal HUD count."""
import unreal as u,builtins,json,time,traceback,re
from pathlib import Path
OUT=Path(u.Paths.project_dir())/'Saved/HubFishing_20261022/Catches'
class CatchCheck:
    def __init__(self):
        assert '-MCHubFishingOwned' in u.SystemLibrary.get_command_line()
        assert not u.EditorLevelLibrary.get_pie_worlds(False)
        self.play=u.load_object(None,'/Script/UnrealEd.Default__LevelEditorPlaySettings')
        values={'playNetMode':'PIE_Standalone','playNumberOfClients':1,'runUnderOneProcess':True,'bLaunchSeparateServer':False,'newWindowWidth':1280,'newWindowHeight':720}
        self.old=json.loads(u.ToolsetLibrary.get_object_properties(self.play,list(values)))
        assert u.ToolsetLibrary.set_object_properties(self.play,json.dumps(values))
        self.perf=u.get_default_object(u.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'))
        self.throttle=self.perf.get_editor_property('bThrottleCPUWhenNotForeground');self.perf.set_editor_property('bThrottleCPUWhenNotForeground',False)
        self.phase='loading';self.started=time.monotonic();self.at=0;self.catch_index=0
        self.report={'passed':False,'checks':{},'bite_delays':[],'scope':'Solo PIE on the actual Hub pond. Native input callbacks, two default-delay catches, cancellation, empty reeling, pawn recovery and rendered HUD. No multiplayer or package.'}
        self.callback=u.register_slate_post_tick_callback(self.tick)
        u.ToolsetRegistry.execute_tool('EditorToolset.EditorAppToolset','StartPIE',json.dumps({'options':{'bSimulate':False,'playMode':'PlayMode_InEditorFloating','warmupSeconds':0.}}))
    def now(self):return u.GameplayStatics.get_time_seconds(self.world)
    def write(self):
        self.report['phase']=self.phase
        (OUT/'solo_report.json').write_text(json.dumps(self.report,indent=2))
    def stage(self,name):self.phase=name;self.at=self.now();self.write()
    def check(self,name,ok):
        self.report['checks'][name]=bool(ok)
        assert ok,name
    def flag(self,name):return self.rod.get_editor_property(name)
    def primary(self):self.hero.set_primary_input_held(True);self.hero.set_primary_input_held(False)
    def bind_hud(self):
        widgets=[w for w in u.ObjectIterator(u.MCHubWidget.static_class()) if w.get_owning_player()==self.pc and w.is_in_viewport()]
        self.check('one_personal_hub_hud',len(widgets)==1)
        self.label=next(t for t in u.ObjectIterator(u.TextBlock.static_class()) if t.get_name()=='HubFishingCount' and t.get_outer().get_outer()==widgets[0])
    def hud_count(self):
        values=re.findall(r'\d+',str(self.label.get_text()))
        return int(values[-1]) if values else -1
    def bank(self):
        start=u.Vector(-1100,200,1700)
        hit=u.SystemLibrary.line_trace_single(self.world,start,start-u.Vector(0,0,3000),u.TraceTypeQuery.ECC_VISIBILITY,False,[self.hero],u.DrawDebugTrace.NONE,True)
        assert hit
        data=u.get_default_object(u.GameplayStatics).call_method('BreakHitResult',args=(hit,));assert data[0]
        self.hero.character_movement.stop_movement_immediately()
        self.hero.set_actor_location(data[5]+u.Vector(0,0,self.hero.capsule_component.get_scaled_capsule_half_height()+2),False,True)
        self.hero.set_actor_rotation(u.Rotator(0,180,0),True)
    def resume(self):
        assert self.phase=='review'
        self.report['passed']=True;self.finish()
    def tick(self,dt):
        try:
            if time.monotonic()-self.started>110:raise RuntimeError('Timeout '+self.phase)
            if self.phase=='loading':
                worlds=u.EditorLevelLibrary.get_pie_worlds(False)
                if not worlds:return
                assert len(worlds)==1;self.world=worlds[0]
                self.pc=u.GameplayStatics.get_player_controller(self.world,0);self.hero=u.GameplayStatics.get_player_character(self.world,0)
                if not self.hero or self.now()<3 or not self.pc.is_world_presentation_ready() or self.pc.is_move_input_ignored():return
                self.identity=self.pc.player_state;self.rod=self.hero.fishing_rod
                self.hero.set_primary_input_held(False);self.hero.set_handle_input_held(False);self.hero.set_brace_input_held(False)
                self.rod.server_set_equipped(False);self.rod.server_set_equipped(True)
                self.bind_hud();self.stage('initial');return
            if self.phase=='review':return
            elapsed=self.now()-self.at
            if self.phase=='initial' and elapsed>.3:
                self.check('initial_fish_count_zero',self.identity.fish_caught==0 and self.hud_count()==0)
                self.check('fish_cube_mesh_available',self.rod.fish_visual.static_mesh.get_path_name()=='/Engine/BasicShapes/Cube.Cube')
                self.check('cube_hidden_before_catch',not self.rod.fish_visual.is_visible())
                self.check('default_random_interval_4_to_9',self.rod.min_bite_delay==4 and self.rod.max_bite_delay==9)
                self.primary();self.check('dry_cast_does_not_start_catch',not self.flag('bLineOut') and not self.flag('bFishHooked'))
                self.bank();self.stage('cancel_prepare')
            elif self.phase=='cancel_prepare' and elapsed>.3:
                self.check('pond_cast_available',self.rod.can_cast_line());self.primary();self.stage('cancel_water')
            elif self.phase=='cancel_water' and elapsed>1.8:
                self.check('wait_armed_only_after_water_landing',4<=self.rod.last_bite_delay<=9 and not self.flag('bFishHooked'))
                self.stage('walk_cancel')
            elif self.phase=='walk_cancel':
                self.hero.add_movement_input(u.Vector(0,1,0),.6,False)
                if elapsed>.45:self.hero.character_movement.stop_movement_immediately();self.stage('cancelled')
            elif self.phase=='cancelled' and elapsed>.4:
                self.check('walking_cancels_wait',not self.flag('bLineOut') and not self.flag('bFishHooked'))
                self.check('cancelled_cast_gives_no_fish',self.identity.fish_caught==0 and self.hud_count()==0)
                self.bank();self.stage('empty_prepare')
            elif self.phase=='empty_prepare' and elapsed>.3:
                self.primary();self.stage('empty_water')
            elif self.phase=='empty_water' and elapsed>1.7:
                self.hero.set_handle_input_held(True);self.stage('empty_reel')
            elif self.phase=='empty_reel':
                if not self.flag('bLineOut'):
                    self.hero.set_handle_input_held(False)
                    self.check('empty_reeling_gives_no_fish',self.identity.fish_caught==0 and not self.flag('bFishHooked'))
                    self.stage('cast_prepare')
                elif elapsed>7:raise RuntimeError('Empty reeling stuck')
            elif self.phase=='cast_prepare' and elapsed>.4:
                self.primary();self.cast_at=self.now();self.stage('wait_bite')
            elif self.phase=='wait_bite':
                self.check('no_early_or_duplicate_award_'+str(self.catch_index),self.identity.fish_caught==self.catch_index)
                if self.flag('bFishHooked'):
                    age=self.now()-self.cast_at;delay=self.rod.last_bite_delay
                    self.report['bite_delays'].append(delay)
                    self.check('random_delay_in_bounds_'+str(self.catch_index),4<=delay<=9)
                    self.check('bite_waits_after_water_landing_'+str(self.catch_index),age>=delay+1.1)
                    self.check('cube_visible_on_hook_'+str(self.catch_index),self.rod.fish_visual.is_visible())
                    self.check('hook_automatically_reels_'+str(self.catch_index),self.flag('bReeling'))
                    self.stage('pickup')
                elif elapsed>13:raise RuntimeError('No bite within default interval')
            elif self.phase=='pickup':
                if not self.flag('bLineOut'):
                    self.catch_index+=1;self.stage('count_update')
                elif elapsed>7:raise RuntimeError('Hooked fish was not retrieved')
            elif self.phase=='count_update' and elapsed>.5:
                self.check('personal_counter_increased_'+str(self.catch_index),self.identity.fish_caught==self.catch_index)
                self.check('hud_counter_increased_'+str(self.catch_index),self.hud_count()==self.catch_index)
                self.check('cube_picked_up_and_hidden_'+str(self.catch_index),not self.flag('bFishHooked') and not self.rod.fish_visual.is_visible())
                self.check('carry_state_after_pickup_'+str(self.catch_index),not self.flag('bFishingPose') and not self.flag('bReeling'))
                if self.catch_index<2:self.stage('cast_prepare')
                else:
                    self.old_pawn=self.hero;self.hero.destroy_actor();self.stage('respawn')
            elif self.phase=='respawn':
                pawn=u.GameplayStatics.get_player_character(self.world,0)
                if pawn and pawn!=self.old_pawn and elapsed>.8:
                    self.hero=pawn;self.rod=pawn.fishing_rod;self.bind_hud()
                    self.check('fish_survives_pawn_recovery',self.pc.player_state==self.identity and self.identity.fish_caught==2)
                    self.stage('final')
                elif elapsed>8:raise RuntimeError('Hub did not recover the pawn')
            elif self.phase=='final' and elapsed>.6:
                self.check('recovered_hud_keeps_fish_count',self.hud_count()==2)
                self.check('no_extra_fish_after_pickup',self.identity.fish_caught==2)
                self.report['final_hud_text']=str(self.label.get_text())
                self.stage('review')
        except Exception:self.report['error']=traceback.format_exc();self.finish()
    def finish(self):
        u.unregister_slate_post_tick_callback(self.callback);self.report['wall_seconds']=time.monotonic()-self.started;self.write()
        u.get_editor_subsystem(u.LevelEditorSubsystem).editor_request_end_play()
        u.ToolsetLibrary.set_object_properties(self.play,json.dumps(self.old))
        self.perf.set_editor_property('bThrottleCPUWhenNotForeground',self.throttle)
        print('FISHING_CATCH_CHECK_DONE',self.report['passed'],self.report.get('error',''))
builtins.mc_catch_check=CatchCheck()
