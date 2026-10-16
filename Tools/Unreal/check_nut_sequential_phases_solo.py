"""One solo check of the real F3 rain, knight defeat, head split and wizard victory.

The two boss health pools are reduced to 40 only inside PIE to bound the check;
the knight shield is set to zero for the phase trigger. Real Pickaxe input is
used for both lethal contacts and an attempted hit during the invulnerable split.
The event, transformation clock, reveal, wizard's first fireball and victory gate
all run natively. Observer HP is restored; this is not a balance playthrough.
"""
from pathlib import Path
scope=__doc__
base=Path(__file__).with_name('check_nut_knight_ball_solo.py')
code=base.read_text(encoding='utf-8').rsplit('builtins.mc_nut_knight_check=KnightCheck()',1)[0]
code=code.replace('Artifacts/NutKnightBall_20261016','Artifacts/NutSequentialPhases_20261017')
exec(compile(code,str(base),'exec'))
__doc__=scope

class SequentialPhaseCheck(KnightCheck):
 def __init__(self):
  super().__init__()
  self.boss.destroy_actor();self.boss=None;self.event=None;self.boss_path=None
  self.case='rain';self.at=self.now();self.split_at=None;self.checked_split=False;self.saw_spell=False;self.cue_seen=False
  self.pc.request_dev_action(u.MCDevAction.NUT_ENCOUNTER,-1)
  self.untouched_mage_health=None
 def stage(self,name):
  self.case=name;self.at=self.now();self.fired=set();self.hero.set_primary_input_held(False)
  print('NUT_SEQUENTIAL_CASE',name)
 def record(self,now):
  if self.done:return
  bosses=list(u.GameplayStatics.get_all_actors_of_class(self.world,u.MCNutBoss))
  if self.boss and u.SystemLibrary.is_valid(self.boss):
   model=self.boss.get_editor_property('TankModel');mage=self.boss.get_editor_property('MageModel')
   row={'case':self.case,'time':now-self.at,'game_time':now,'actor':self.boss.get_path_name(),
        'role':str(self.boss.get_editor_property('BossRole')),'state':str(self.boss.get_editor_property('State')),
        'attack':str(self.boss.get_editor_property('Attack')),'health':self.boss.get_editor_property('Health'),
        'defeated':self.boss.get_editor_property('bDefeated'),'boss_count':len(bosses),
        'selected_tool':str(self.hero.inventory.get_editor_property('Selected')),'hero':self.hero.get_path_name(),
        'bosses_left':self.event.get_editor_property('BossesLeft'),'event_stage':str(self.event.get_editor_property('Stage')),
        'knight_visible':model.is_visible(),'mage_visible':mage.is_visible(),'mage_scale':list(mage.get_editor_property('relative_scale3d').to_tuple())}
   self.rows.append(row)
  if self.case=='rain' or now-self.lastcap<1/15-.001:return
  self.lastcap=now
  aim=self.boss.get_actor_location()+u.Vector(0,0,70);eye=aim+u.Vector(1000,-1450,650)
  if self.case=='entry':aim=self.anchor+u.Vector(0,0,250);eye=aim+u.Vector(1500,-2100,1100)
  self.capture.set_actor_location_and_rotation(eye,u.MathLibrary.find_look_at_rotation(eye,aim),False,True);self.cc.capture_scene()
  name=f'{self.case}_{len(self.captures):04}.png';u.RenderingLibrary.export_render_target(self.world,self.target,str(self.frames),name)
  self.captures.append({'case':self.case,'time':now-self.at,'game_time':now,'file':str(self.frames/name)})
 def tick(self,dt):
  if self.done or self.busy:return
  self.busy=True
  try:
   assert time.monotonic()-self.started<180,'Sequential phase check exceeded wall-time budget'
   now=self.now()
   if now<=self.last:return
   self.last=now
   self.hero=self.pc.get_controlled_pawn()
   if not self.hero:return
   self.tongue=u.GameplayStatics.get_all_actors_of_class(self.world,u.MCTongue)[0]
   if u.GameplayStatics.is_game_paused(self.world):
    self.pc.call_method('ServerSetPerkChoicePause',args=(False,));u.GameplayStatics.set_game_paused(self.world,False)
   hs=self.hero.status.get_editor_property('State');hs.set_editor_property('Health',100.);self.hero.status.set_editor_property('State',hs)
   events=list(u.GameplayStatics.get_all_actors_of_class(self.world,u.MCNutRainEvent))
   if self.event is None:
    owned=[e for e in events if e.actor_has_tag('MC_DevKeyEvent')];assert len(owned)==1,owned;self.event=owned[0]
   assert not self.event.get_editor_property('bFailed'),'F3 event failed'
   bosses=list(u.GameplayStatics.get_all_actors_of_class(self.world,u.MCNutBoss))
   if self.case=='rain':
    assert not any(b.get_editor_property('BossRole')==u.MCNutBossRole.MAGE for b in bosses),'Wizard appeared before knight defeat'
    if bosses:
     self.check('native opening spawns only the knight',len(bosses)==1 and bosses[0].get_editor_property('BossRole')==u.MCNutBossRole.TANK)
     self.boss=bosses[0];self.boss_path=self.boss.get_path_name();self.anchor=self.boss.get_editor_property('LockedTarget')
     self.check('opening knight owns the wizard phase',self.boss.get_editor_property('bHasMagePhase'))
     self.stage('entry')
    assert now-self.at<32,'Opening rain did not reach the knight'
   else:
    assert len(bosses)==1 and self.boss.get_path_name()==self.boss_path,'Phase change replaced or duplicated the boss actor'
    state=self.boss.get_editor_property('State');role=self.boss.get_editor_property('BossRole');age=now-self.at
    if self.case=='entry':
     if state==u.MCNutBossState.IDLE:
      self.ready();self.boss.set_editor_property('Health',40.);self.boss.set_editor_property('ShieldHealth',0.)
      self.stage('knight')
    elif self.case=='knight':
     self.place(distance=280)
     self.hero.inventory.server_select(u.MCToolSlot.PICKAXE)
     if age>.65 and self.hero.inventory.get_editor_property('Selected')==u.MCToolSlot.PICKAXE:
      if 'defeat_knight' not in self.fired:self.inputs.append({'case':self.case,'time':age});self.fired.add('defeat_knight')
      self.hero.set_primary_input_held(True)
     if state==u.MCNutBossState.TRANSFORMING:
      self.split_at=self.boss.get_editor_property('StateStartedAt')
      self.check('real knight lethal hit starts transformation without final death',not self.boss.get_editor_property('bDefeated'))
      self.stage('split')
     assert age<6,'Real input did not defeat the knight'
    elif self.case=='split':
     self.place(distance=280)
     self.check_split_alive()
     if age>.2:self.press('attempt_during_split')
     for effect in u.GameplayStatics.get_all_actors_of_class(self.world,u.MCNutCombatEffect):
      if effect.get_editor_property('Cue').get_editor_property('Type')==u.MCNutCombatCue.KNIGHT_HEAD_SPLIT and effect.get_editor_property('Debris').get_instance_count()==2:self.cue_seen=True
     if role==u.MCNutBossRole.MAGE and self.untouched_mage_health is None:
      self.untouched_mage_health=self.boss.get_editor_property('Health')
      expected=self.boss.get_editor_property('BossSettings').get_editor_property('MageHealth')
      self.check('wizard receives its own full health pool',abs(self.untouched_mage_health-expected)<.01,health=self.untouched_mage_health)
     if self.untouched_mage_health is not None:
      assert abs(self.boss.get_editor_property('Health')-self.untouched_mage_health)<.01,'Split-window hit damaged the fresh wizard'
     if state==u.MCNutBossState.IDLE and role==u.MCNutBossRole.MAGE:
      self.check('both visible shell halves accompany the split',self.cue_seen)
      self.check('same actor finishes the complete transformation',now-self.split_at>=2.39)
      self.check('wizard is visible after transformation',self.boss.get_editor_property('MageModel').is_visible())
      self.check('attempted real tool hit cannot skip the second phase','attempt_during_split' in self.fired and abs(self.boss.get_editor_property('Health')-self.untouched_mage_health)<.01)
      self.stage('wizard')
     assert age<4,'Wizard phase did not finish emerging'
    elif self.case=='wizard':
     self.place(distance=650)
     if list(u.GameplayStatics.get_all_actors_of_class(self.world,u.MCNutSpellProjectile)):
      self.check('transformed wizard casts its native fireball',True)
      self.boss.set_editor_property('Health',40.);self.stage('defeat_wizard')
     assert age<8,'Wizard never resumed its spell schedule'
    elif self.case=='defeat_wizard':
     self.place(distance=280)
     self.hero.inventory.server_select(u.MCToolSlot.PICKAXE)
     if age>.35 and self.hero.inventory.get_editor_property('Selected')==u.MCToolSlot.PICKAXE:
      if 'defeat_wizard' not in self.fired:self.inputs.append({'case':self.case,'time':age});self.fired.add('defeat_wizard')
      self.hero.set_primary_input_held(True)
     if self.boss.get_editor_property('bDefeated'):
      self.check('wizard defeat is final and never transforms again',role==u.MCNutBossRole.MAGE)
      self.stage('victory')
     assert age<6,'Real input did not defeat the wizard'
    elif self.case=='victory':
     if age>1.:
      self.check('victory requires defeat of the wizard form',self.event.get_editor_property('Stage')==u.MCNutRainStage.COMPLETE and self.event.get_editor_property('BossesLeft')==0)
      self.finish()
   self.record(now)
  except Exception:self.finish(traceback.format_exc())
  finally:self.busy=False
 def check_split_alive(self):
  assert self.event.get_editor_property('Stage')==u.MCNutRainStage.ENEMIES and self.event.get_editor_property('BossesLeft')==1,'Encounter completed between forms'
 def finish(self,error=None):
  if self.done:return
  if self.event and u.SystemLibrary.is_valid(self.event):self.pc.request_dev_action(u.MCDevAction.STOP_NUT_ENCOUNTER,-1)
  super().finish(error)

builtins.mc_nut_sequential_check=SequentialPhaseCheck()
