"""Short solo check through the actual F3 button and ordinary Pickaxe hits.

Owned fixtures place a 240 HP enemy and the two-phase nut boss. No damage or
defeat is fabricated. The test toggles the real button and releases one attack
per stage; the normal knight-to-wizard transition remains native.
"""
from pathlib import Path
scope=__doc__;base=Path(__file__).with_name('check_mage_summons_solo.py')
code=base.read_text().rsplit('builtins.mc_mage_summon_check=SummonCheck()',1)[0]
code=code.replace('Artifacts/MageSummons_20261016','Artifacts/DevOneHitKill_20261017')
exec(compile(code,str(base),'exec'));__doc__=scope

class OneHitCheck(SummonCheck):
 def __init__(self):
  super().__init__();builtins.mc_one_hit_check=self;self.case='normal';self.at=self.now();self.pressed=False;self.target_actor=None
  self.mode=u.GameplayStatics.get_game_mode(self.w);self.tag=u.Name('MC.DevOneHitKill')
  self.check('one-hit mode starts disabled in a fresh solo session',not self.mode.actor_has_tag(self.tag))
  self.mage.destroy_actor()
  self.knight=self.nut_boss();self.enemy=self.nut_enemy();self.target_actor=self.enemy
  self.pc.toggle_dev_panel();self.button=self.find_button()
  self.check('F3 contains the disabled one-hit toggle',self.button is not None and 'ВЫКЛ' in self.title(self.button))
  self.pc.toggle_dev_panel();self.enemy_before=self.enemy.get_editor_property('Health');self.lastcap=-1
 def title(self,button):return str(button.get_content().get_child_at(0).get_text())
 def find_button(self):
  panels=u.WidgetLibrary.get_all_widgets_of_class(self.w,u.MCDevPanelWidget,False);assert len(panels)==1
  prefix=panels[0].get_path_name()+'.'
  return next((button for button in u.ObjectIterator(u.MCDevActionButton) if button.get_path_name().startswith(prefix) and button.get_parent() and self.title(button).startswith('Убийство с одного удара:')),None)
 def toggle(self,enabled):
  self.pc.toggle_dev_panel();button=self.find_button();assert button
  button.call_method('Clicked')
  self.check('actual F3 button '+('enables' if enabled else 'disables')+' one-hit mode',self.mode.actor_has_tag(self.tag)==enabled)
  self.check('F3 button reflects the new state',('ВКЛ' if enabled else 'ВЫКЛ') in self.title(self.find_button()))
  self.pc.toggle_dev_panel()
 def nut_enemy(self):
  point=self.floor(self.center+u.Vector(-600,0,0));assert point is not None
  pose=u.Transform(location=point+u.Vector(0,0,75));actor=self.defer(u.MCNutEnemy,pose)
  settings=u.MCNutEnemySettings();settings.set_editor_properties(dict(MaxHealth=240.,BodyRadius=70.,MoveSpeed=40.))
  mesh=u.load_asset('/Game/Gameplay/CoreLoop/SM_Walnut_Whole');scale=140/(2*mesh.get_bounds().box_extent.z)
  actor.set_editor_properties(dict(Tongue=self.tongue,Settings=settings,Health=240.,NutMesh=mesh,MeshScale=u.Vector(scale,scale,scale)))
  self.finish_spawn(actor,pose);actor.set_actor_tick_enabled(False);return actor
 def nut_boss(self):
  point=self.floor(self.center+u.Vector(650,0,0));assert point is not None
  pose=u.Transform(location=point+u.Vector(0,0,184),rotation=u.Rotator(yaw=0))
  actor=self.defer(u.MCNutBoss,pose);boss=u.load_asset('/Game/Gameplay/CoreLoop/DA_NutRain').get_editor_property('settings').get_editor_property('boss')
  settings=u.MCNutEnemySettings();settings.set_editor_properties(dict(MaxHealth=1400.,BodyRadius=180.,MoveSpeed=40.))
  actor.set_editor_properties(dict(Tongue=self.tongue,EncounterOwner=self.event,BossRole=u.MCNutBossRole.TANK,BossSettings=boss,Settings=settings,Health=1400.,ShieldHealth=500.,State=u.MCNutBossState.IDLE,Target=self.h,bHasMagePhase=True))
  self.finish_spawn(actor,pose);self.ready(actor);return actor
 def ready(self,actor):
  for key in ['NextMeleeAt','NextChargeAt','NextJumpAt','NextRollAt','NextFireballAt','NextRainAt','NextTeleportAt','NextSummonAt','NextSpecialAt']:actor.set_editor_property(key,self.now()+300)
 def stage(self,name,target):
  self.case=name;self.target_actor=target;self.at=self.now();self.pressed=False;print('ONE_HIT_CASE',name)
 def attack_once(self):
  actor=self.target_actor;offset=120 if actor==self.enemy else 275
  self.place(actor.get_actor_location()+u.Vector(offset,0,0),180)
  self.h.inventory.server_select(u.MCToolSlot.PICKAXE)
  if not self.pressed and self.now()-self.at>.4 and self.h.get_editor_property('ToothPhysics').get_body_state()==u.MCBodyState.STANDING:
   self.h.set_primary_input_held(True);self.h.set_primary_input_held(False);self.pressed=True
 def record(self,now):
  actor=self.target_actor
  if not actor or not u.SystemLibrary.is_valid(actor):return
  row=dict(case=self.case,time=now-self.at,game_time=now,health=actor.get_editor_property('Health'),defeated=actor.get_editor_property('bDefeated'),one_hit=self.mode.actor_has_tag(self.tag),selected=str(self.h.inventory.get_editor_property('Selected')),body=str(self.h.get_editor_property('ToothPhysics').get_body_state()))
  if isinstance(actor,u.MCNutBoss):row.update(role=str(actor.get_editor_property('BossRole')),state=str(actor.get_editor_property('State')))
  self.rows.append(row)
  if now-self.lastcap<.1:return
  self.lastcap=now;aim=actor.get_actor_location()+u.Vector(0,0,40);eye=aim+u.Vector(650,-900,420)
  self.capture.set_actor_location_and_rotation(eye,u.MathLibrary.find_look_at_rotation(eye,aim),False,True);self.cc.capture_scene()
  name=f'one_hit_{len(self.captures):04}.png';u.RenderingLibrary.export_render_target(self.w,self.target,str(self.frames),name)
  self.captures.append(dict(case=self.case,time=now-self.at,game_time=now,file=str(self.frames/name)))
 def finish(self,error=None):
  if self.done:return
  if self.mode.actor_has_tag(self.tag):self.pc.request_dev_action(u.MCDevAction.ONE_HIT_KILL,-1)
  self.done=True;u.unregister_slate_post_tick_callback(self.handle);self.h.set_primary_input_held(False)
  self.engine.set_editor_properties(dict(bUseFixedFrameRate=self.old['fixed'],FixedFrameRate=self.old['fps']))
  self.perf.set_editor_property('bThrottleCPUWhenNotForeground',self.old['throttle'])
  result=dict(passed=error is None,error=error,checks=self.checks,samples=self.rows,captures=self.captures,restored_engine_settings=self.old,scope=__doc__)
  (self.out/'SoloCheck.json').write_text(json.dumps(result,indent=2))
  for actor in reversed(self.spawned):
   try:
    if u.SystemLibrary.is_valid(actor):actor.destroy_actor()
   except TypeError as exception:
    # Dead enemies may have completed their lifetime and been collected.
    if 'ObjectInstance is null' not in str(exception):raise
  print('DEV_ONE_HIT_SOLO_RESULT',json.dumps(dict(passed=result['passed'],error=error,checks=self.checks,frames=len(self.captures))))
 def tick(self,dt):
  if self.done or self.busy:return
  self.busy=True
  try:
   assert time.monotonic()-self.started<90,'one-hit solo wall limit'
   if u.GameplayStatics.is_game_paused(self.w):self.resolve_choice()
   now=self.now()
   if now<=self.last:return
   self.last=now;age=now-self.at
   hp=self.h.status.get_editor_property('State');hp.set_editor_property('Health',100.);self.h.status.set_editor_property('State',hp)
   if u.SystemLibrary.is_valid(self.knight):self.ready(self.knight)
   self.record(now)
   if self.case=='normal':
    self.attack_once()
    health=self.enemy.get_editor_property('Health')
    if health<self.enemy_before:
     self.ordinary_damage=self.enemy_before-health
     self.check('disabled mode retains nonlethal ordinary Pickaxe damage',health>0 and 0<self.ordinary_damage<self.enemy_before,before=self.enemy_before,after=health)
     self.toggle(True);self.stage('enemy_one_hit',self.enemy)
   elif self.case=='enemy_one_hit':
    if age>.5:self.attack_once()
    if self.enemy.get_editor_property('bDefeated'):
     self.check('one real hit defeats an enemy with more HP than ordinary damage',self.pressed)
     self.stage('knight_one_hit',self.knight)
   elif self.case=='knight_one_hit':
    self.attack_once()
    if self.knight.get_editor_property('State')==u.MCNutBossState.TRANSFORMING:
     self.check('one real hit defeats the shielded knight but preserves its second phase',self.pressed and not self.knight.get_editor_property('bDefeated'))
     self.stage('transformation',self.knight)
   elif self.case=='transformation':
    if self.knight.get_editor_property('BossRole')==u.MCNutBossRole.MAGE and self.knight.get_editor_property('State')==u.MCNutBossState.IDLE:
     self.check('normal transition reveals the living wizard',self.knight.get_editor_property('Health')==1100.)
     self.stage('wizard_one_hit',self.knight)
   elif self.case=='wizard_one_hit':
    self.attack_once()
    if self.knight.get_editor_property('bDefeated'):
     self.check('one real hit defeats the wizard',self.pressed)
     self.toggle(False);self.enemy=self.nut_enemy();self.enemy_before=self.enemy.get_editor_property('Health');self.stage('normal_restored',self.enemy)
   elif self.case=='normal_restored':
    self.attack_once();health=self.enemy.get_editor_property('Health')
    if health<self.enemy_before:
     self.check('turning the mode off restores ordinary damage',abs(self.enemy_before-health-self.ordinary_damage)<.01 and not self.enemy.get_editor_property('bDefeated'),before=self.enemy_before,after=health)
     self.finish()
   assert age<9,('one-hit stage did not complete',self.case,self.pressed)
  except Exception:self.finish(traceback.format_exc())
  finally:self.busy=False

builtins.mc_one_hit_check=OneHitCheck()
