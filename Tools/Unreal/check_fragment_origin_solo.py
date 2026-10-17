"""Short standalone regression: native knife cuts stay at their parent location.

Two PIE-only broccoli fixtures use20 fragments, Health1 and the ordinary held
Knife input. The second parent hovers75cm above the floor to detect floor snapping.
Never calls HitFood/SpawnFragments or moves generated fragments. Restores settings.
"""
import unreal as u,json,builtins,traceback,math,time
from pathlib import Path
root=Path(u.Paths.project_dir());out=root/'Artifacts/FragmentOrigin_20261017/Solo'
FOOD_THROW_OUTPUT=str(out);FOOD_THROW_INSTANTIATE=False
exec(compile((root/'Tools/Unreal/check_food_throw_solo.py').read_text(),'check_food_throw_solo.py','exec'))
class FragmentOriginCheck(FoodThrowCheck):
 def __init__(self):
  world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_game_world();spawn_hero=u.GameplayStatics.get_player_controller(world,0).get_controlled_pawn()
  spawn_position=spawn_hero.get_actor_location();spawn_rotation=spawn_hero.get_actor_rotation()
  super().__init__()
  if self.collection.get_editor_property('bCollecting'):self.hero.call_method('ServerToggleFoodCollection',args=())
  for f in self.fixtures:
   if u.SystemLibrary.is_valid(f):f.destroy_actor()
  self.fixtures=[];self.cases=[];self.samples=[];self.frames=[];self.index=0;self.stage='settle';self.at=self.now();self.parent=None;self.parts=[];self.before_bounds=None
  floor=self.floor(spawn_position)
  if floor is not None:
   spawn_rotation.pitch=0;spawn_rotation.roll=0
   self.hero.set_actor_location_and_rotation(floor+u.Vector(0,0,self.hero.capsule_component.get_scaled_capsule_half_height()+4),spawn_rotation,False,True)
   self.hero.character_movement.stop_movement_immediately()
  self.hero.inventory.server_select(u.MCToolSlot.KNIFE)
  forward=self.hero.get_actor_forward_vector();side=self.hero.get_actor_right_vector();hero_pos=self.hero.get_actor_location()
  eye=hero_pos-forward*200+side*320+u.Vector(0,0,190);aim=hero_pos+forward*100+u.Vector(0,0,20)
  self.camera.set_actor_location(hero_pos-forward*300+u.Vector(0,0,160),False,True)
  self.capture.set_actor_location_and_rotation(eye,u.MathLibrary.find_look_at_rotation(eye,aim),False,True)
  self.cc.set_editor_property('post_process_settings',self.hero.get_editor_property('Camera').get_editor_property('post_process_settings'))
  self.start_case()
 def bounds(self,actor):
  pieces=[u.SystemLibrary.get_component_bounds(actor.get_editor_property(n)) for n in ['body','visual']]
  lows=[a-b for a,b,c in pieces];highs=[a+b for a,b,c in pieces]
  lo=u.Vector(*(min(v.to_tuple()[i] for v in lows) for i in range(3)));hi=u.Vector(*(max(v.to_tuple()[i] for v in highs) for i in range(3)))
  return (lo+hi)*.5,(hi-lo)*.5
 def start_case(self):
  self.batch=191018+self.index;self.case_name=['ground','air'][self.index]
  hp=self.hero.get_actor_location();point=self.floor(hp+self.hero.get_actor_forward_vector()*120);assert point is not None
  mesh=u.load_asset('/Game/Stylized_Vegetables/Meshes/SM_Broccoli');pose=u.Transform(location=point+u.Vector(0,0,100),rotation=u.Rotator(0,35,0))
  p=self.lib.call_method('BeginDeferredActorSpawnFromClass',args=(self.w,u.MCFoodActor,pose,u.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN,None,u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
  row=p.get_editor_property('food_data');row.set_editor_property('kind',u.MCFoodKind.FOOD);row.set_editor_property('whole_meshes',[mesh]);row.set_editor_property('fragment_meshes',[mesh]);row.set_editor_property('scale',u.Vector(.55,.55,.55));row.set_editor_property('fragment_scale',u.Vector(.18,.18,.18));row.set_editor_property('fragments',20);row.set_editor_property('health',1.);row.set_editor_property('spoil_seconds',600.)
  p.set_editor_property('food_data',row);p.set_editor_property('item_name','FragmentOriginFixture');p.set_editor_property('item_mesh',mesh);p.set_editor_property('health',1.);p.set_editor_property('batch',self.batch)
  self.lib.call_method('FinishSpawningActor',args=(p,pose,u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
  _,extent=self.bounds(p);p.set_actor_location(point+u.Vector(0,0,extent.z+5+(75 if self.index else 0)),False,True)
  if self.index:p.body.set_enable_gravity(False)
  p.body.set_physics_linear_velocity(u.Vector());p.body.set_physics_angular_velocity_in_radians(u.Vector())
  self.parent=p;self.fixtures.append(p);self.parts=[];self.before_bounds=self.bounds(p);self.before_mass=p.body.get_mass()
  self.camera.set_actor_rotation(u.MathLibrary.find_look_at_rotation(self.camera.get_actor_location(),self.before_bounds[0]),True)
  self.go('settle')
 def tick(self,dt):
  if self.done:return
  try:
   assert time.monotonic()-self.started<240,'Wall-time limit'
   now=self.now()
   if now<=self.last:return
   self.last=now;age=now-self.at
   self.hero.set_editor_property('bInCoffee',False)
   state=self.hero.status.get_editor_property('State');state.set_editor_property('Health',100.);self.hero.status.set_editor_property('State',state)
   if self.parent and self.parent.get_editor_property('phase')!=u.MCFoodPhase.DISPOSED:self.before_bounds=self.bounds(self.parent)
   if self.stage=='settle' and age>.35:
    self.shot(self.case_name+'_Parent');self.hero.set_primary_input_held(True);self.go('cut')
   elif self.stage=='cut':
    assert age<5,'Knife did not cut '+self.case_name+' parent'
    if self.parent.get_editor_property('phase')==u.MCFoodPhase.DISPOSED:
     self.hero.set_primary_input_held(False)
     self.parts=[a for a in u.GameplayStatics.get_all_actors_of_class(self.w,u.MCFoodActor) if a.get_editor_property('batch')==self.batch and a.get_editor_property('fragment') and a.get_editor_property('phase')!=u.MCFoodPhase.DISPOSED]
     assert len(self.parts)==20,('Fragment count',len(self.parts))
     center,extent=self.before_bounds;birth=[]
     for f in self.parts:
      c,e=self.bounds(f);birth.append(dict(name=f.get_name(),position=list(c.to_tuple()),extent=list(e.to_tuple()),mass=f.body.get_mass(),spoils=f.get_editor_property('spoil_at')))
     mean=u.Vector(*(sum(p['position'][i] for p in birth)/len(birth) for i in range(3)))
     largest=max(math.hypot(*p['extent'][:2]) for p in birth)
     limit=max(math.hypot(extent.x,extent.y)*1.5,largest*3)+15
     horizontal=max(math.hypot(p['position'][0]-center.x,p['position'][1]-center.y) for p in birth)
     assert horizontal<limit,('A segment was spawned away from its parent',horizontal,limit)
     assert math.hypot(mean.x-center.x,mean.y-center.y)<8,('Pile is not centered on the cut',mean,center)
     min_bottom=min(p['position'][2]-p['extent'][2] for p in birth)
     assert min_bottom>=center.z-extent.z-5,('Fragments snapped beneath the parent',min_bottom,center.z-extent.z)
     assert abs(sum(p['mass'] for p in birth)-self.before_mass)<.1,'Fragment mass changed'
     self.result=dict(case=self.case_name,passed=True,count=len(birth),parent_center=list(center.to_tuple()),parent_extent=list(extent.to_tuple()),birth=birth,max_birth_horizontal_cm=horizontal,centroid_horizontal_error_cm=math.hypot(mean.x-center.x,mean.y-center.y),bottom_clearance_from_parent_min_cm=min_bottom-(center.z-extent.z),allowed_radius_cm=limit)
     self.shot(self.case_name+'_Birth');self.go('observe')
   elif self.stage=='observe':
    positions=[f.get_actor_location() for f in self.parts];center=u.Vector(*self.result['parent_center'])
    distances=[math.hypot(p.x-center.x,p.y-center.y) for p in positions]
    self.samples.append(dict(time=now,case=self.case_name,positions=[list(p.to_tuple()) for p in positions]))
    assert max(distances)<self.result['allowed_radius_cm']+150,'A cut fragment relocated elsewhere during settling'
    if age>3.6:
     self.result['max_settled_horizontal_cm']=max(distances);self.cases.append(self.result);self.shot(self.case_name+'_Settled')
     if self.index==1:self.finish();return
     for f in self.parts:f.destroy_actor()
     self.parent.destroy_actor();self.index+=1;self.start_case()
  except Exception:self.finish(traceback.format_exc())
 def finish(self,error=None):
  if self.done:return
  self.done=True;u.unregister_slate_post_tick_callback(self.handle);self.hero.set_primary_input_held(False)
  self.engine.set_editor_property('FixedFrameRate',self.old['fps']);self.engine.set_editor_property('bUseFixedFrameRate',self.old['fixed']);self.perf.set_editor_property('bThrottleCPUWhenNotForeground',self.old['throttle'])
  self.pc.set_view_target_with_blend(self.original_view,0.);self.capture.destroy_actor();self.camera.destroy_actor()
  report=dict(passed=error is None and len(self.cases)==2,error=error,cases=self.cases,samples=self.samples,frames=self.frames,engine_restored=self.old)
  (self.out/'Report.json').write_text(json.dumps(report,indent=2));print('FRAGMENT_ORIGIN_SOLO_FINISHED',report['passed'],error)
builtins.mc_fragment_origin_check=FragmentOriginCheck()
