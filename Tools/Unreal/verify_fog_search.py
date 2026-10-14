"""Short solo PIE check through the real F3 event and ordinary handle/brace inputs."""
import json
import math
import time
import traceback
from pathlib import Path
import unreal as u

class NativeFogView:
    # Live Coding changes UClass layout; use native reflection rather than Python's cached field descriptors.
    fields={'stage':'Stage','active_target':'ActiveTarget','warning_seconds':'WarningSeconds',
            'strikes_resolved':'StrikesResolved','target_glow':'TargetGlow','last_tooth_saved':'bLastToothSaved',
            'last_guard_count':'LastGuardCount'}
    methods={'warning_remaining':'WarningRemaining','should_reveal_marker':'ShouldRevealMarker','stop':'Stop'}
    def __init__(self,actor):self.actor=actor
    def __getattr__(self,name):
        if name in self.fields:return self.actor.get_editor_property(self.fields[name])
        if name in self.methods:return lambda *args:self.actor.call_method(self.methods[name],args)
        return getattr(self.actor,name)

class FogSearchReview:
    def __init__(self):
        self.world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_game_world()
        assert self.world
        self.pc=u.GameplayStatics.get_player_controller(self.world,0)
        self.pc.request_dev_action(u.MCDevAction.FOG_EVENT)
        self.out=Path(u.Paths.project_dir()).resolve()/'Artifacts/FogBrawlSearch_20261014'
        self.started=time.monotonic();self.handle=None;self.event=None;self.phase=0
        self.rows=[];self.last_sample=-1;self.mark_at=0;self.first_target=None;self.final_at=None
        self.played=set();self.captures=[];self.after_impact=False
        self.busy=False;self.finished=False
        self.handle=u.register_slate_post_tick_callback(self.tick)

    def shot(self,name):
        u.SystemLibrary.execute_console_command(self.world,'HighResShot 1280x720 filename='+(self.out/(name+'.png')).as_posix(),self.pc)
        self.captures.append(name+'.png')

    def place(self,near):
        h=self.hero;e=self.event;t=e.active_target
        center,extent,_=u.SystemLibrary.get_component_bounds(t.body)
        tongue=u.GameplayStatics.get_all_actors_of_class(self.world,u.MCTongue)[0]
        tongue_center,_,_=u.SystemLibrary.get_component_bounds(tongue.surface)
        base=math.atan2(tongue_center.y-center.y,tongue_center.x-center.x)
        ignore=[a for a in u.GameplayStatics.get_all_actors_of_class(self.world,u.Actor) if a!=tongue]
        for i in range(32):
            angle=base+((i+1)//2)*(.18 if near else .12)*(1 if i%2 else -1)
            direction=u.Vector(math.cos(angle),math.sin(angle),0)
            radius=(abs(direction.x)*extent.x+abs(direction.y)*extent.y)+ (110 if near else 1350)
            point=center+direction*radius
            hit=u.SystemLibrary.line_trace_single(self.world,point+u.Vector(0,0,300),point-u.Vector(0,0,2200),u.TraceTypeQuery.ECC_VISIBILITY,False,ignore,u.DrawDebugTrace.NONE)
            if not hit:continue
            floor=hit.to_tuple()[5]
            pos=floor+u.Vector(0,0,h.capsule_component.get_scaled_capsule_half_height()+3)
            h.set_actor_location(pos,False,True)
            h.character_movement.stop_movement_immediately()
            h.character_movement.set_movement_mode(u.MovementMode.MOVE_WALKING)
            rot=u.MathLibrary.find_look_at_rotation(pos,center)
            self.pc.set_control_rotation(rot)
            h.set_actor_rotation(u.Rotator(0,rot.yaw,0),True)
            if (e.call_method('CanMarkTarget',(h,)) if near else not e.should_reveal_marker(h)):
                view=h.camera.get_world_rotation()
                yaw=(rot.yaw-view.yaw+180)%360-180
                sensitivity=h.camera_orbit_sensitivity
                h.apply_camera_orbit_input(u.Vector2D(yaw/sensitivity,(-12-view.pitch)/sensitivity))
                return
        raise AssertionError('No supported '+('near' if near else 'far')+' placement')

    def done(self,error=None):
        if self.finished:return
        self.finished=True
        if self.handle is not None:u.unregister_slate_post_tick_callback(self.handle);self.handle=None
        data={'passed':error is None,'error':error,'checks':self.rows,'played_sounds':sorted(self.played),'captures':self.captures,'final_warning_seconds':1.5,'search_seconds':7,'mode':'single local PIE; one resolved strike, next search, then cancellation; F3 event; normal E/RMB entry points'}
        (self.out/'SoloRuntime.json').write_text(json.dumps(data,indent=2),encoding='utf-8')
        print('FOG_SEARCH_SOLO_RESULT',json.dumps(data))

    def tick(self,dt):
        if self.busy or self.finished:return
        self.busy=True
        try:
            assert time.monotonic()-self.started<100,'Solo check exceeded wall limit'
            events=u.GameplayStatics.get_all_actors_of_class(self.world,u.MCFogBrawlEvent)
            if not events:return
            self.event=NativeFogView(events[0]);e=self.event
            self.hero=u.GameplayStatics.get_player_pawn(self.world,0);h=self.hero
            e.set_editor_property('TotalStrikes',2)
            now=u.GameplayStatics.get_time_seconds(self.world)
            for audio in e.get_components_by_class(u.AudioComponent):
                if audio.is_playing() and audio.sound:self.played.add(audio.sound.get_name())
            if self.phase==0 and e.stage==u.MCFogBrawlStage.WARNING:
                assert abs(e.get_editor_property('SearchSeconds')-7)<.001 and abs(e.warning_seconds-1.5)<.001
                assert 8<e.warning_remaining()<=8.5
                self.first_target=e.active_target
                self.place(False)
                assert not e.call_method('CanMarkTarget',(h,))
                assert not e.call_method('MarkTarget',(h,e.active_target))
                self.rows.append({'check':'search starts; distant mark rejected','remaining':e.warning_remaining()})
                self.at=now;self.phase=1
            elif self.phase==1 and now-self.at>.45:
                assert not e.should_reveal_marker(h) and not e.target_glow.is_visible()
                self.shot('01_SearchFog')
                self.at=now;self.phase=11
            elif self.phase==11 and now-self.at>.15:
                self.place(True)
                self.near_position=h.get_actor_location()
                assert not e.call_method('MarkTarget',(h,None)),'Stale/invalid target accepted'
                h.set_handle_input_held(True);self.mark_at=now;self.phase=2
            elif self.phase==2 and now-self.mark_at>.2:
                h.set_handle_input_held(False)
                assert e.get_editor_property('bTargetMarked'),'Ordinary E did not mark the active tooth'
                assert not e.call_method('MarkTarget',(h,e.active_target)),'Duplicate mark accepted'
                self.rows.append({'check':'ordinary E sets authoritative shared marker','remaining':e.warning_remaining()})
                self.place(False);self.at=now;self.phase=3
            elif self.phase==3:
                if now-self.at>.35 and not any(x=='02_TeamBeacon.png' for x in self.captures):
                    assert e.get_editor_property('bTargetMarked')
                    signal=next(c for c in e.get_components_by_class(u.ProceduralMeshComponent) if c.get_name()=='FogSearchAndTeamBeacon')
                    assert signal.is_visible() and signal.get_num_sections()>=3
                    self.shot('02_TeamBeacon')
                if e.call_method('IsFinalWarning',()) and e.warning_remaining()<1.3:
                    self.final_at=now
                    fog=next(c for c in e.get_components_by_class(u.ExponentialHeightFogComponent) if c.get_name()=='BrawlHeightFog')
                    threat=next(c for c in e.get_components_by_class(u.ProceduralMeshComponent) if c.get_name()=='FogIncomingStrikeDirection')
                    assert fog.fog_density<.005 and e.should_reveal_marker(h) and e.target_glow.is_visible() and threat.is_visible()
                    self.rows.append({'check':'final reveal reaches distant player; smoke opens; threat visible','remaining':e.warning_remaining(),'density':fog.fog_density})
                    self.shot('03_FinalWarning');self.phase=4
            elif self.phase==4:
                if e.warning_remaining()<.85:
                    h.set_actor_location(self.near_position,False,True)
                    h.character_movement.stop_movement_immediately()
                    h.character_movement.set_movement_mode(u.MovementMode.MOVE_WALKING)
                    h.set_brace_input_held(True);self.phase=5
            elif self.phase==5 and e.strikes_resolved==1:
                assert e.last_tooth_saved and not e.get_editor_property('bTargetMarked')
                self.rows.append({'check':'ordinary RMB saves tooth; impact clears marker','guards':e.last_guard_count})
                h.set_brace_input_held(False);self.phase=6
            elif self.phase==6 and e.stage==u.MCFogBrawlStage.WARNING:
                assert not e.get_editor_property('bTargetMarked') and not e.call_method('IsFinalWarning',())
                self.rows.append({'check':'next target starts a fresh seven second search','remaining':e.warning_remaining()})
                e.stop()
                for c in e.get_components_by_class(u.ProceduralMeshComponent):assert not c.is_visible() and c.get_num_sections()==0,c.get_name()
                for c in e.get_components_by_class(u.AudioComponent):assert not c.is_playing(),c.get_name()
                for c in e.get_components_by_class(u.ExponentialHeightFogComponent):assert not c.is_visible(),c.get_name()
                self.rows.append({'check':'cancel clears smoke, markers, direction mesh and audio'})
                self.done()
        except Exception:self.done(traceback.format_exc())
        finally:self.busy=False

_fog_search_review=FogSearchReview()
print('FOG_SEARCH_SOLO_INSTALLED')
