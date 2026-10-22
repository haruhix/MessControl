"""Focused solo regression for pond-only casts, carry recovery and reel direction."""
import unreal as u,builtins,json,time,traceback
from pathlib import Path
OUT=Path(u.Paths.project_dir())/'Saved/HubFishing_20261022/PondRules'
class PondCheck:
    def __init__(self):
        assert '-MCHubFishingOwned' in u.SystemLibrary.get_command_line()
        assert not u.EditorLevelLibrary.get_pie_worlds(False)
        self.play=u.load_object(None,'/Script/UnrealEd.Default__LevelEditorPlaySettings')
        values={'playNetMode':'PIE_Standalone','playNumberOfClients':1,'runUnderOneProcess':True,'bLaunchSeparateServer':False,'newWindowWidth':1280,'newWindowHeight':720}
        self.old=json.loads(u.ToolsetLibrary.get_object_properties(self.play,list(values)))
        assert u.ToolsetLibrary.set_object_properties(self.play,json.dumps(values))
        self.perf=u.get_default_object(u.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'))
        self.throttle=self.perf.get_editor_property('bThrottleCPUWhenNotForeground');self.perf.set_editor_property('bThrottleCPUWhenNotForeground',False)
        self.phase='loading';self.started=time.monotonic();self.at=0;self.speed=0
        self.report={'passed':False,'checks':{},'scope':'Standalone PIE, native input callbacks; actual Hub pond. No multiplayer or package.'}
        self.callback=u.register_slate_post_tick_callback(self.tick)
        u.ToolsetRegistry.execute_tool('EditorToolset.EditorAppToolset','StartPIE',json.dumps({'options':{'bSimulate':False,'playMode':'PlayMode_InEditorFloating','warmupSeconds':0.}}))
    def now(self):return u.GameplayStatics.get_time_seconds(self.world)
    def stage(self,name):self.phase=name;self.at=self.now();self.write()
    def write(self):
        self.report['phase']=self.phase
        (OUT/'solo_report.json').write_text(json.dumps(self.report,indent=2))
    def check(self,name,ok):
        self.report['checks'][name]=bool(ok)
        assert ok,name
    def flag(self,name):return self.rod.get_editor_property(name)
    def primary(self):self.hero.set_primary_input_held(True);self.hero.set_primary_input_held(False)
    def place(self,point,yaw):
        self.hero.character_movement.stop_movement_immediately()
        self.hero.set_actor_location(point,False,True);self.hero.set_actor_rotation(u.Rotator(0,yaw,0),True)
    def bank(self,yaw=180):
        # This is a sampled walkable bank of the real pond; derive its Z from collision.
        p=u.Vector(-1100,200,1700)
        hit=u.SystemLibrary.line_trace_single(self.world,p,p-u.Vector(0,0,3000),u.TraceTypeQuery.ECC_VISIBILITY,False,[self.hero],u.DrawDebugTrace.NONE,True)
        assert hit
        info=u.get_default_object(u.GameplayStatics).call_method('BreakHitResult',args=(hit,))
        assert info[0]
        self.bank_point=info[5]+u.Vector(0,0,self.hero.capsule_component.get_scaled_capsule_half_height()+2)
        self.place(self.bank_point,yaw)
    def carry_error(self):
        expected=u.MathLibrary.compose_transforms(self.rod.carry_pose,self.hero.get_actor_transform())
        actual=self.rod.rod_frame.get_world_transform()
        return max((u.MathLibrary.transform_location(actual,p)-u.MathLibrary.transform_location(expected,p)).length() for p in [self.rod.left_grip,self.rod.right_grip])
    def tick(self,dt):
        try:
            if time.monotonic()-self.started>90:raise RuntimeError('Timeout: '+self.phase)
            if self.phase=='loading':
                worlds=u.EditorLevelLibrary.get_pie_worlds(False)
                if not worlds:return
                assert len(worlds)==1;self.world=worlds[0]
                self.hero=u.GameplayStatics.get_player_character(self.world,0);pc=u.GameplayStatics.get_player_controller(self.world,0)
                if not self.hero or self.now()<3 or not pc.is_world_presentation_ready() or pc.is_move_input_ignored():return
                self.rod=self.hero.fishing_rod;self.start=self.hero.get_actor_location()
                self.hero.set_primary_input_held(False);self.hero.set_brace_input_held(False);self.hero.set_handle_input_held(False)
                self.rod.server_set_equipped(False);self.rod.server_set_equipped(True)
                self.place(self.start,0);self.stage('dry');return
            elapsed=self.now()-self.at
            if self.phase=='dry' and elapsed>.3:
                self.check('dry_ground_has_no_cast_target',not self.rod.can_cast_line())
                self.primary();self.hero.set_brace_input_held(True);self.stage('dry_rejected')
            elif self.phase=='dry_rejected' and elapsed>.3:
                self.check('dry_ground_cast_rejected',not self.flag('bLineOut'))
                self.check('dry_ground_fishing_pose_rejected',not self.flag('bFishingPose'))
                self.hero.set_brace_input_held(False);self.bank(0);self.stage('bank_away')
            elif self.phase=='bank_away' and elapsed>.4:
                self.check('bank_remains_walkable',not self.hero.character_movement.is_swimming())
                self.check('facing_land_has_no_target',not self.rod.can_cast_line())
                self.primary();self.check('landward_cast_rejected',not self.flag('bLineOut'))
                self.place(self.bank_point,180);self.stage('bank_pond')
            elif self.phase=='bank_pond' and elapsed>.4:
                self.check('pond_direction_has_cast_target',self.rod.can_cast_line())
                self.hero.set_brace_input_held(True);self.stage('pose')
            elif self.phase=='pose' and elapsed>.5:
                self.check('pond_allows_fishing_pose',self.flag('bFishingPose'))
                self.primary();self.hero.set_brace_input_held(False)
                self.check('pond_cast_starts',self.flag('bLineOut'))
                self.stage('water')
            elif self.phase=='water' and elapsed>1.8:
                ponds=[p for p in u.GameplayStatics.get_all_actors_of_class(self.world,u.PhysicsVolume) if 'FishingPond' in [str(t) for t in p.tags]]
                self.check('one_tagged_hub_pond',len(ponds)==1);pond=ponds[0]
                center,extent=pond.get_actor_bounds(False)
                p=self.rod.lure_point;self.report['pond_landing_position']=[p.x,p.y,p.z]
                self.check('lure_lands_inside_pond_footprint',abs(p.x-center.x)<extent.x and abs(p.y-center.y)<extent.y)
                self.check('lure_resting_at_water_surface',abs(self.rod.lure_point.z-(center.z+extent.z+4))<2)
                hit=u.SystemLibrary.line_trace_single(self.world,p+u.Vector(0,0,1500),p-u.Vector(0,0,220),u.TraceTypeQuery.ECC_VISIBILITY,False,[self.hero],u.DrawDebugTrace.NONE,True)
                floor=u.get_default_object(u.GameplayStatics).call_method('BreakHitResult',args=(hit,)) if hit else None
                self.check('lure_lands_over_water_not_dry_bank',not floor or not floor[0] or floor[5].z<center.z+extent.z-12)
                self.check('line_pays_out_to_pond',self.rod.free_line_length>self.rod.hanging_length*2)
                self.stage('walk')
            elif self.phase=='walk':
                self.hero.add_movement_input(u.Vector(0,1,0),.65,False)
                self.speed=max(self.speed,self.hero.get_velocity().length())
                if elapsed>.5:self.hero.character_movement.stop_movement_immediately();self.stage('carry')
            elif self.phase=='carry' and elapsed>.7:
                self.check('player_walked_after_cast',self.speed>30)
                self.check('walking_cancels_cast',not self.flag('bLineOut'))
                self.check('walking_clears_fishing_pose',not self.flag('bFishingPose'))
                error=self.carry_error();self.report['walking_carry_grip_error_cm']=error
                self.check('carry_pose_restored_after_walking',error<1)
                self.check('hanging_line_restored_after_walking',abs(self.rod.free_line_length-self.rod.hanging_length)<.1)
                self.bank();self.stage('cast_again')
            elif self.phase=='cast_again' and elapsed>.4:
                self.check('can_fish_again_after_walking',self.rod.can_cast_line())
                self.primary();self.stage('second_water')
            elif self.phase=='second_water' and elapsed>1.8:
                self.reel_pitch=self.rod.reel_visual.get_editor_property('relative_rotation').pitch
                self.hero.set_handle_input_held(True);self.check('reeling_starts',self.flag('bReeling'))
                self.stage('reverse')
            elif self.phase=='reverse' and elapsed>.1:
                pitch=self.rod.reel_visual.get_editor_property('relative_rotation').pitch
                delta=(pitch-self.reel_pitch+180)%360-180;self.report['reel_pitch_delta_degrees']=delta
                self.check('reel_rotates_in_reversed_direction',delta>5)
                self.stage('retrieve')
            elif self.phase=='retrieve':
                if not self.flag('bLineOut'):
                    self.hero.set_handle_input_held(False);self.stage('done')
                elif elapsed>8:raise RuntimeError('Reeling failed to return lure')
            elif self.phase=='done' and elapsed>.7:
                self.check('reeling_finishes_cast',not self.flag('bLineOut') and not self.flag('bReeling'))
                self.check('reeling_clears_fishing_pose',not self.flag('bFishingPose'))
                self.check('carry_pose_restored_after_reeling',self.carry_error()<1)
                self.report['passed']=True;self.finish()
        except Exception:self.report['error']=traceback.format_exc();self.finish()
    def finish(self):
        u.unregister_slate_post_tick_callback(self.callback);self.report['wall_seconds']=time.monotonic()-self.started;self.write()
        u.get_editor_subsystem(u.LevelEditorSubsystem).editor_request_end_play()
        u.ToolsetLibrary.set_object_properties(self.play,json.dumps(self.old))
        self.perf.set_editor_property('bThrottleCPUWhenNotForeground',self.throttle)
        print('FISHING_POND_CHECK_DONE',self.report['passed'],self.report.get('error',''))
builtins.mc_pond_check=PondCheck()
