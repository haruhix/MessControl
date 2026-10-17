"""Bounded solo proof of artist clips, retained walk and sustained-damage blink.

Uses the saved wizard profile and native AI scheduler. Fixture controls place
two real ground nuts and the observer, select one ready ability at a time,
restore observer HP and reduce wizard HP only before the final real tool hit.
No animation phase, teleport or summon result is fabricated.
"""
from pathlib import Path
scope=__doc__;base=Path(__file__).with_name('check_mage_summons_solo.py')
code=base.read_text().rsplit('builtins.mc_mage_summon_check=SummonCheck()',1)[0]
code=code.replace('Artifacts/MageSummons_20261016','Artifacts/NutWizardUpdate_20261017')
code=code.replace('self.event=self.spawn(u.MCNutRainEvent,self.center);','self.event=next(a for a in u.GameplayStatics.get_all_actors_of_class(self.w,u.MCNutRainEvent) if a.actor_has_tag("MC_DevKeyEvent"));self.spawned.append(self.event);')
exec(compile(code,str(base),'exec'));__doc__=scope

class WizardCheck(SummonCheck):
 def __init__(self):
  # Initialize the actual event through its normal F3 entry point. Start fills
  # the private tongue and baked food row required by the native rain spell.
  world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_game_world()
  u.GameplayStatics.get_player_controller(world,0).request_dev_action(u.MCDevAction.NUT_ENCOUNTER,-1)
  super().__init__();self.case='idle';self.at=self.now();self.first=self.now();self.fired=set();self.saw=False;self.yaws=[];self.damage_hits=0;self.last_hp=self.mage.get_editor_property('Health');self.tp_start=None;self.death_at=None
  self.ready();self.place(self.center+u.Vector(800,0,0));self.start_pos=self.mage.get_actor_location();self.last_pose=None
  self.bindings=json.loads((self.out/'Bindings.json').read_text());self.mage.get_editor_property('Label').set_hidden_in_game(True)
 def ready(self,name=None):
  now=self.now()
  for k in ['NextMeleeAt','NextChargeAt','NextJumpAt','NextRollAt','NextFireballAt','NextRainAt','NextTeleportAt','NextSummonAt','NextSpecialAt']:self.mage.set_editor_property(k,now+300)
  if name:self.mage.set_editor_property(name,now);self.mage.set_editor_property('NextSpecialAt',now)
 def stage(self,name):
  self.case=name;self.at=self.now();self.fired=set();self.saw=False;self.h.set_primary_input_held(False)
  print('WIZARD_UPDATE_CASE',name)
  if name=='walk':self.start_pos=self.mage.get_actor_location();self.ready();self.place(self.start_pos+u.Vector(-1350,0,0))
  elif name=='cast':self.ready('NextFireballAt');self.mage.set_editor_property('NextTeleportAt',self.now());self.place(self.mage.get_actor_location()+u.Vector(850,0,0))
  elif name=='summon':self.ready('NextSummonAt')
  elif name=='rain':self.ready('NextRainAt')
  elif name=='pressure':
   self.ready('NextTeleportAt');self.last_hp=self.mage.get_editor_property('Health');self.damage_hits=0;self.teleport_observed=False
  elif name=='death':
   self.ready();self.mage.set_editor_property('Health',40.);self.death_at=None
 def tool_place(self):
  p=self.mage.get_actor_location()+u.Vector(275,0,0);self.place(p,180)
  self.h.inventory.server_select(u.MCToolSlot.PICKAXE)
 def press(self,key):
  if key in self.fired:return
  if self.h.get_editor_property('ToothPhysics').get_body_state()!=u.MCBodyState.STANDING:return
  self.fired.add(key);self.h.set_primary_input_held(True);self.h.set_primary_input_held(False)
 def record(self,now):
  if not u.SystemLibrary.is_valid(self.mage):return
  model=self.mage.get_editor_property('MageModel');yaw=self.mage.get_actor_rotation().yaw
  if self.last_pose is not None:
   delta=abs((yaw-self.last_pose+180)%360-180);self.yaws.append(delta)
  self.last_pose=yaw
  self.rows.append(dict(case=self.case,time=now-self.at,game_time=now,state=str(self.mage.get_editor_property('State')),attack=str(self.mage.get_editor_property('Attack')),health=self.mage.get_editor_property('Health'),actor=list(self.mage.get_actor_location().to_tuple()),yaw=yaw,visible=model.is_visible(),scale=list(model.get_editor_property('relative_scale3d').to_tuple()),head=list(model.get_socket_transform('head_x',u.RelativeTransformSpace.RTS_COMPONENT).translation.to_tuple()),right_hand=list(model.get_socket_transform('hand_r',u.RelativeTransformSpace.RTS_COMPONENT).translation.to_tuple()),foot=list(model.get_socket_transform('foot_l',u.RelativeTransformSpace.RTS_COMPONENT).translation.to_tuple())))
  if now-self.lastcap<1/15-.001:return
  self.lastcap=now;aim=self.mage.get_actor_location()+u.Vector(0,0,70);eye=aim+u.Vector(750,-1100,520)
  if self.case=='pressure' and self.tp_start is not None:
   aim=(self.tp_start+self.mage.get_actor_location())*.5+u.Vector(0,0,60);eye=aim+u.Vector(1100,-1650,850)
  self.capture.set_actor_location_and_rotation(eye,u.MathLibrary.find_look_at_rotation(eye,aim),False,True);self.cc.capture_scene()
  name=f'wizard_{len(self.captures):04}.png';u.RenderingLibrary.export_render_target(self.w,self.target,str(self.frames),name)
  self.captures.append(dict(case=self.case,time=now-self.at,game_time=now,file=str(self.frames/name)))
 def tick(self,dt):
  if self.done or self.busy:return
  self.busy=True
  try:
   assert time.monotonic()-self.started<150,'Wizard check wall limit'
   now=self.now()
   if u.GameplayStatics.is_game_paused(self.w):self.resolve_choice()
   if now<=self.last:return
   self.last=now
   hs=self.h.status.get_editor_property('State');hs.set_editor_property('Health',100.);self.h.status.set_editor_property('State',hs);self.h.set_editor_property('bInCoffee',False)
   assert u.SystemLibrary.is_valid(self.mage),'Wizard disappeared before full death playback'
   state=self.mage.get_editor_property('State');attack=self.mage.get_editor_property('Attack');age=now-self.at
   self.record(now)
   if self.case=='idle' and age>1.6:
    self.check('new idle uses the imported artist mesh and native anim instance',self.mage.get_editor_property('MageModel').get_skeletal_mesh_asset().get_path_name().endswith('SK_NutWizard_ArtistUpdate.SK_NutWizard_ArtistUpdate'))
    self.stage('walk')
   elif self.case=='walk' and age>1.5:
    self.check('retained walking moves the wizard normally',self.mage.get_actor_location().distance(self.start_pos)>60)
    self.check('the 180 degree turn takes multiple frames',max(self.yaws)<9.2 and max(self.yaws)>0,max_yaw_step=max(self.yaws))
    self.stage('cast')
   elif self.case in ('cast','summon','rain'):
    expected={'cast':u.MCNutBossAttack.FIREBALL,'summon':u.MCNutBossAttack.SUMMON,'rain':u.MCNutBossAttack.NUT_RAIN}[self.case]
    if attack==expected and state==u.MCNutBossState.TELEGRAPH:self.saw=True
    if self.saw and state==u.MCNutBossState.IDLE:
     self.check(self.case+' completes warning, release and the full authored exit',True,elapsed=age)
     if self.case=='cast':
      for p in u.GameplayStatics.get_all_actors_of_class(self.w,u.MCNutSpellProjectile):p.destroy_actor()
      self.stage('summon')
     elif self.case=='summon':
      creeps=list(u.GameplayStatics.get_all_actors_of_class(self.w,u.MCNutKamikaze))
      self.check('summon animation revives the actual dropped nuts',len(creeps)==2)
      for c in creeps:c.destroy_actor()
      self.stage('rain')
     else:
      self.check('rain animation emits real falling nuts',self.event.get_editor_property('NutsSpawned')>0)
      for food in u.GameplayStatics.get_all_actors_of_class(self.w,u.MCFoodActor):food.destroy_actor()
      self.stage('pressure')
    assert age<12,'Ability did not finish: '+self.case
   elif self.case=='pressure':
    if attack!=u.MCNutBossAttack.TELEPORT:
     self.tool_place()
     if age>.3:self.press('hit1')
     if age>1.:self.press('hit2')
     if age>1.7:self.press('hit3')
     hp=self.mage.get_editor_property('Health')
     if hp<self.last_hp:self.damage_hits+=1;self.last_hp=hp
    if attack==u.MCNutBossAttack.TELEPORT and state==u.MCNutBossState.TELEGRAPH and self.tp_start is None:
     self.check('three real tool hits trigger teleport in the encounter',self.damage_hits>=3,hits=self.damage_hits)
     self.tp_start=self.mage.get_actor_location();self.h.set_primary_input_held(False)
    if self.tp_start is not None and self.mage.get_actor_location().distance(self.tp_start)>400:
     self.teleport_observed=True
    if self.tp_start is not None and state==u.MCNutBossState.IDLE:
     self.check('teleport relocates the wizard and fully completes its clip',self.teleport_observed)
     cues=[a for a in u.GameplayStatics.get_all_actors_of_class(self.w,u.MCNutCombatEffect) if a.get_editor_property('Cue').get_editor_property('Type') in [u.MCNutCombatCue.TELEPORT_TELL,u.MCNutCombatCue.TELEPORT_BURST]]
     self.check('teleport visibly disappears and returns',any(not r['visible'] or min(r['scale'])<.01 for r in self.rows if r['case']=='pressure'))
     self.stage('death')
    assert age<13,('Sustained real hits did not complete teleport',self.damage_hits,str(state))
   elif self.case=='death':
    if not self.mage.get_editor_property('bDefeated'):
     self.tool_place()
     if age>.4:self.press('death')
    elif self.death_at is None:self.death_at=now
    elif now-self.death_at>=3.31:
     self.check('death remains alive through the last source frame before fade/despawn',u.SystemLibrary.is_valid(self.mage))
     self.finish()
    assert age<7,'Actual tool hit did not finish wizard death'
  except Exception:self.finish(traceback.format_exc())
  finally:self.busy=False

builtins.mc_wizard_update_check=WizardCheck()
