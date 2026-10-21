"""One solo PIE: walk into the saved water, float, stroke, and reach dry shore."""
import unreal as u,json,time,traceback,builtins,math
from pathlib import Path
OUT=Path(u.Paths.project_saved_dir())/'HubSwimming_20261021'
def vec(v):return [v.x,v.y,v.z]
def direction2d(v):return u.Vector(v.x,v.y,0)/max(.001,math.hypot(v.x,v.y))
class SwimmingProbe:
    def __init__(self):
        assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
        self.play=u.load_object(None,'/Script/UnrealEd.Default__LevelEditorPlaySettings')
        settings={'playNetMode':'PIE_Standalone','playNumberOfClients':1,'runUnderOneProcess':True,'bLaunchSeparateServer':False,'newWindowWidth':1280,'newWindowHeight':720}
        self.old=json.loads(u.ToolsetLibrary.get_object_properties(self.play,list(settings)))
        assert u.ToolsetLibrary.set_object_properties(self.play,json.dumps(settings))
        self.perf=u.get_default_object(u.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'))
        self.throttle=self.perf.get_editor_property('bThrottleCPUWhenNotForeground')
        self.perf.set_editor_property('bThrottleCPUWhenNotForeground',False)
        self.start=time.monotonic();self.phase='loading';self.report={'passed':False,'checks':{},'frames':[]}
        self.callback=u.register_slate_post_tick_callback(self.tick)
        u.ToolsetRegistry.execute_tool('EditorToolset.EditorAppToolset','StartPIE',json.dumps({'options':{'bSimulate':False,'playMode':'PlayMode_InEditorFloating','warmupSeconds':0}}))
    def check(self,name,result):
        self.report['checks'][name]=bool(result);assert result,name
    def stage(self,name):
        self.phase=name;self.at=u.GameplayStatics.get_time_seconds(self.world)
        self.report['phase']=name;(OUT/'solo_progress.json').write_text(json.dumps(self.report,indent=2))
    def snapshot(self):
        accel=self.move.get_current_acceleration();anim=self.hero.mesh.get_anim_instance()
        return {'position':vec(self.hero.get_actor_location()),'velocity':vec(self.hero.get_velocity()),'mode':str(self.move.movement_mode),
          'swim':self.move.is_swimming(),'energy':self.move.get_swim_energy(),'animation_class':anim.get_class().get_path_name() if anim else None,
          'acceleration_2d':math.hypot(accel.x,accel.y),'wet_coffee':self.hero.get_editor_property('bInCoffee')}
    def screenshot(self,name):
        path=OUT/(name+'.png')
        u.SystemLibrary.execute_console_command(self.world,'HighResShot 1280x720 filename="'+path.as_posix()+'"',self.pc)
        self.report['frames'].append(path.as_posix())
    def choose_shore(self):
        data=json.loads((OUT/'ground_grid_final.json').read_text())
        occupied={(r['x'],r['y']):r for r in data}
        pairs=[]
        for r in data:
            if r['actor']!='Ground' or r['normal_z']<.8 or not(-180<r['depth']<-15):continue
            for dx,dy in [(-600,0),(600,0),(0,-600),(0,600)]:
                x,y=r['x']+dx,r['y']+dy
                if (x,y) not in occupied and -3500<x<500 and -2800<y<1000:
                    pairs.append((abs(r['depth']),r,u.Vector(x,y,self.z+5)))
        assert pairs,'No clear water / walkable shore pair in the authored map'
        _,shore,water=min(pairs,key=lambda p:p[0])
        self.dry=u.Vector(shore['x'],shore['y'],shore['z']+self.half+3)
        self.deep=water;self.direction=direction2d(water-self.dry)
        self.report['fixture']={'dry':vec(self.dry),'water':vec(self.deep),'surface_z':self.z,'capsule_half':self.half}
    def tick(self,dt):
        try:
            assert time.monotonic()-self.start<85,'Timed out during '+self.phase
            if self.phase=='loading':
                worlds=u.EditorLevelLibrary.get_pie_worlds(False)
                if not worlds:return
                self.check('solo_world',len(worlds)==1);self.world=worlds[0]
                self.hero=u.GameplayStatics.get_player_character(self.world,0)
                if not self.hero or u.GameplayStatics.get_time_seconds(self.world)<2:return
                self.pc=u.GameplayStatics.get_player_controller(self.world,0)
                self.move=self.hero.character_movement
                self.volume=next(a for a in u.GameplayStatics.get_all_actors_of_class(self.world,u.PhysicsVolume) if a.get_actor_label()=='Hub_SwimmingWater')
                center,extent=self.volume.get_actor_bounds(False);self.z=center.z+extent.z
                self.half=self.hero.capsule_component.get_scaled_capsule_half_height()
                self.choose_shore()
                self.hero.set_actor_location(self.dry,False,True);self.move.stop_movement_immediately();self.move.set_movement_mode(u.MovementMode.MOVE_FALLING)
                self.pc.reset_ignore_move_input();self.stage('dry');return
            elapsed=u.GameplayStatics.get_time_seconds(self.world)-self.at
            if self.phase=='dry' and elapsed>.7:
                self.check('starts_on_dry_ground',self.move.is_moving_on_ground() and not self.move.is_swimming())
                self.stage('entry')
            elif self.phase=='entry':
                self.hero.add_movement_input(self.direction,1.)
                if self.move.is_swimming():
                    self.report['entry']=self.snapshot();self.check('walks_into_swimming',True)
                    self.stage('float')
                elif elapsed>8:raise AssertionError('Walking into the pond failed: '+json.dumps(self.snapshot()))
            elif self.phase=='float' and elapsed>2.5:
                self.check('floats_at_visible_surface',self.move.is_swimming() and abs(self.hero.get_actor_location().z-(self.z+5))<15)
                self.check('procedural_animation_instance_present',isinstance(self.hero.mesh.get_anim_instance(),u.MCToothAnimInstance))
                self.check('ordinary_water_is_not_coffee',not self.hero.get_editor_property('bInCoffee'))
                self.check('safe_hub_water_preserves_energy',abs(self.move.get_swim_energy()-self.move.get_max_swim_energy())<.01)
                self.report['idle']=self.snapshot();self.stroke_start=self.hero.get_actor_location()
                self.screenshot('swimming_idle');self.stage('stroke')
            elif self.phase=='stroke':
                self.hero.add_movement_input(self.direction,1.)
                if elapsed>1.5:
                    delta=self.hero.get_actor_location()-self.stroke_start;distance=math.hypot(delta.x,delta.y)
                    self.check('input_moves_swimmer',distance>100)
                    accel=self.move.get_current_acceleration()
                    self.check('stroke_input_has_acceleration',math.hypot(accel.x,accel.y)>100)
                    self.report['stroke']=self.snapshot();self.report['travel_cm']=distance
                    self.screenshot('swimming_stroke');self.stage('exit')
            elif self.phase=='exit':
                direction=direction2d(self.dry-self.hero.get_actor_location())
                # The ordinary W binding turns into upward input while climbing.
                self.hero.add_movement_input(u.Vector(0,0,1) if self.move.movement_mode==u.MovementMode.MOVE_CUSTOM else direction,1.)
                if elapsed>5:self.hero.set_handle_input_held(True)
                if self.move.is_moving_on_ground() and self.hero.get_actor_location().z>self.z+self.half:
                    self.hero.set_handle_input_held(False);self.report['exit']=self.snapshot()
                    self.check('reaches_dry_shore',True);self.report['shore_climb_used']=elapsed>5;self.stage('land')
                elif elapsed>15:raise AssertionError('Could not return to the shore: '+json.dumps(self.snapshot()))
            elif self.phase=='land' and elapsed>1:
                self.check('walking_restores_after_swimming',self.move.is_moving_on_ground() and not self.move.is_swimming())
                self.check('finite_position',all(math.isfinite(v) for v in vec(self.hero.get_actor_location())))
                self.screenshot('swimming_shore');self.report['passed']=True;self.finish()
        except Exception:
            self.report['error']=traceback.format_exc()
            try:self.report['last_state']=self.snapshot() if hasattr(self,'hero') and self.hero else None
            except Exception:self.report['diagnostic_error']=traceback.format_exc()
            self.finish()
    def finish(self):
        u.unregister_slate_post_tick_callback(self.callback)
        self.report.update(phase=self.phase,wall_seconds=time.monotonic()-self.start)
        (OUT/'solo_report.json').write_text(json.dumps(self.report,indent=2))
        u.get_editor_subsystem(u.LevelEditorSubsystem).editor_request_end_play()
        u.ToolsetLibrary.set_object_properties(self.play,json.dumps(self.old))
        self.perf.set_editor_property('bThrottleCPUWhenNotForeground',self.throttle)
        print('HUB_SWIMMING_SOLO_RESULT '+json.dumps(self.report))
builtins.mc_hub_swimming_probe=SwimmingProbe()
