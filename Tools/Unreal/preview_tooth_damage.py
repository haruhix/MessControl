"""Three stable gameplay-camera frames plus a persistent wipe-volume check in solo PIE."""
import json, math, time, traceback
from pathlib import Path
import unreal as u

class ToothStagePreview:
    def __init__(self):
        self.world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_game_world();assert self.world
        self.pc=u.GameplayStatics.get_player_controller(self.world,0);self.pc.request_dev_action(u.MCDevAction.FOG_EVENT)
        self.hero=u.GameplayStatics.get_player_pawn(self.world,0)
        for e in u.GameplayStatics.get_all_actors_of_class(self.world,u.MCFogBrawlEvent):e.call_method('Stop',())
        teeth=u.GameplayStatics.get_all_actors_of_class(self.world,u.MCArenaTooth)
        self.tooth=next(t for t in teeth if t.get_editor_property('Visual').static_mesh.get_name()=='SM_Teeth_R_04')
        self.neighbor=next(t for t in teeth if t.get_editor_property('Visual').static_mesh.get_name()=='SM_Teeth_R_03')
        self.dirty=next(t for t in teeth if t.get_editor_property('Visual').static_mesh.get_name()=='SM_Teeth_L_03')
        self.dirty.call_method('SetCoffee',(1,))
        self.mask=[255 if i%3 else 0 for i in range(len(self.dirty.get_editor_property('GrimeMask')))]
        assert len(self.mask)>0
        self.dirty.set_editor_property('GrimeMask',self.mask)
        self.dirty.call_method('ReceiveArenaHit',(70,u.Vector(0,1,0)))
        self.phase=0;self.busy=False;self.finished=False;self.rows=[];self.started=time.monotonic();self.at=u.GameplayStatics.get_time_seconds(self.world)
        self.out=Path(globals().get('TOOTH_DAMAGE_ARTIFACTS',Path(u.Paths.project_dir()).resolve()/'Artifacts/ToothDamage_20261014'));self.out.mkdir(parents=True,exist_ok=True)
        self.place()
        self.handle=u.register_slate_post_tick_callback(self.tick)
    def place(self):
        t=self.tooth;h=self.hero
        center,extent,_=u.SystemLibrary.get_component_bounds(t.get_editor_property('Body'))
        tongue=u.GameplayStatics.get_all_actors_of_class(self.world,u.MCTongue)[0]
        tc,_,_=u.SystemLibrary.get_component_bounds(tongue.surface)
        length=math.hypot(tc.x-center.x,tc.y-center.y)
        direction=u.Vector((tc.x-center.x)/length,(tc.y-center.y)/length,0)
        radius=abs(direction.x)*extent.x+abs(direction.y)*extent.y+260
        point=center+direction*radius
        ignore=[a for a in u.GameplayStatics.get_all_actors_of_class(self.world,u.Actor) if a!=tongue]
        hit=u.SystemLibrary.line_trace_single(self.world,point+u.Vector(0,0,500),point-u.Vector(0,0,2200),u.TraceTypeQuery.ECC_VISIBILITY,False,ignore,u.DrawDebugTrace.NONE)
        assert hit
        pos=hit.to_tuple()[5]+u.Vector(0,0,h.capsule_component.get_scaled_capsule_half_height()+3)
        h.set_actor_location(pos,False,True);h.character_movement.stop_movement_immediately();h.character_movement.set_movement_mode(u.MovementMode.MOVE_WALKING)
        self.yaw=u.MathLibrary.find_look_at_rotation(pos,center).yaw
        self.orbit()
    def orbit(self):
        h=self.hero;view=h.camera.get_world_rotation()
        yaw=(self.yaw-view.yaw+180)%360-180
        h.apply_camera_orbit_input(u.Vector2D(yaw/h.camera_orbit_sensitivity,(-10-view.pitch)/h.camera_orbit_sensitivity))
    def shot(self,name):
        t=self.tooth
        self.rows.append({'image':name+'.png','health':t.get_editor_property('State').get_editor_property('Health'),'mesh':t.get_editor_property('Visual').static_mesh.get_name(),
                          'neighbor_mesh':self.neighbor.get_editor_property('Visual').static_mesh.get_name(),
                          'hero':list(self.hero.get_actor_location().to_tuple()),'camera':list(self.hero.camera.get_world_location().to_tuple()),'rotation':list(self.hero.camera.get_world_rotation().to_tuple())})
        u.SystemLibrary.execute_console_command(self.world,'HighResShot 1280x720 filename='+(self.out/(name+'.png')).as_posix(),self.pc)
    def done(self,error=None):
        if self.finished:return
        self.finished=True
        u.unregister_slate_post_tick_callback(self.handle)
        data={'passed':error is None,'error':error,'captures':self.rows,'mask_preserved':list(self.dirty.get_editor_property('GrimeMask'))==self.mask,'mode':'existing R04 and neighboring R03 arena teeth damaged equally; ordinary orbit camera at -10 degrees; supported tongue placement; no render-only mesh substitution'}
        (self.out/'StagePreview.json').write_text(json.dumps(data,indent=2),encoding='utf-8')
        print('TOOTH_STAGE_PREVIEW_RESULT',json.dumps(data))
    def tick(self,dt):
        if self.busy or self.finished:return
        self.busy=True
        try:
            assert time.monotonic()-self.started<40
            now=u.GameplayStatics.get_time_seconds(self.world)
            if self.phase==0:
                self.orbit()
                if now-self.at>1.5:
                    assert list(self.dirty.get_editor_property('GrimeMask'))==self.mask,'Damage reset partial cleaning'
                    self.shot('11_IntactFullCrown');self.at=now;self.phase=1
            elif self.phase==1 and now-self.at>.4:
                for t in (self.tooth,self.neighbor):t.call_method('ReceiveArenaHit',(25,u.Vector(0,1,0)))
                self.at=now;self.phase=2
            elif self.phase==2 and now-self.at>1.2:
                self.shot('12_ChippedFullCrown');self.at=now;self.phase=3
            elif self.phase==3 and now-self.at>.4:
                for t in (self.tooth,self.neighbor):t.call_method('ReceiveArenaHit',(40,u.Vector(0,1,0)))
                self.at=now;self.phase=4
            elif self.phase==4 and now-self.at>1.2:
                self.shot('13_BrokenFullCrown');self.at=now;self.phase=5
            elif self.phase==5 and now-self.at>.4:self.done()
        except Exception:self.done(traceback.format_exc())
        finally:self.busy=False

_tooth_stage_preview=ToothStagePreview()
print('TOOTH_STAGE_PREVIEW_INSTALLED')
