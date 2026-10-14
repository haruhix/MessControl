"""Short solo gameplay proof: actual missed fog strike, spray hold, and both watergun modes."""
import json, math, time, traceback
from pathlib import Path
import unreal as u

class ToothDamageReview:
    def __init__(self):
        self.world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_game_world();assert self.world
        self.pc=u.GameplayStatics.get_player_controller(self.world,0)
        self.pc.request_dev_action(u.MCDevAction.FOG_EVENT)
        self.out=Path(u.Paths.project_dir()).resolve()/'Artifacts/ToothDamage_20261014'
        self.started=time.monotonic();self.phase=0;self.at=0;self.busy=False;self.finished=False
        self.rows=[];self.captures=[];self.target=None;self.aimpoint=None
        self.handle=u.register_slate_post_tick_callback(self.tick)

    def health(self):return self.target.get_editor_property('State').get_editor_property('Health')
    def mesh(self):return self.target.get_editor_property('Visual').static_mesh.get_name()
    def row(self,check):self.rows.append({'check':check,'health':self.health(),'mesh':self.mesh()})
    def shot(self,name):
        self.captures.append(name+'.png')
        u.SystemLibrary.execute_console_command(self.world,'HighResShot 1280x720 filename='+(self.out/(name+'.png')).as_posix(),self.pc)
    def phase_to(self,phase,now):self.phase=phase;self.at=now

    def place(self):
        h=self.hero;t=self.target
        center,extent,_=u.SystemLibrary.get_component_bounds(t.get_editor_property('Body'))
        tongue=u.GameplayStatics.get_all_actors_of_class(self.world,u.MCTongue)[0]
        tc,_,_=u.SystemLibrary.get_component_bounds(tongue.surface)
        base=math.atan2(tc.y-center.y,tc.x-center.x)
        ignore=[a for a in u.GameplayStatics.get_all_actors_of_class(self.world,u.Actor) if a!=tongue]
        for i in range(32):
            angle=base+((i+1)//2)*.15*(1 if i%2 else -1)
            d=u.Vector(math.cos(angle),math.sin(angle),0)
            radius=abs(d.x)*extent.x+abs(d.y)*extent.y+95
            point=center+d*radius
            hit=u.SystemLibrary.line_trace_single(self.world,point+u.Vector(0,0,300),point-u.Vector(0,0,2200),u.TraceTypeQuery.ECC_VISIBILITY,False,ignore,u.DrawDebugTrace.NONE)
            if not hit:continue
            floor=hit.to_tuple()[5]
            pos=floor+u.Vector(0,0,h.capsule_component.get_scaled_capsule_half_height()+3)
            self.aimpoint=u.Vector(center.x,center.y,max(center.z+extent.z*.25,pos.z+30))
            if self.aimpoint.z>center.z+extent.z*.85:continue
            h.set_actor_location(pos,False,True);h.character_movement.stop_movement_immediately();h.character_movement.set_movement_mode(u.MovementMode.MOVE_WALKING)
            rot=u.MathLibrary.find_look_at_rotation(pos,self.aimpoint);h.set_actor_rotation(u.Rotator(0,rot.yaw,0),True)
            self.aim(1)
            return
        raise AssertionError('No supported spray placement beside actual target')

    def aim(self,strength=.35):
        if self.aimpoint is None:return
        view=self.hero.camera.get_world_rotation()
        rot=u.MathLibrary.find_look_at_rotation(self.hero.camera.get_world_location(),self.aimpoint)
        yaw=(rot.yaw-view.yaw+180)%360-180
        self.hero.apply_camera_orbit_input(u.Vector2D(yaw*strength/self.hero.camera_orbit_sensitivity,(rot.pitch-view.pitch)*strength/self.hero.camera_orbit_sensitivity))

    def done(self,error=None):
        if self.finished:return
        self.finished=True
        if getattr(self,'hero',None):self.hero.set_primary_input_held(False)
        if self.handle is not None:u.unregister_slate_post_tick_callback(self.handle);self.handle=None
        data={'passed':error is None,'error':error,'checks':self.rows,'captures':self.captures,'phase':self.phase,
              'mode':'one local solo PIE; F3 event, actual missed strike; normal primary/select input; supported tongue placement and ordinary orbit camera'}
        (self.out/'SoloRuntime.json').write_text(json.dumps(data,indent=2),encoding='utf-8')
        print('TOOTH_DAMAGE_SOLO_RESULT',json.dumps(data))

    def tick(self,dt):
        if self.busy or self.finished:return
        self.busy=True
        try:
            assert time.monotonic()-self.started<100,'Solo check wall limit'
            self.hero=u.GameplayStatics.get_player_pawn(self.world,0);h=self.hero
            if not h:return
            now=u.GameplayStatics.get_time_seconds(self.world)
            if self.phase==0:
                events=u.GameplayStatics.get_all_actors_of_class(self.world,u.MCFogBrawlEvent)
                if not events:return
                self.event=events[0];e=self.event
                if e.get_editor_property('Stage')!=u.MCFogBrawlStage.WARNING:return
                e.set_editor_property('TotalStrikes',1)
                self.target=e.get_editor_property('ActiveTarget');assert self.health()==100
                self.place();self.row('intact source mesh before actual fog strike')
                self.phase_to(1,now)
            elif self.phase==1:
                if self.event.get_editor_property('StrikesResolved')<1:return
                assert 66<self.health()<67 and self.target.call_method('IsAvailable',())
                self.row('actual unguarded fog strike chips reserve without loss')
                self.event.call_method('Stop',());self.phase_to(2,now)
            elif self.phase==2 and now-self.at>1.2:
                assert self.mesh().endswith('_Chipped'),self.mesh()
                self.shot('01_ChippedGameplay');self.phase_to(3,now)
            elif self.phase==3 and now-self.at>.3:
                self.target.call_method('ReceiveArenaHit',(100/3,u.Vector(0,1,0)));self.phase_to(4,now)
            elif self.phase==4 and now-self.at>1.2:
                assert self.mesh().endswith('_Broken'),self.mesh()
                self.row('second impact changes actual crown and brush surface to broken variant')
                assert self.target.get_editor_property('BrushSurface').static_mesh==self.target.get_editor_property('Visual').static_mesh
                self.shot('02_BrokenGameplay');self.phase_to(41,now)
            elif self.phase==41 and now-self.at>.3:
                self.place();h.inventory.server_select(u.MCToolSlot.SPRAY);self.phase_to(5,now)
            elif self.phase==5:
                self.aim()
                if now-self.at>.9:
                    self.before=self.health();h.set_primary_input_held(True);self.phase_to(6,now)
            elif self.phase==6:
                self.aim(.12)
                if now-self.at>.6:
                    h.set_primary_input_held(False)
                    assert self.health()>self.before+5,('Spray did not heal',self.before,self.health(),str(h.inventory.spray_aim()),str(self.aimpoint),h.can_work())
                    assert self.mesh().endswith('_Chipped')
                    self.row('held ordinary spray repairs broken crown to chipped')
                    self.saved=self.health();self.phase_to(7,now)
            elif self.phase==7 and now-self.at>.4:
                assert abs(self.health()-self.saved)<.01,'Released spray continued treatment'
                self.row('release preserves partial repair')
                h.set_primary_input_held(True);self.phase_to(8,now)
            elif self.phase==8:
                self.aim(.12)
                if self.health()>=100:
                    h.set_primary_input_held(False);self.row('continued spray fully restores original tooth')
                    self.phase_to(9,now)
                else:assert now-self.at<6,('Spray did not finish',self.health())
            elif self.phase==9 and now-self.at>.4:
                assert not self.mesh().endswith(('_Broken','_Chipped'))
                self.shot('03_RepairedGameplay')
                self.phase_to(91,now)
            elif self.phase==91 and now-self.at>.3:
                # Grant through the native development menu; subsequent use is the normal selected tool path.
                self.pc.request_dev_action(u.MCDevAction.GRANT_ALL_TOOL_BOOSTERS,1)
                assert h.inventory.has_upgrade(u.MCToolUpgrade.WATERGUN)
                self.target.call_method('ReceiveArenaHit',(100*2/3,u.Vector(0,1,0)))
                self.before=self.health();self.phase_to(10,now)
            elif self.phase==10 and now-self.at>.5:
                self.aim();assert not h.inventory.get_editor_property('bPressureMode')
                h.set_primary_input_held(True);self.phase_to(11,now)
            elif self.phase==11:
                self.aim(.12)
                if now-self.at>.55:
                    h.set_primary_input_held(False)
                    assert self.health()>self.before+20,('Watergun stream did not repair',self.health(),self.before)
                    self.row('watergun treatment stream restores crown faster')
                    self.shot('04_WatergunTreatment');self.phase_to(12,now)
            elif self.phase==12 and now-self.at>.3:
                self.target.call_method('ReceiveArenaHit',(25,u.Vector(0,1,0)))
                h.inventory.server_select(u.MCToolSlot.SPRAY)
                assert h.inventory.get_editor_property('bPressureMode')
                self.before=self.health();self.ammo=h.inventory.get_editor_property('WaterAmmo');h.set_primary_input_held(True);self.phase_to(13,now)
            elif self.phase==13:
                self.aim(.12)
                if now-self.at>.5:
                    h.set_primary_input_held(False)
                    assert h.inventory.get_editor_property('WaterAmmo')<self.ammo
                    assert self.health()>self.before,('Watergun pressure hits did not repair',self.before,self.health())
                    self.row('watergun pressure shots consume ammunition and repair actual contact')
                    self.done()
        except Exception:self.done(traceback.format_exc())
        finally:self.busy=False

_tooth_damage_review=ToothDamageReview()
print('TOOTH_DAMAGE_SOLO_INSTALLED')
