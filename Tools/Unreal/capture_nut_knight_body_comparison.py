"""Render identical frozen animation poses with linear and dual-quaternion skinning.

Disposable solo PIE actors only. Bone poses, meshes, materials, camera and lighting
are identical between each pair; only the component's GPU deformer differs.
"""
import unreal as u,builtins,json,time,traceback
from pathlib import Path

class BodyComparison:
 def __init__(self):
  check=builtins.mc_nut_knight_check;assert check.done
  self.out=Path(u.Paths.project_dir())/'Artifacts/NutKnightBodyFix_20261017/Comparison';self.out.mkdir(exist_ok=True)
  self.world=check.world;self.hero=check.hero;self.hero_hidden=self.hero.get_editor_property('hidden');self.hero.set_actor_hidden_in_game(True)
  self.lib=u.get_default_object(u.GameplayStatics);self.actors=[];self.rows=[];self.index=-1;self.started=time.monotonic();self.done=False
  self.origin=check.floor(check.anchor);assert self.origin is not None
  profile=u.load_asset('/Game/Gameplay/CoreLoop/DA_NutRain').get_editor_property('settings').get_editor_property('boss')
  self.graph=u.load_asset('/DeformerGraph/Deformers/DG_DualQuatSkin_Morph_Cloth')
  for label in ['linear','dual_quaternion']:
   a=self.spawn(u.SkeletalMeshActor,u.Transform(location=self.origin));comp=a.get_editor_property('skeletal_mesh_component')
   comp.set_collision_enabled(u.CollisionEnabled.NO_COLLISION)
   comp.set_editor_property('visibility_based_anim_tick_option',u.VisibilityBasedAnimTickOption.ALWAYS_TICK_POSE_AND_REFRESH_BONES)
   comp.set_animation_mode(u.AnimationMode.ANIMATION_SINGLE_NODE)
   a.set_actor_scale3d(check.component_scale);a.set_actor_rotation(u.Rotator(yaw=profile.get_editor_property('TankModelYaw')),False)
   self.actors.append(a)
  self.capture=self.spawn(u.SceneCapture2D,u.Transform());self.cc=self.capture.get_editor_property('capture_component2d')
  self.target=u.RenderingLibrary.create_render_target2d(self.world,960,720,u.TextureRenderTargetFormat.RTF_RGBA8,u.LinearColor(.03,.035,.05,1),False,False)
  self.cc.set_editor_properties(dict(texture_target=self.target,capture_source=u.SceneCaptureSource.SCS_FINAL_COLOR_LDR,capture_every_frame=False,capture_on_movement=False,always_persist_rendering_state=True,fov_angle=45.))
  self.cc.set_editor_property('post_process_settings',self.hero.get_editor_property('Camera').get_editor_property('post_process_settings'))
  self.cases=[(m,label,t) for m in ['SK_NutTank','SK_NutTank_NoShield'] for label,t in [('Idle',.8),('HeadLeft',.9),('HeadRight',.9)]]
  self.next_case();self.handle=u.register_slate_post_tick_callback(self.tick)
 def spawn(self,cls,pose):
  a=self.lib.call_method('BeginDeferredActorSpawnFromClass',args=(self.world,cls,pose,u.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN,None,u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
  self.lib.call_method('FinishSpawningActor',args=(a,pose,u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT));return a
 def next_case(self):
  self.index+=1
  if self.index>=len(self.cases):self.finish();return
  self.mesh_name,self.label,self.seconds=self.cases[self.index]
  mesh=u.load_asset('/Game/Gameplay/CoreLoop/NutTank/'+self.mesh_name)
  clip=u.load_asset('/Game/Gameplay/CoreLoop/NutTank/Animations/KnightBallUpdate/A_NutKnight_'+self.label)
  for i,a in enumerate(self.actors):
   comp=a.get_editor_property('skeletal_mesh_component');comp.set_skeletal_mesh_asset(mesh)
   comp.set_animation(clip);comp.set_play_rate(0.);comp.set_position(self.seconds,False)
   # The reflected bSetMeshDeformer property shadows the Python method name.
   if self.index==0:comp.call_method('SetMeshDeformer',args=(None if i==0 else self.graph,))
   a.set_actor_hidden_in_game(False)
  self.at=time.monotonic();self.phase=0
 def tick(self,dt):
  if self.done:return
  try:
   assert time.monotonic()-self.started<45
   age=time.monotonic()-self.at
   if self.phase==0 and age>.6:
    self.record(0);self.phase=1;self.at=time.monotonic()
   elif self.phase==1 and age>.35:
    self.record(1);self.next_case()
  except Exception:self.finish(traceback.format_exc())
 def record(self,index):
  comp=self.actors[index].get_editor_property('skeletal_mesh_component')
  if index==1:assert comp.get_mesh_deformer_instance() is not None
  self.cc.set_editor_property('hidden_actors',[self.actors[1-index]])
  aim=self.origin+u.Vector(0,0,220);eye=aim+u.Vector(700,-1100,300)
  self.capture.set_actor_location_and_rotation(eye,u.MathLibrary.find_look_at_rotation(eye,aim),False,True);self.cc.capture_scene()
  mode='linear' if index==0 else 'dual_quaternion';name=f'{self.mesh_name}_{self.label}_{mode}.png'
  u.RenderingLibrary.export_render_target(self.world,self.target,str(self.out),name)
  bones={n:comp.get_socket_transform(n,u.RelativeTransformSpace.RTS_COMPONENT).export_text() for n in ['root_x','head_x','hand_r','foot_l']}
  if index==0:self.before=bones
  else:assert bones==self.before,'Comparison must use identical bone poses'
  self.rows.append({'mesh':self.mesh_name,'clip':self.label,'seconds':self.seconds,'skinning':mode,'file':str(self.out/name),'bones':bones})
 def finish(self,error=None):
  if self.done:return
  self.done=True
  if hasattr(self,'handle'):u.unregister_slate_post_tick_callback(self.handle)
  for a in self.actors+[self.capture]:
   if u.SystemLibrary.is_valid(a):a.destroy_actor()
  self.hero.set_actor_hidden_in_game(self.hero_hidden)
  (self.out/'Comparison.json').write_text(json.dumps({'passed':error is None,'error':error,'captures':self.rows,'scope':__doc__},indent=2))
  print('KNIGHT_BODY_COMPARISON',error or 'PASS')

builtins.mc_knight_body_comparison=BodyComparison()
