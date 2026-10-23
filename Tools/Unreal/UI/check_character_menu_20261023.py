"""Short solo check of the saved menu: readiness, motion and two loop wraps."""
import unreal as u,builtins,json,time,traceback,math
from pathlib import Path

OUT=Path(u.Paths.project_dir())/'Artifacts/CharacterMenu20261023'
levels=u.get_editor_subsystem(u.LevelEditorSubsystem)
world=u.load_asset('/Game/Maps/L_MainMenu')
assert world.get_path_name()=='/Game/Maps/L_MainMenu.L_MainMenu'
seq=u.load_asset('/Game/UI/MenuShowcase/AnatomyV2/LS_MainMenuCleaningV2')
assert seq.get_playback_start()==0 and seq.get_playback_end()==219
bindings={b.get_name():[o.get_path_name() for o in seq.locate_bound_objects(b,world)] for b in seq.get_bindings()}
assert all(len(v)==1 for v in bindings.values()),bindings

class MenuCheck:
    def __init__(self):
        self.started=time.monotonic();self.phase='waiting';self.wraps=0;self.last=0.;self.path=[];self.first=None;self.handle=None
        self.report={'status':'running','bindings':bindings,'sequence_frames':[0,219],'network_test':False,'build_or_package':False}
        self.handle=u.register_slate_post_tick_callback(self.tick)
    def write(self):(OUT/'Validation.json').write_text(json.dumps(self.report,indent=2),encoding='utf8')
    def finish(self,error=None):
        if self.handle:u.unregister_slate_post_tick_callback(self.handle);self.handle=None
        self.report.update(status='failed' if error else 'passed',error=error,wraps=self.wraps,elapsed_seconds=time.monotonic()-self.started,samples=self.path)
        self.write();u.log('CHARACTER_MENU_SOLO '+self.report['status'])
    def pose(self):
        c=self.hero.skeletal_mesh_component
        return {n:[p.x,p.y,p.z] for n in ['head_x','hand_l','hand_r','foot_l','foot_r','MenuBrush_brush'] for p in [c.get_socket_location(n)]}
    def tick(self,dt):
        try:
            if time.monotonic()-self.started>100:raise RuntimeError('Solo menu validation timed out')
            if self.phase=='waiting':
                worlds=u.EditorLevelLibrary.get_pie_worlds(False)
                if not worlds:return
                self.world=worlds[0]
                loading=next(s for s in u.ObjectIterator(u.MCWorldPreloadSubsystem) if s.get_world()==self.world)
                if not loading.is_ready():return
                widgets=[w for w in u.ObjectIterator(u.MCMainMenuWidget) if w.is_in_viewport() and w.get_visibility()!=u.SlateVisibility.COLLAPSED]
                if not widgets:return
                actors=u.GameplayStatics.get_all_actors_of_class(self.world,u.Actor)
                self.hero=next(a for a in actors if 'MC_MenuV2_Hero' in [str(t) for t in a.tags]);self.actor=next(a for a in actors if isinstance(a,u.LevelSequenceActor));self.player=self.actor.sequence_player
                c=self.hero.skeletal_mesh_component
                assert c.skeletal_mesh_asset.get_path_name()=='/Game/UI/MenuShowcase/CharacterMenu20261023/SK_CharacterMenu.SK_CharacterMenu'
                assert self.actor.playback_settings.loop_count.value==-1
                self.report.update(preload_ready=True,preload_phase=str(loading.get_phase()),menu_visible=True,materials=[c.get_material(i).get_path_name() for i in range(c.get_num_materials())],hero_transform=str(self.hero.get_actor_transform()),clip_seconds=9.125)
                p=u.MovieSceneSequencePlaybackParams();p.position_type=u.MovieScenePositionType.FRAME;p.frame=u.FrameTime(u.FrameNumber(0));p.update_method=u.UpdatePositionMethod.JUMP;self.player.set_playback_position(p);self.player.play()
                self.phase='playing';self.last=0;self.last_sample=-1.;self.played_at=time.monotonic();self.write();return
            qt=self.player.get_current_time();frame=qt.time.frame_number.value+qt.time.sub_frame
            if frame+5<self.last:self.wraps+=1
            self.last=frame
            if frame-self.last_sample>12 or frame<self.last_sample:
                pose=self.pose();self.path.append({'wrap':self.wraps,'frame':frame,'bones':pose,'blink':self.hero.skeletal_mesh_component.get_anim_instance().get_curve_value('Eyes_Blink')});self.last_sample=frame
                if self.first is None:self.first=pose
            if self.wraps>=2:
                max_motion={n:max(math.dist(self.first[n],s['bones'][n]) for s in self.path) for n in self.first}
                assert max_motion['hand_l']>5 and max_motion['MenuBrush_brush']>10,max_motion
                assert max(s['blink'] for s in self.path)>.2
                self.report.update(max_motion_cm=max_motion,natural_play_seconds=time.monotonic()-self.played_at,autoplay=True,infinite_loop=True)
                self.finish()
        except Exception:self.finish(traceback.format_exc())

builtins.mc_character_menu_check=MenuCheck()
if not levels.is_in_play_in_editor():levels.editor_request_begin_play()
print('CHARACTER_MENU_SOLO_STARTED')
