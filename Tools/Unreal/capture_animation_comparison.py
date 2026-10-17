"""Capture the actual PIE native animation proxies at current game tuning.

Run with Tools/Unreal/remote_python.py in L_VFXLab. Only transient PIE actors
and in-memory timing settings are changed. Rendered frames retain 15 fps;
the world and both solvers evaluate at a fixed 30 fps during this comparison.
"""
import builtins
import json
import time
from pathlib import Path
import unreal as u

class AnimationCapture:
    def __init__(self):
        self.callback=None
        self.capture=None
        self.fill=None
        self.target=None
        self.engine=None
        self.perf=None
        self.old_engine=None
        self.old_perf=None
        self.started=time.monotonic()
        self.output=Path(globals().get('MC_ANIMATION_CAPTURE_OUTPUT',
            Path(u.Paths.project_dir(),'Artifacts/AnimationComparison_20261010_Lit'))).resolve()
        self.output.mkdir(parents=True,exist_ok=True)
        self.report={'status':'running','capture':'actual PIE SceneCapture2D output',
            'map':'/Game/Tests/L_VFXLab','station':22,'scenario':'Phase3PunchRight',
            'game_fps':30,'source_fps':15,'settings':'current game tuning; no amplification',
            'lighting':'lab lighting plus identical transient frontal fill for all three runs',
            'frames':[],'modes':[]}
        self.world=u.EditorLevelLibrary.get_pie_worlds(False)[0]
        self.pc=u.GameplayStatics.get_player_controller(self.world,0)
        self.lab=u.GameplayStatics.get_all_actors_of_class(self.world,u.MCVFXLab)[0]
        library=u.get_default_object(u.load_class(None,'/Script/Engine.SubsystemBlueprintLibrary'))
        self.comparison=library.call_method('GetWorldSubsystem',args=(self.world,u.MCAnimationComparisonSubsystem))
        self.old_mode=self.comparison.call_method('GetMode')
        self.modes=[('Native',u.MCSecondaryAnimationMode.NATIVE),
                    ('StylizedDynamics',u.MCSecondaryAnimationMode.STYLIZED_DYNAMICS),
                    ('KawaiiPhysics',u.MCSecondaryAnimationMode.KAWAII_PHYSICS)]
        self.pc.call_method('ServerSelectStation',args=(22,))
        self.station=self.lab.get_editor_property('results')[22].get_editor_property('station')
        self.floor=self.station.get_editor_property('floor')
        self.engine=next(o for o in u.ObjectIterator(u.load_class(None,'/Script/Engine.Engine'))
                         if o.get_path_name().startswith('/Engine/Transient.'))
        self.old_engine=(self.engine.get_editor_property('bUseFixedFrameRate'),self.engine.get_editor_property('FixedFrameRate'))
        self.engine.set_editor_property('FixedFrameRate',30.)
        self.engine.set_editor_property('bUseFixedFrameRate',True)
        self.perf=u.get_default_object(u.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'))
        self.old_perf=self.perf.get_editor_property('bThrottleCPUWhenNotForeground')
        self.perf.set_editor_property('bThrottleCPUWhenNotForeground',False)
        lib=u.get_default_object(u.GameplayStatics)
        self.capture=lib.call_method('BeginDeferredActorSpawnFromClass',args=(self.world,u.SceneCapture2D,u.Transform(),
            u.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN,None,u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        lib.call_method('FinishSpawningActor',args=(self.capture,u.Transform(),u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        self.component=self.capture.get_editor_property('capture_component2d')
        self.target=u.RenderingLibrary.create_render_target2d(self.world,640,720,
            u.TextureRenderTargetFormat.RTF_RGBA8,u.LinearColor(.025,.03,.045,1),False,False)
        for key,value in dict(texture_target=self.target,capture_source=u.SceneCaptureSource.SCS_FINAL_COLOR_LDR,
            capture_every_frame=False,capture_on_movement=False,always_persist_rendering_state=True,
            projection_type=u.CameraProjectionMode.ORTHOGRAPHIC,ortho_width=690.,
            primitive_render_mode=u.SceneCapturePrimitiveRenderMode.PRM_USE_SHOW_ONLY_LIST).items():
            self.component.set_editor_property(key,value)
        self.component.set_editor_property('show_flag_settings',[
            u.EngineShowFlagsSetting(show_flag_name=name,enabled=False)
            for name in ('MotionBlur','Fog','VolumetricFog','Atmosphere')])
        base=self.station.get_actor_location()
        eye=base+u.Vector(1100,-650,520)
        aim=base+u.Vector(0,0,180)
        self.capture.set_actor_location_and_rotation(eye,u.MathLibrary.find_look_at_rotation(eye,aim),False,True)
        light_pose=u.Transform(location=eye,rotation=u.MathLibrary.find_look_at_rotation(eye,aim))
        self.fill=lib.call_method('BeginDeferredActorSpawnFromClass',args=(self.world,u.DirectionalLight,light_pose,
            u.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN,None,u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        lib.call_method('FinishSpawningActor',args=(self.fill,light_pose,u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        light=self.fill.get_editor_property('light_component')
        light.set_editor_property('mobility',u.ComponentMobility.MOVABLE)
        light.set_editor_property('intensity',8.)
        light.set_editor_property('cast_shadows',False)
        self.index=-1
        self.frame=0
        self.wait=0
        self.boss=None
        self.advance()
        self.callback=u.register_slate_post_tick_callback(self.tick)

    def advance(self):
        self.index+=1
        if self.index==len(self.modes):
            self.finish()
            return
        name,mode=self.modes[self.index]
        if not self.comparison.call_method('IsModeAvailable',args=(mode,)):
            raise RuntimeError('Unavailable mode '+name)
        self.comparison.call_method('SetMode',args=(mode,))
        self.lab.call_method('ResetStation',args=(22,))
        self.directory=self.output/name
        self.directory.mkdir(exist_ok=True)
        if any(self.directory.glob('*.png')):
            raise RuntimeError('Capture directory already contains frames: '+str(self.directory))
        self.frame=0
        self.wait=0
        self.boss=None
        self.report['modes'].append(name)

    def tick(self,dt):
        try:
            if time.monotonic()-self.started>100:
                raise RuntimeError('Capture timeout')
            if self.boss is None:
                bosses=u.GameplayStatics.get_all_actors_of_class(self.world,u.MCBossCharacter)
                if not bosses:return
                self.boss=bosses[0]
                mesh=self.boss.get_editor_property('mesh')
                mesh.set_editor_property('visibility_based_anim_tick_option',u.VisibilityBasedAnimTickOption.ALWAYS_TICK_POSE_AND_REFRESH_BONES)
                self.component.clear_show_only_components()
                self.component.show_only_actor_components(self.floor)
                self.component.show_only_actor_components(self.boss)
            self.wait+=1
            if self.wait<3 or self.wait%2==0:return
            self.component.capture_scene()
            filename='%04d.png'%(self.frame+1)
            u.RenderingLibrary.export_render_target(self.world,self.target,str(self.directory),filename)
            if not (self.directory/filename).is_file():raise RuntimeError('Missing frame '+filename)
            samples=self.comparison.call_method('GetSamples')
            values=[{'mesh':s.get_editor_property('MeshName'),
                'active_bones':s.get_editor_property('ActiveBones'),
                'angle':s.get_editor_property('MaxAngleDegrees'),
                'translation':s.get_editor_property('MaxTranslation'),
                'evaluations':s.get_editor_property('Evaluations')} for s in samples
                if self.boss.get_name() in s.get_editor_property('MeshName')]
            self.report['frames'].append({'mode':self.modes[self.index][0],
                'frame':self.frame+1,'game_time':u.GameplayStatics.get_time_seconds(self.world),
                'game_dt':u.GameplayStatics.get_world_delta_seconds(self.world),'samples':values})
            self.frame+=1
            if self.frame==1:self.write()
            if self.frame>=75:self.advance()
        except Exception as error:self.finish(str(error))

    def write(self):
        (self.output/'Capture.json').write_text(json.dumps(self.report,indent=2),encoding='utf-8')

    def finish(self,error=None):
        if self.callback is not None:
            u.unregister_slate_post_tick_callback(self.callback)
            self.callback=None
        if self.comparison:self.comparison.call_method('SetMode',args=(self.old_mode,))
        if self.target:
            self.component.set_editor_property('texture_target',None)
            u.RenderingLibrary.release_render_target2d(self.target)
            self.target=None
        if self.capture:
            self.capture.destroy_actor()
            self.capture=None
        if self.fill:
            self.fill.destroy_actor()
            self.fill=None
        if self.old_engine:
            self.engine.set_editor_property('bUseFixedFrameRate',self.old_engine[0])
            self.engine.set_editor_property('FixedFrameRate',self.old_engine[1])
        if self.old_perf is not None:self.perf.set_editor_property('bThrottleCPUWhenNotForeground',self.old_perf)
        self.report['status']='failed' if error else 'complete'
        self.report['error']=error
        self.report['wall_seconds']=time.monotonic()-self.started
        self.write()
        u.log('MC_ANIMATION_CAPTURE '+self.report['status']+' '+str(error or self.output))

capture=AnimationCapture.__new__(AnimationCapture)
builtins.mc_animation_capture=capture
try:capture.__init__()
except Exception as error:
    capture.finish(str(error))
    raise
print('Animation capture started: '+str(capture.output))
