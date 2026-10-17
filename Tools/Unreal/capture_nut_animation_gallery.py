"""Capture every saved encounter AnimSequence at 30 Hz from two Unreal views.

Transient single-node previews in the existing solo PIE lab; no package saves.
Override MC_NUT_GALLERY_OUTPUT and MC_NUT_GALLERY_ROLES for later iterations.
"""
import builtins
import json
import math
import time
from pathlib import Path
import unreal as u

FPS = 30

def prop(obj, name, default=None):
    try: return obj.get_editor_property(name)
    except Exception: return default

def xyz(v): return [float(v.x), float(v.y), float(v.z)]

class NutAnimationGallery:
    def __init__(self):
        self.done=False
        self.callback=None
        self.actors=[]
        self.targets=[]
        self.old_engine=self.old_perf=None
        self.engine=self.perf=None
        self.started=time.monotonic()
        self.output=Path(globals().get('MC_NUT_GALLERY_OUTPUT', Path(u.Paths.project_dir(),'Artifacts/NutFullReview_20261010_Review'))).resolve()
        self.output.mkdir(parents=True,exist_ok=True)
        self.report=dict(status='running',fps=FPS,size=[640,480],capture='Actual Unreal SceneCapture2D frames',
            scope='RAW saved AnimSequence preview; full source timeline; single-node; no gameplay state machine, VFX or secondary solver',
            playback_speed=1,views=['front','side'],scenarios=[],frames=[],restoration=[],
            note='Tank procedural ball roll is gameplay rotation, not an additional AnimSequence. ShieldBreak previews the reaction on the no-shield mesh; mesh-removal timing is gameplay.')
        worlds=u.EditorLevelLibrary.get_pie_worlds(False)
        if len(worlds)!=1: raise RuntimeError('Requires existing solo PIE')
        self.world=worlds[0]
        self.report['map']=self.world.get_path_name()
        self.library=u.get_default_object(u.GameplayStatics)
        profile=u.load_asset('/Game/Gameplay/CoreLoop/DA_NutRain')
        self.boss=profile.get_editor_property('settings').get_editor_property('boss')
        roles=globals().get('MC_NUT_GALLERY_ROLES', ['tank','mage'])
        counts=json.loads(Path(u.Paths.project_dir(),'Saved/Checks/NutAnimationCounts.json').read_text(encoding='utf8'))
        self.cases=[c for c in counts['clips'] if c['role'] in roles]
        # Resolve all original assets now so a later profile update cannot change this review.
        for c in self.cases:
            actual=self.boss.get_editor_property(c['field'])
            c['clip']=actual
            if not isinstance(c['clip'],u.AnimSequence): raise RuntimeError('Missing sequence '+c['asset'])
            c['asset']=actual.get_path_name()
        self.report['binding_source']='Current saved DA_NutRain profile resolved at capture start; manifest supplies the 23 field names only'
        self.engine=next(o for o in u.ObjectIterator(u.load_class(None,'/Script/Engine.Engine')) if o.get_path_name().startswith('/Engine/Transient.'))
        self.old_engine=(prop(self.engine,'bUseFixedFrameRate'),prop(self.engine,'FixedFrameRate'))
        self.engine.set_editor_property('FixedFrameRate',float(FPS))
        self.engine.set_editor_property('bUseFixedFrameRate',True)
        self.perf=u.get_default_object(u.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'))
        self.old_perf=prop(self.perf,'bThrottleCPUWhenNotForeground')
        self.perf.set_editor_property('bThrottleCPUWhenNotForeground',False)
        # Far enough away to isolate the raw preview while reusing current lab illumination.
        self.origin=u.Vector(60000,40000,0)
        self.preview=self.spawn(u.SkeletalMeshActor,u.Transform(location=self.origin))
        self.model=prop(self.preview,'skeletal_mesh_component')
        self.model.set_collision_enabled(u.CollisionEnabled.NO_COLLISION)
        self.model.set_editor_property('visibility_based_anim_tick_option',u.VisibilityBasedAnimTickOption.ALWAYS_TICK_POSE_AND_REFRESH_BONES)
        self.model.set_editor_property('enable_update_rate_optimizations',False)
        self.floor=self.spawn(u.StaticMeshActor,u.Transform(location=self.origin+u.Vector(0,0,-12),scale=u.Vector(16,16,.20)))
        floor_mesh=prop(self.floor,'static_mesh_component')
        floor_mesh.set_editor_property('mobility',u.ComponentMobility.MOVABLE)
        floor_mesh.set_static_mesh(u.load_asset('/Engine/BasicShapes/Cube'))
        background=floor_mesh.create_dynamic_material_instance(0,u.load_asset('/Engine/BasicShapes/BasicShapeMaterial'))
        background.set_vector_parameter_value('Color',u.LinearColor(.18,.22,.28,1))
        background.set_scalar_parameter_value('Roughness',1.)
        floor_mesh.set_material(0,background)
        floor_mesh.set_collision_enabled(u.CollisionEnabled.NO_COLLISION)
        self.backgrounds=[self.floor]
        for offset,scale in ((u.Vector(-700,0,600),u.Vector(.2,30,20)),(u.Vector(0,800,600),u.Vector(30,.2,20))):
            wall=self.spawn(u.StaticMeshActor,u.Transform(location=self.origin+offset,scale=scale))
            wm=prop(wall,'static_mesh_component')
            wm.set_editor_property('mobility',u.ComponentMobility.MOVABLE)
            wm.set_static_mesh(u.load_asset('/Engine/BasicShapes/Cube'))
            wm.set_material(0,background)
            wm.set_collision_enabled(u.CollisionEnabled.NO_COLLISION)
            self.backgrounds.append(wall)
        self.cameras=[]
        for name in ('front','side'):
            a=self.spawn(u.SceneCapture2D,u.Transform())
            component=prop(a,'capture_component2d')
            target=u.RenderingLibrary.create_render_target2d(self.world,640,480,u.TextureRenderTargetFormat.RTF_RGBA8,u.LinearColor(.025,.03,.045,1),False,False)
            self.targets.append((component,target))
            for key,value in dict(texture_target=target,capture_source=u.SceneCaptureSource.SCS_FINAL_COLOR_LDR,
                capture_every_frame=False,capture_on_movement=False,always_persist_rendering_state=True,
                projection_type=u.CameraProjectionMode.ORTHOGRAPHIC,ortho_width=1100.,
                primitive_render_mode=u.SceneCapturePrimitiveRenderMode.PRM_USE_SHOW_ONLY_LIST).items(): component.set_editor_property(key,value)
            component.set_editor_property('show_flag_settings',[u.EngineShowFlagsSetting(show_flag_name=n,enabled=False) for n in ('MotionBlur','Fog','VolumetricFog','Atmosphere')])
            component.show_only_actor_components(self.preview)
            for background_actor in self.backgrounds:component.show_only_actor_components(background_actor)
            self.cameras.append((name,a,component,target))
        for direction,intensity in ((u.Vector(1100,-900,700),6.),(u.Vector(-600,800,400),4.)):
            light=self.spawn(u.DirectionalLight,u.Transform())
            lc=prop(light,'light_component')
            lc.set_editor_property('mobility',u.ComponentMobility.MOVABLE)
            light.set_actor_rotation(u.MathLibrary.find_look_at_rotation(direction,u.Vector()),True)
            lc.set_editor_property('intensity',intensity)
            lc.set_editor_property('cast_shadows',False)
        sky=self.spawn(u.SkyLight,u.Transform())
        sc=prop(sky,'light_component')
        sc.set_editor_property('mobility',u.ComponentMobility.MOVABLE)
        sc.set_editor_property('source_type',u.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
        sc.set_editor_property('cubemap',u.load_asset('/Engine/MapTemplates/Sky/DaylightAmbientCubemap'))
        sc.set_editor_property('intensity',1.5)
        sc.set_editor_property('cast_shadows',False)
        self.report['lighting']='Current lab + transient 6/4 directional fill and 1.5 daylight skylight; neutral gray preview backdrop; original character materials'
        self.index=-1
        self.last_game_time=None
        self.advance()
        self.callback=u.register_slate_post_tick_callback(self.tick)

    def spawn(self,cls,pose):
        a=self.library.call_method('BeginDeferredActorSpawnFromClass',args=(self.world,cls,pose,u.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN,None,u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        self.library.call_method('FinishSpawningActor',args=(a,pose,u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        self.actors.append(a)
        return a

    def advance(self):
        self.index+=1
        if self.index>=len(self.cases): self.finish(); return
        self.case=self.cases[self.index]
        clip=self.case['clip']
        role=self.case['role']
        self.name=clip.get_name()
        self.directory=self.output/self.name
        for view in ('front','side'):
            path=self.directory/view
            path.mkdir(parents=True,exist_ok=True)
            if any(path.glob('*.png')): raise RuntimeError('Refusing overwrite '+str(path))
        unshielded=role=='tank' and ('unshielded' in self.case['field'] or 'shield_break' in self.case['field'])
        mesh=self.boss.get_editor_property('tank_unshielded_skeletal_mesh' if unshielded else role+'_skeletal_mesh')
        if not mesh: raise RuntimeError('Missing mesh for '+role)
        if mesh.get_editor_property('skeleton')!=clip.get_editor_property('skeleton'): raise RuntimeError('Skeleton mismatch '+self.name)
        base=self.boss.get_editor_property(role+'_skeletal_mesh')
        bounds=base.get_bounds()
        extent=prop(bounds,'box_extent')
        center=prop(bounds,'origin')
        height=float(self.boss.get_editor_property(role+'_height'))
        yaw=float(self.boss.get_editor_property(role+'_model_yaw'))
        scale=height/max(1.,2*extent.z)
        offset=u.MathLibrary.rotate_angle_axis(center*scale,yaw,u.Vector(0,0,1))
        self.model.set_skeletal_mesh_asset(mesh)
        self.preview.set_actor_transform(u.Transform(location=self.origin+u.Vector(0,0,height*.5)-offset,rotation=u.Rotator(pitch=0,yaw=yaw,roll=0),scale=u.Vector(scale,scale,scale)),False,True)
        self.model.set_animation_mode(u.AnimationMode.ANIMATION_SINGLE_NODE,True)
        self.model.play_animation(clip,False)
        self.model.set_play_rate(0.)
        self.model.set_position(0.,False)
        seconds=float(clip.get_play_length())
        cycles=2 if any(x in self.case['field'] for x in ('idle','walk','hit')) else 4 if 'charge_loop' in self.case['field'] else 1
        self.cycle_frames=max(1,int(math.ceil(seconds*FPS-1e-5)))
        self.total=self.cycle_frames*cycles+1
        self.frame=0
        # Frame full hands, club, jump apex, and death without animated zoom.
        width=1160. if role=='tank' else 1050.
        aim=self.origin+u.Vector(35,0,height*.62)
        for name,a,component,target in self.cameras:
            eye=aim+(u.Vector(1100,-850,470) if name=='front' else u.Vector(50,-1400,200))
            a.set_actor_location_and_rotation(eye,u.MathLibrary.find_look_at_rotation(eye,aim),False,True)
            component.set_editor_property('ortho_width',width)
        self.report['scenarios'].append(dict(name=self.name,role=role,field=self.case['field'],asset=clip.get_path_name(),seconds=seconds,
            cycles=cycles,frames=0,expected_frames=self.total,mesh=mesh.get_path_name(),
            materials=[self.model.get_material(i).get_path_name() for i in range(self.model.get_num_materials())],
            model_scale=scale,model_yaw=yaw,camera_aim_cm=xyz(aim),ortho_width_cm=width,source_sample_interval_seconds=1/FPS))
        self.write()

    def position(self,frame):
        seconds=self.report['scenarios'][-1]['seconds']
        if frame==self.total-1: return seconds
        return min((frame%self.cycle_frames)/FPS,seconds)

    def tick(self,slate_dt):
        try:
            if time.monotonic()-self.started>900: raise RuntimeError('Gallery timeout')
            gt=float(u.GameplayStatics.get_time_seconds(self.world))
            if self.last_game_time is not None and gt<=self.last_game_time+1e-6:return
            self.last_game_time=gt
            expected=self.position(self.frame)
            if abs(float(self.model.get_position())-expected)>.001: raise RuntimeError('Preview position changed unexpectedly')
            bones={n:xyz(self.model.get_socket_location(n)) for n in ('root_x','head_x','hand_l','hand_r','foot_l','foot_r') if self.model.does_socket_exist(n)}
            filename='%04d.png'%(self.frame+1)
            for name,a,component,target in self.cameras:
                component.capture_scene()
                u.RenderingLibrary.export_render_target(self.world,target,str(self.directory/name),filename)
                if not (self.directory/name/filename).is_file():raise RuntimeError('Missing '+name+' '+filename)
            self.report['frames'].append(dict(scenario=self.name,frame=self.frame+1,source_time=expected,game_time=gt,pose_bones_cm=bones))
            self.frame+=1
            self.report['scenarios'][-1]['frames']=self.frame
            if self.frame%FPS==0 or self.frame==1:self.write()
            if self.frame>=self.total:self.advance()
            if not self.done:self.model.set_position(self.position(self.frame),False)
        except Exception as e:self.finish(str(e))

    def write(self):
        (self.output/'GalleryCapture.json').write_text(json.dumps(self.report,indent=2),encoding='utf8')

    def finish(self,error=None):
        if getattr(self,'done',False):return
        self.done=True
        failures=[]
        def restore(label,operation):
            try:operation();self.report['restoration'].append(label)
            except Exception as e:failures.append(label+': '+str(e))
        if self.callback is not None:restore('unregister preview callback',lambda:u.unregister_slate_post_tick_callback(self.callback));self.callback=None
        for component,target in self.targets:
            restore('detach target',lambda component=component:component.set_editor_property('texture_target',None))
            restore('release target',lambda target=target:u.RenderingLibrary.release_render_target2d(target))
        for a in reversed(self.actors):
            if u.SystemLibrary.is_valid(a):restore('destroy '+a.get_name(),a.destroy_actor)
        if self.old_engine:
            restore('fixed-frame switch',lambda:self.engine.set_editor_property('bUseFixedFrameRate',self.old_engine[0]))
            restore('fixed frame rate',lambda:self.engine.set_editor_property('FixedFrameRate',self.old_engine[1]))
        if self.old_perf is not None:restore('foreground throttle',lambda:self.perf.set_editor_property('bThrottleCPUWhenNotForeground',self.old_perf))
        self.report.update(status='failed' if error or failures else 'complete',error=error,restore_errors=failures,wall_seconds=time.monotonic()-self.started)
        self.write()
        u.log('MC_NUT_GALLERY '+self.report['status']+' '+str(error or self.output))

previous=getattr(builtins,'mc_nut_animation_gallery',None)
if previous is not None and not getattr(previous,'done',True):raise RuntimeError('A gallery capture is already active; await its completion')
capture=NutAnimationGallery.__new__(NutAnimationGallery)
builtins.mc_nut_animation_gallery=capture
try:capture.__init__()
except Exception as e:capture.finish(str(e));raise
print('Full saved animation gallery capture started: '+str(capture.output))
