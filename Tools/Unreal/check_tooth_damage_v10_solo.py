"""One bounded solo check: all 12 crown stages, normal held spray, loss physics.

Uses the existing transient arena fixture; damage enters ReceiveArenaHit. Repair
uses selected Spray, ordinary camera orbit and held primary input, never HP edits.
"""
import unreal as u,json,time,traceback,builtins,runpy
from pathlib import Path
root=Path(u.Paths.project_dir()).resolve();out=Path(globals().get('TOOTH_DAMAGE_OUTPUT',root/'Artifacts/ToothDamageV10_20261017'))
Base=runpy.run_path(str(root/'Tools/Unreal/check_arena_v10_solo.py'),init_globals=dict(ARENA_CHECK_INSTANTIATE=False,ARENA_CHECK_OUTPUT=str(out)))['ArenaCheck']

class DamageCheck(Base):
 def __init__(self):
  self.phase=0;self.stage_rows=[];self.repaired=False;self.busy=False
  super().__init__();self.out=out
  self.originals={t:t.visual.static_mesh for t in self.teeth}
  self.eye=u.Vector(-1100,500,480);self.look=u.Vector(-750,1410,120)
 def health(self):return self.subject.get_editor_property('State').get_editor_property('Health')
 def closeup(self,name):
  p=self.subject.get_actor_location()
  self.shot(name,p+u.Vector(-100,-660,160),p+u.Vector(0,0,70))
 def stages(self,stage):
  mat=u.load_asset('/Game/Art/Materials/M_ArenaTooth')
  expected={0:u.MCArenaToothDamageStage.INTACT,1:u.MCArenaToothDamageStage.CHIPPED,2:u.MCArenaToothDamageStage.BROKEN}[stage]
  for t in self.teeth:
   mesh=t.visual.static_mesh
   assert t.damage_stage()==expected,(t.get_name(),str(t.damage_stage()))
   assert (mesh==self.originals[t]) if stage==0 else mesh.get_name().endswith('_Chipped' if stage==1 else '_Broken')
   assert t.get_editor_property('BrushSurface').static_mesh==mesh
   slots=mesh.get_editor_property('static_materials')
   index=next((i for i,s in enumerate(slots) if str(s.material_slot_name)=='MC_Damage_Enamel'),0)
   assert t.visual.get_material(index).get_editor_property('parent')==mat
   self.stage_rows.append(dict(id=t.get_editor_property('State').get_editor_property('ToothId'),health=t.get_editor_property('State').get_editor_property('Health'),stage=str(expected),mesh=mesh.get_path_name()))
  self.check('all twelve teeth show '+str(expected)+' with matching brush geometry and M_ArenaTooth',True)
 def aim(self):
  self.place(0)
  p=self.subject.get_actor_location();ext=self.subject.body.get_scaled_box_extent()
  aim=u.Vector(p.x,p.y,max(p.z+ext.z*.2,self.hero.get_actor_location().z+45))
  view=self.hero.camera.get_world_rotation();rot=u.MathLibrary.find_look_at_rotation(self.hero.camera.get_world_location(),aim)
  yaw=(rot.yaw-view.yaw+180)%360-180
  self.hero.apply_camera_orbit_input(u.Vector2D(yaw*.3/self.hero.camera_orbit_sensitivity,(rot.pitch-view.pitch)*.3/self.hero.camera_orbit_sensitivity))
 def tick(self,dt):
  if self.done or self.busy:return
  self.busy=True
  try:
   assert time.monotonic()-self.started<80,'Wall limit'
   now=u.GameplayStatics.get_time_seconds(self.world)-self.t0
   if self.phase==0 and now>.7:
    self.stages(0);self.shot('01_Intact',self.eye,self.look);self.closeup('01_Intact_Close')
    for t in self.teeth:assert t.receive_arena_hit(100/3,u.Vector(0,1,0))
    self.phase=1;self.at=now
   elif self.phase==1 and now-self.at>1.1:
    self.stages(1);self.shot('02_Chipped',self.eye,self.look);self.closeup('02_Chipped_Close')
    for t in self.teeth:assert t.receive_arena_hit(100/3,u.Vector(0,1,0))
    self.phase=2;self.at=now
   elif self.phase==2 and now-self.at>1.1:
    self.stages(2);self.shot('03_Broken',self.eye,self.look);self.closeup('03_Broken_Close')
    self.hero.inventory.server_select(u.MCToolSlot.SPRAY);self.hero.set_primary_input_held(False)
    self.aim();self.phase=3;self.at=now;self.hp0=self.health()
   elif self.phase==3:
    self.aim()
    if now-self.at>1.4:
     self.hero.set_primary_input_held(True);self.phase=4;self.at=now
   elif self.phase==4:
    self.aim();self.hero.set_primary_input_held(True)
    if self.health()>40 and not self.repaired:
     assert self.subject.visual.static_mesh.get_name().endswith('_Chipped')
     self.check('ordinary held Spray repairs broken crown back to chipped',True);self.repaired=True
    if self.health()>=100:
     self.hero.set_primary_input_held(False);self.phase=5;self.at=now
    assert now-self.at<10,('Held Spray did not fully heal',self.health(),str(self.hero.inventory.spray_aim()),str(self.hero.get_actor_location()),self.hero.can_work())
   elif self.phase==5 and now-self.at>.4:
    assert self.subject.visual.static_mesh==self.originals[self.subject]
    self.check('ordinary held Spray restores the original V10 tooth at full health',True)
    self.shot('04_Repaired',self.eye,self.look);self.closeup('04_Repaired_Close')
    self.lost=self.teeth[0];self.lost_start=self.lost.get_actor_location()
    assert self.lost.receive_arena_hit(100,u.Vector(0,1,0))
    self.phase=6;self.at=now
   elif self.phase==6 and now-self.at>.7:
    assert self.lost.damage_stage()==u.MCArenaToothDamageStage.LOST and not self.lost.is_available()
    assert self.lost.body.is_simulating_physics()
    self.check('zero health still knocks the tooth out with physics',True);self.finish()
  except Exception:self.finish(traceback.format_exc())
  finally:self.busy=False
 def finish(self,error=None):
  if self.done:return
  self.done=True;u.unregister_slate_post_tick_callback(self.handle)
  self.hero.set_primary_input_held(False);self.capture.destroy_actor()
  self.engine.set_editor_property('FixedFrameRate',self.old['fps']);self.engine.set_editor_property('bUseFixedFrameRate',self.old['fixed']);self.perf.set_editor_property('bThrottleCPUWhenNotForeground',self.old['throttle'])
  report=dict(passed=error is None,error=error,phase=self.phase,checks=self.checks,stages=self.stage_rows,captures=self.captures,restored_engine_settings=self.old,scope='one standalone solo PIE; native damage on all 12 teeth; held Spray repairs one; one knockout')
  (self.out/'SoloCheck.json').write_text(json.dumps(report,indent=2))
  print('TOOTH_DAMAGE_V10_SOLO_RESULT',report['passed'],error)

builtins.mc_tooth_damage_v10_check=DamageCheck()
