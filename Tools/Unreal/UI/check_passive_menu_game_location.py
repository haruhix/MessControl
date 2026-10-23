"""Check the actual menu for native game scenery, no mechanics and a loop wrap."""
import unreal as u,json,builtins,time,traceback,hashlib,math
from pathlib import Path
ROOT=Path(u.Paths.project_dir()).resolve();OUT=ROOT/'Saved/MenuGameLocation20261023'
levels=u.get_editor_subsystem(u.LevelEditorSubsystem);assert not levels.is_in_play_in_editor()
u.MCGameplayUIEditorLibrary.close_main_menu_preview();u.LevelSequenceEditorBlueprintLibrary.close_level_sequence()
report=json.loads((OUT/'Integration.json').read_text());world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
assert world.get_path_name()=='/Game/Maps/L_MainMenu.L_MainMenu'
assert hashlib.sha256((ROOT/'Content/Maps/L_Mouth.umap').read_bytes()).hexdigest()==report['source_sha256']
actors=u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors()
environment=[a for a in actors if 'MC_MenuGameLocation' in [str(t) for t in a.tags]]
assert len(environment)==41,len(environment)
assert all(a.get_class() in [u.StaticMeshActor.static_class(),*[c.static_class() for c in (u.DirectionalLight,u.SkyLight,u.SkyAtmosphere,u.VolumetricCloud,u.ExponentialHeightFog,u.PostProcessVolume)]] for a in environment)
assert all(c.get_collision_enabled()==u.CollisionEnabled.NO_COLLISION for a in environment for c in a.get_components_by_class(u.PrimitiveComponent))
seq=next(a.get_sequence() for a in actors if isinstance(a,u.LevelSequenceActor));assert seq.get_playback_end()==219
assert len(seq.get_bindings())==2
assert all(len(seq.locate_bound_objects(b,world))==1 for b in seq.get_bindings())
settings=u.load_object(None,'/Script/UnrealEd.Default__LevelEditorPlaySettings')
values={'playNetMode':'PIE_Standalone','playNumberOfClients':1,'runUnderOneProcess':True,'bLaunchSeparateServer':False,'newWindowWidth':1600,'newWindowHeight':900}
before=json.loads(u.ToolsetLibrary.get_object_properties(settings,list(values)))
perf=u.get_default_object(u.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'))
(OUT/'PlaySettingsBefore.json').write_text(json.dumps({'play':before,'throttle':perf.get_editor_property('bThrottleCPUWhenNotForeground')},indent=2))
assert u.ToolsetLibrary.set_object_properties(settings,json.dumps(values));perf.set_editor_property('bThrottleCPUWhenNotForeground',False)

class Check:
    def __init__(self):
        self.started=time.monotonic();self.phase='waiting';self.last=0.;self.samples=[];self.handle=u.register_slate_post_tick_callback(self.tick)
        self.report={'status':'running','saved_environment_count':41,'source_map_unchanged':True,'saved_collision_disabled':True,'saved_bindings_resolve':True,'network_test':False,'build_or_package':False}
    def write(self):(OUT/'Validation.json').write_text(json.dumps(self.report,indent=2),encoding='utf8')
    def finish(self,error=None):
        if self.handle:u.unregister_slate_post_tick_callback(self.handle);self.handle=None
        self.report.update(status='failed' if error else 'passed',error=error,elapsed_seconds=time.monotonic()-self.started,samples=self.samples);self.write()
        u.log('PASSIVE_MENU_LOCATION_CHECK '+self.report['status'])
    def tick(self,dt):
        try:
            if time.monotonic()-self.started>120:raise RuntimeError('Menu validation timed out')
            if self.phase=='waiting':
                worlds=u.EditorLevelLibrary.get_pie_worlds(False)
                if not worlds:return
                self.world=worlds[0]
                loading=next(s for s in u.ObjectIterator(u.MCWorldPreloadSubsystem) if s.get_world()==self.world)
                if loading.has_failed():raise RuntimeError(str(loading.get_detail_text()))
                if not loading.is_ready():return
                if not any(w.is_in_viewport() and w.get_visibility()!=u.SlateVisibility.COLLAPSED for w in u.ObjectIterator(u.MCMainMenuWidget)):return
                aa=u.GameplayStatics.get_all_actors_of_class(self.world,u.Actor)
                forbidden=['MCThroat','MCTongue','MCArenaToothSocket','MCArenaTooth','MCFoodActor','MCBossCharacter','MCNutEnemy','MCRoguelikeDirector','MCFoodDisposal','MCToothCharacter','MCRewardDropZone','MCGameDirector','MCHazardWave','MCFogBrawlEvent']
                bad=[a.get_class().get_name() for a in aa if any(isinstance(a,getattr(u,n)) for n in forbidden if hasattr(u,n))]
                assert not bad,bad
                assert u.GameplayStatics.get_game_mode(self.world).get_class().get_name()=='MCMainMenuGameMode'
                assert u.GameplayStatics.get_player_pawn(self.world,0) is None
                self.hero=next(a for a in aa if 'MC_MenuV2_Hero' in [str(t) for t in a.tags]);a=next(a for a in aa if isinstance(a,u.LevelSequenceActor));self.player=a.sequence_player
                assert a.playback_settings.loop_count.value==-1
                self.report.update(preload_ready=True,menu_visible=True,gameplay_actors=bad,player_pawn=None,game_mode='MCMainMenuGameMode',infinite_animation_loop=True)
                p=u.MovieSceneSequencePlaybackParams();p.position_type=u.MovieScenePositionType.FRAME;p.frame=u.FrameTime(u.FrameNumber(0));p.update_method=u.UpdatePositionMethod.JUMP;self.player.set_playback_position(p);self.player.play()
                self.phase='playing';self.write();return
            qt=self.player.get_current_time();frame=qt.time.frame_number.value+qt.time.sub_frame
            p=self.hero.skeletal_mesh_component.get_socket_location('MenuBrush_brush')
            if not self.samples or frame-self.samples[-1]['frame']>12:self.samples.append({'frame':frame,'brush':list(p.to_tuple())})
            if frame+5<self.last:
                motion=max(math.dist(self.samples[0]['brush'],s['brush']) for s in self.samples)
                assert motion>10,motion
                self.report.update(natural_loop_wraps=1,brush_motion_cm=motion)
                self.finish()
            self.last=frame
        except Exception:self.finish(traceback.format_exc())
builtins.mc_passive_menu_location_check=Check();levels.editor_request_begin_play()
print('PASSIVE_MENU_SOLO_STARTED')
