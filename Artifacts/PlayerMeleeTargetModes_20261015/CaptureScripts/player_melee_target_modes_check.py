"""Short solo PIE: held food chopping, enemy combos, priority and target transition."""
import unreal as u
import json,math,time,traceback,builtins
from pathlib import Path

class TargetModesCheck:
 def __init__(self):
  self.out=Path('D:/PROJECT/GAME/WaifuTD/MessControl/Artifacts/PlayerMeleeTargetModes_20261015');self.frames=self.out/'CapturedFrames';self.frames.mkdir(exist_ok=True)
  self.w=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_game_world();assert self.w
  self.pc=u.GameplayStatics.get_player_controller(self.w,0);assert isinstance(self.pc,u.MCCombatFeedbackController)
  stations=u.GameplayStatics.get_all_actors_of_class(self.w,u.MCCombatFeedbackStation)
  for s in stations:
   s.set_running(False)
   for c in s.get_components_by_class(u.TextRenderComponent):c.set_hidden_in_game(True)
  self.pc.close_test_panel();self.pc.view_current();self.h=u.GameplayStatics.get_player_pawn(self.w,0)
  self.station=next(s for s in stations if not s.get_editor_property('bLegacyFeedback'));self.mob=self.station.get_editor_property('Enemy')
  hero=self.h.get_actor_location();enemy=self.mob.get_actor_location();delta=enemy-hero;gap=math.hypot(delta.x,delta.y);direction=u.Vector(delta.x/gap,delta.y/gap,0)
  self.origin=enemy-direction*150;self.origin.z=hero.z
  self.h.set_actor_location(self.origin,False,True);self.h.character_movement.disable_movement()
  self.forward=direction;self.right=u.Vector(-direction.y,direction.x,0);self.yaw=math.degrees(math.atan2(direction.y,direction.x))
  self.lib=u.get_default_object(u.GameplayStatics)
  self.food=self.spawn(u.MCFoodActor,u.Transform(location=self.origin+self.forward*3000),True)
  self.food.body.set_simulate_physics(False);self.food.body.set_enable_gravity(False);self.food.set_actor_tick_enabled(False)
  _,extent,_=u.SystemLibrary.get_component_bounds(self.food.visual);scale=35/max(extent.x,extent.y,extent.z);self.food.set_actor_scale3d(u.Vector(scale,scale,scale));self.food.label.set_hidden_in_game(True)
  cls=u.load_class(None,'/Game/Gameplay/Boss/Phase3/BP_BossPhase3.BP_BossPhase3_C');assert cls
  self.boss=self.spawn(cls,u.Transform(location=self.origin+self.forward*4000),False)
  self.boss.character_movement.disable_movement();self.boss.set_actor_tick_enabled(False)
  brain=self.boss.get_controller()
  if brain:brain.set_actor_tick_enabled(False)
  self.engine=next(o for o in u.ObjectIterator(u.load_class(None,'/Script/Engine.Engine')) if o.get_path_name().startswith('/Engine/Transient.'))
  self.perf=u.get_default_object(u.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'))
  self.old={'fixed':self.engine.get_editor_property('bUseFixedFrameRate'),'fps':self.engine.get_editor_property('FixedFrameRate'),'throttle':self.perf.get_editor_property('bThrottleCPUWhenNotForeground')}
  self.engine.set_editor_property('FixedFrameRate',60.);self.engine.set_editor_property('bUseFixedFrameRate',True);self.perf.set_editor_property('bThrottleCPUWhenNotForeground',False)
  self.capture=self.spawn(u.SceneCapture2D,u.Transform(),False);self.component=self.capture.get_editor_property('capture_component2d')
  self.target=u.RenderingLibrary.create_render_target2d(self.w,960,540,u.TextureRenderTargetFormat.RTF_RGBA8,u.LinearColor(.025,.03,.045,1),False,False)
  for n,v in dict(texture_target=self.target,capture_source=u.SceneCaptureSource.SCS_FINAL_COLOR_LDR,capture_every_frame=False,capture_on_movement=False,always_persist_rendering_state=True).items():self.component.set_editor_property(n,v)
  u.SystemLibrary.execute_console_command(self.w,'mc.Combo.Debug 1',self.pc)
  self.cases=['knife_food_hold','pickaxe_food_hold','mob_hold','mob_combo','boss_combo','food_to_enemy_hold'];self.index=0;self.rows=[];self.captures=[];self.checks=[];self.input=[]
  self.started=time.monotonic();self.busy=False;self.finished=False;self.last=-1
  self.begin();self.handle=u.register_slate_post_tick_callback(self.tick);print('TARGET_MODES_CHECK_STARTED')
 def spawn(self,cls,transform,food=False):
  a=self.lib.call_method('BeginDeferredActorSpawnFromClass',args=(self.w,cls,transform,u.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN,None,u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
  if food:
   row=a.get_editor_property('FoodData');row.set_editor_property('scale',u.Vector(1,1,1));row.set_editor_property('health',1000.);row.set_editor_property('resistance',u.MCFoodResistance.SOFT)
   a.set_editor_property('FoodData',row);a.set_editor_property('ItemMesh',u.load_asset('/Game/Stylized_Fruits/Meshes/SM_GrapesPurple'));a.set_editor_property('ItemName','MeleeModeFood');a.set_editor_property('Health',1000.)
  self.lib.call_method('FinishSpawningActor',args=(a,transform,u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT));return a
 def now(self):return u.GameplayStatics.get_time_seconds(self.w)
 def health(self):return {'food':self.food.get_editor_property('Health'),'mob':self.mob.get_editor_property('Health'),'boss':self.boss.get_editor_property('Runtime').health}
 def serial(self,a):return a.get_editor_property('Feedback').get_editor_property('LastHit').get_editor_property('Serial')
 def place_mob(self):self.mob.set_actor_location(self.origin+self.forward*150+u.Vector(0,0,-10),False,True)
 def begin(self):
  self.case=self.cases[self.index];self.at=self.now();self.fired=set();self.last_capture=-1
  self.h.set_primary_input_held(False);self.h.inventory.server_select(u.MCToolSlot.PICKAXE if self.case in ['pickaxe_food_hold','boss_combo'] else u.MCToolSlot.KNIFE)
  self.mob.set_actor_location(self.origin+self.forward*3000,False,True);self.boss.set_actor_location(self.origin+self.forward*4000,False,True)
  self.mob.set_editor_property('Health',1000.);self.mob.set_editor_property('bDefeated',False)
  self.food.set_editor_property('Health',1000.);self.food.set_editor_property('Phase',u.MCFoodPhase.FREE)
  row=self.food.get_editor_property('FoodData');row.set_editor_property('resistance',u.MCFoodResistance.HARD if self.case in ['pickaxe_food_hold','boss_combo'] else u.MCFoodResistance.SOFT);self.food.set_editor_property('FoodData',row)
  mixed=self.case in ['mob_hold','mob_combo','boss_combo'];self.food.set_actor_location(self.origin+self.forward*(75 if mixed else 110)+self.right*(55 if mixed else 0)+u.Vector(0,0,-10),False,True)
  if self.case.startswith('mob_'):self.place_mob()
  if self.case=='boss_combo':
   radius=self.boss.get_editor_property('BodyHitbox').get_scaled_capsule_radius();half=self.boss.get_editor_property('capsule_component').get_scaled_capsule_half_height()
   self.boss.set_actor_location(self.origin+self.forward*(radius+100)+u.Vector(0,0,half-58),False,True)
  self.base=self.health();self.mob_serial=self.serial(self.mob);self.boss_serial=self.serial(self.boss)
  u.log('MC_TARGET_MODE_CASE '+self.case)
 def press(self,key,held=False):
  if key in self.fired:return
  self.fired.add(key);self.h.set_primary_input_held(True)
  if not held:self.h.set_primary_input_held(False)
  self.input.append({'case':self.case,'time':self.now()-self.at,'held':held})
 def capture_frame(self,age):
  if self.case not in ['knife_food_hold','mob_combo','boss_combo'] or age> (2.8 if self.case=='knife_food_hold' else 2.45) or age-self.last_capture<1/30-.001:return
  self.last_capture=age;manager=self.pc.player_camera_manager
  self.capture.set_actor_location_and_rotation(manager.get_camera_location(),manager.get_camera_rotation(),False,True);self.component.set_editor_property('fov_angle',manager.get_fov_angle());self.component.capture_scene()
  name=f'{self.case}_{len(self.captures):04d}.png';u.RenderingLibrary.export_render_target(self.w,self.target,str(self.frames),name)
  self.captures.append({'case':self.case,'time':age,'file':str(self.frames/name)})
 def verify(self):
  current=self.health();case=self.case
  expected={'knife_food_hold':(75,0,0),'pickaxe_food_hold':(120,0,0),'mob_hold':(0,25,0),'mob_combo':(0,75,0),'boss_combo':(0,0,120),'food_to_enemy_hold':(25,0,0)}[case]
  damage=tuple(self.base[k]-current[k] for k in ['food','mob','boss']);assert all(abs(a-b)<.01 for a,b in zip(damage,expected)),(case,damage,expected)
  if case in ['mob_hold','mob_combo']:assert self.serial(self.mob)-self.mob_serial==(1 if case=='mob_hold' else 3)
  if case=='boss_combo':assert self.serial(self.boss)-self.boss_serial==3
  self.checks.append({'case':case,'passed':True,'food_mob_boss_damage':damage,'expected':expected})
 def finish(self,error=None):
  if self.finished:return
  self.finished=True;u.unregister_slate_post_tick_callback(self.handle);self.h.set_primary_input_held(False)
  self.engine.set_editor_property('bUseFixedFrameRate',self.old['fixed']);self.engine.set_editor_property('FixedFrameRate',self.old['fps']);self.perf.set_editor_property('bThrottleCPUWhenNotForeground',self.old['throttle'])
  u.SystemLibrary.execute_console_command(self.w,'mc.Combo.Debug 0',self.pc)
  for a in [self.food,self.boss,self.capture]:a.destroy_actor()
  data={'passed':error is None,'error':error,'checks':self.checks,'input':self.input,'samples':self.rows,'captures':self.captures,'restored_engine_settings':self.old,'scope':'Short solo PIE; real primary press/release edges, real food damage, enemy damage and boss damage. No multiplayer, cook or package.'}
  (self.out/'SoloCheck.json').write_text(json.dumps(data,indent=2));print('TARGET_MODES_CHECK_RESULT',json.dumps({'passed':data['passed'],'error':error,'checks':self.checks,'frames':len(self.captures)}))
 def tick(self,dt):
  if self.busy or self.finished:return
  self.busy=True
  try:
   assert time.monotonic()-self.started<150,'Target-mode check timed out'
   now=self.now()
   if now<=self.last:return
   self.last=now;age=now-self.at;self.h.set_actor_rotation(u.Rotator(pitch=0,yaw=self.yaw,roll=0),False)
   hand=self.h.mesh.get_socket_location('hand_r')-self.h.get_actor_location();self.rows.append({'case':self.case,'time':age,'health':self.health(),'hand_local':[hand.dot(self.forward),hand.dot(self.right),hand.z]});self.capture_frame(age)
   if self.case in ['mob_combo','boss_combo']:
    for i,t in enumerate([.2,.3,.4]):
     if age>=t:self.press(i)
    end=2.7
   else:
    if age>=.2:self.press('hold',True)
    release=2.35 if self.case.endswith('food_hold') else 1.5 if self.case=='mob_hold' else 1.8
    if age>=release:self.h.set_primary_input_held(False)
    if self.case=='food_to_enemy_hold' and age>=.9 and 'transition' not in self.fired:
     self.fired.add('transition');self.food.set_actor_location(self.origin+self.forward*3000,False,True);self.place_mob()
    end=3.2 if self.case.endswith('food_hold') else 1.8 if self.case=='mob_hold' else 2.1
   if age>=end:
    self.verify();self.h.set_primary_input_held(False);self.index+=1
    if self.index==len(self.cases):self.finish()
    else:self.begin()
  except Exception:self.finish(traceback.format_exc())
  finally:self.busy=False

_target_modes_check=TargetModesCheck();builtins.mc_target_modes_check=_target_modes_check
