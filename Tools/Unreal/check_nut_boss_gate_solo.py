"""One natural F3 storm through knight landing and an open boss fight.

Restores observer HP and fixed-rate/background preferences. Does not change
rain, boss, placement, stage, ability clocks or combat outcomes.
"""
import unreal as u,builtins,time,json,traceback
from pathlib import Path

class NutGateCheck:
 def __init__(self):
  self.w=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_game_world();assert self.w
  self.pc=u.GameplayStatics.get_player_controller(self.w,0);self.pc.request_dev_action(u.MCDevAction.NUT_ENCOUNTER,-1)
  self.event=next(a for a in u.GameplayStatics.get_all_actors_of_class(self.w,u.MCNutRainEvent) if a.actor_has_tag('MC_DevKeyEvent'))
  self.h=u.GameplayStatics.get_player_character(self.w,0);self.start=u.GameplayStatics.get_time_seconds(self.w);self.wall=time.monotonic();self.last=-1;self.rows=[];self.checks=[];self.boss_at=None;self.falling=False;self.landed=False;self.done=False
  self.out=Path(u.Paths.project_dir()).resolve()/'Artifacts/NutBossGateFix_20261017'
  self.engine=next(o for o in u.ObjectIterator(u.load_class(None,'/Script/Engine.Engine')) if o.get_path_name().startswith('/Engine/Transient.'))
  self.perf=u.get_default_object(u.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'))
  self.old=dict(fixed=self.engine.get_editor_property('bUseFixedFrameRate'),fps=self.engine.get_editor_property('FixedFrameRate'),throttle=self.perf.get_editor_property('bThrottleCPUWhenNotForeground'))
  self.engine.set_editor_properties(dict(bUseFixedFrameRate=True,FixedFrameRate=60.));self.perf.set_editor_property('bThrottleCPUWhenNotForeground',False)
  self.rain=self.event.get_editor_property('Settings').get_editor_property('RainSeconds')
  self.handle=u.register_slate_post_tick_callback(self.tick)
 def check(self,name,ok):
  self.checks.append(dict(name=name,passed=bool(ok)));assert ok,name
 def finish(self,error=None):
  if self.done:return
  self.done=True;u.unregister_slate_post_tick_callback(self.handle)
  self.engine.set_editor_properties(dict(bUseFixedFrameRate=self.old['fixed'],FixedFrameRate=self.old['fps']));self.perf.set_editor_property('bThrottleCPUWhenNotForeground',self.old['throttle'])
  result=dict(passed=error is None,error=error,checks=self.checks,boss_appeared_at=self.boss_at,rows=self.rows,scope=__doc__,preferences_restored=True)
  (self.out/'SoloCheck.json').write_text(json.dumps(result,indent=2));print('NUT_GATE_SOLO_RESULT',dict(passed=result['passed'],error=error,checks=self.checks,boss_at=self.boss_at))
 def tick(self,dt):
  try:
   assert time.monotonic()-self.wall<75,'wall limit'
   now=u.GameplayStatics.get_time_seconds(self.w)-self.start
   state=self.h.status.get_editor_property('State');state.set_editor_property('Health',100.);self.h.status.set_editor_property('State',state)
   self.h.set_editor_property('bInCoffee',False)
   assert not u.GameplayStatics.is_game_paused(self.w),'unexpected pause'
   if int(now)==self.last:return
   self.last=int(now)
   bosses=list(self.event.get_editor_property('Bosses'))
   row=dict(age=now,stage=str(self.event.get_editor_property('Stage')),failed=self.event.get_editor_property('bFailed'),bosses=len(bosses),nuts=self.event.get_editor_property('NutsSpawned'))
   self.rows.append(row);assert not row['failed'],'cataclysm failed'
   if bosses:
    boss=bosses[0];bs=boss.get_editor_property('State')
    row.update(boss_state=str(bs),health=boss.get_editor_property('Health'),role=str(boss.get_editor_property('BossRole')))
    if self.boss_at is None:
     self.boss_at=now
     self.check('The complete natural storm precedes exactly one knight',now>=self.rain-.1 and len(bosses)==1 and boss.get_editor_property('BossRole')==u.MCNutBossRole.TANK)
     self.check('The knight owns the subsequent wizard phase',boss.get_editor_property('bHasMagePhase'))
    self.falling|=bs==u.MCNutBossState.FALLING;self.landed|=bs!=u.MCNutBossState.FALLING
   if now>self.rain+8:
    self.check('The real F3 storm launches falling nuts',row['nuts']>0)
    self.check('The knight appears and completes its falling entrance',self.boss_at is not None and self.falling and self.landed)
    self.check('The living boss keeps the event open beyond the rain deadline',len(bosses)==1 and self.event.get_editor_property('Stage')==u.MCNutRainStage.ENEMIES and bosses[0].get_editor_property('Health')>0 and not bosses[0].get_editor_property('bDefeated'))
    self.finish()
  except Exception:self.finish(traceback.format_exc())

builtins.mc_nut_gate_check=NutGateCheck()
