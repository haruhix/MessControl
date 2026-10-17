"""Solo: native blink/cast at 1800 cm/s, then a real downward projectile hit.

The second shot is an explicit aim fixture on the saved tongue. Its collision,
ulcer, pain pulse and encounter cleanup all run through native gameplay code.
"""
from pathlib import Path
ulcer_scope=__doc__;base=Path(__file__).with_name('check_fire_nut_hand_solo.py')
code=base.read_text().rsplit('builtins.mc_fire_nut_check=FireNutCheck()',1)[0]
code=code.replace('Artifacts/FireNutHandFix_20261017','Artifacts/FireNutUlcer_20261017').replace('1100','1800').replace('speed is doubled','speed is increased')
exec(compile(code,str(base),'exec'));__doc__=ulcer_scope

class UlcerCheck(FireNutCheck):
 def __init__(self):
  self.ulcer_phase=None;self.ulcer_data={};super().__init__()
 def finish(self,error=None):
  if error is None and self.ulcer_phase is None:
   self.begin_ulcer();return
  super().finish(error)
  p=self.out/'SoloCheck.json';result=json.loads(p.read_text());result['ulcer']=self.ulcer_data;result['scope']=ulcer_scope;p.write_text(json.dumps(result,indent=2))
 def begin_ulcer(self):
  self.ulcer_phase='impact';self.ulcer_at=self.now();self.patch=None;self.pulsed=False
  self.before_ulcers=set(u.GameplayStatics.get_all_actors_of_class(self.w,u.MCMouthSurface))
  self.point=self.floor(self.center+u.Vector(-500,-400,0));assert self.point is not None
  start=self.point+u.Vector(-100,0,500);target=self.point-u.Vector(0,0,80);pose=u.Transform(location=start)
  self.probe=self.defer(u.MCNutSpellProjectile,pose);flight=self.probe.get_editor_property('Flight').copy()
  for k,v in dict(Start=start,Target=target,StartedAt=self.now(),Seconds=start.distance(target)/1800,Radius=65.).items():flight.set_editor_property(k,v)
  for k,v in dict(Flight=flight,Tongue=self.tongue,SourceActor=self.mage).items():self.probe.set_editor_property(k,v)
  self.probe.set_owner(self.event);self.finish_spawn(self.probe,pose)
  self.ulcer_data=dict(aim_fixture=True,target=list(self.point.to_tuple()),speed=1800.)
 def tick(self,dt):
  if self.ulcer_phase is None:return super().tick(dt)
  if self.done or self.busy:return
  self.busy=True
  try:
   assert time.monotonic()-self.started<55,'ulcer wall limit'
   now=self.now()
   if self.ulcer_phase=='impact':
    created=[a for a in u.GameplayStatics.get_all_actors_of_class(self.w,u.MCMouthSurface) if a not in self.before_ulcers and a.actor_has_tag('MC_NutFireUlcer')]
    if created:
     self.check('A real tongue collision creates exactly one ordinary pain ulcer',len(created)==1 and created[0].get_editor_property('bUlcer'))
     self.patch=created[0];self.born=now;self.ulcer_phase='pulse'
     impact=self.probe.get_editor_property('Impact');self.check('The new ulcer is placed at the actual projectile impact',impact.get_editor_property('bImpacted') and self.patch.get_actor_location().distance(impact.get_editor_property('Point'))<12)
     self.ulcer_data.update(position=list(self.patch.get_actor_location().to_tuple()),impact=list(impact.get_editor_property('Point').to_tuple()),pulse_interval=self.patch.get_editor_property('PulseInterval'))
     self.capture_ulcer()
    assert now-self.ulcer_at<2,'no ulcer after actual downward projectile collision'
   elif self.ulcer_phase=='pulse':
    self.pulsed|=any(w.get_editor_property('StartedAt')>=self.born and w.get_actor_location().distance(self.patch.get_actor_location())<25 for w in u.GameplayStatics.get_all_actors_of_class(self.w,u.MCHazardWave))
    if self.pulsed:
     self.check('The wound emits a real pain wave and remains treatable',self.patch.get_editor_property('Healing')==0 and not self.patch.get_editor_property('bTreatmentBlocked') and self.patch.get_editor_property('PulseDamage')>0)
     self.ulcer_data['pain_wave_seen']=True;self.event.destroy_actor();self.ulcer_phase='cleanup';self.cleanup_at=now
    assert now-self.born<max(5,self.patch.get_editor_property('PulseInterval')+2),'no pain pulse from the wound'
   elif self.ulcer_phase=='cleanup' and now-self.cleanup_at>.1:
    try:alive=u.SystemLibrary.is_valid(self.patch)
    except TypeError as ex:
     if 'ObjectInstance is null' not in str(ex):raise
     alive=False
    self.check('Encounter cleanup removes its fire-nut wound',not alive);self.ulcer_data['cleanup_passed']=True;self.finish()
  except Exception:self.finish(traceback.format_exc())
  finally:self.busy=False
 def capture_ulcer(self):
  aim=self.patch.get_actor_location();eye=aim+u.Vector(500,450,550)
  self.capture.set_actor_location_and_rotation(eye,u.MathLibrary.find_look_at_rotation(eye,aim),False,True);self.cc.capture_scene()
  u.RenderingLibrary.export_render_target(self.w,self.target,str(self.frames),'ulcer_after_impact.png')

builtins.mc_fire_nut_ulcer_check=UlcerCheck()
