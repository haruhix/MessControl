"""Bounded solo check: full jump/combo tails, scale invariance and floor pivot.

Uses the existing native Knight fixture and real tool input. Controls are
observer placement/HP, ability readiness, zero chase speed for turn observation,
a one-second roll for entry/exit observation, and temporarily routing BallEnd
(which contains non-unit scale keys) through the ordinary damage slot. This
stress clip is PIE-only; no animation, skeleton or original profile is edited.
No attack phase, hit result or evaluated pose is fabricated.
"""
from pathlib import Path
playback_scope=__doc__
code=Path(__file__).with_name('check_nut_knight_ball_solo.py').read_text(encoding='utf-8')
exec(compile(code.rsplit('builtins.mc_nut_knight_check=KnightCheck()',1)[0],str(Path(__file__).with_name('check_nut_knight_ball_solo.py')),'exec'))
__doc__=playback_scope

class PlaybackCheck(KnightCheck):
 def __init__(self):
  super().__init__()
  self.out=Path(u.Paths.project_dir())/'Artifacts/NutKnightPlayback_20261016';self.frames=self.out/'GameplayFrames';self.frames.mkdir(exist_ok=True)
  self.original_hit=self.boss.get_editor_property('BossSettings').get_editor_property('TankHitAnimation')
  self.initial_scale=self.boss.get_editor_property('TankModel').get_editor_property('relative_scale3d')
  self.ref=u.AnimPoseExtensions.get_reference_pose(self.boss.get_editor_property('TankModel').get_skeletal_mesh_asset().get_editor_property('skeleton'))
  self.bones=u.AnimPoseExtensions.get_bone_names(self.ref)
  self.reference_scales={str(n):u.AnimPoseExtensions.get_bone_pose(self.ref,n,u.AnimPoseSpaces.WORLD).scale3d for n in self.bones}
  self.source_poses={};opt=u.AnimPoseEvaluationOptions();opt.set_editor_property('optional_skeletal_mesh',self.boss.get_editor_property('TankModel').get_skeletal_mesh_asset());opt.set_editor_property('extract_root_motion',False)
  for case,field in [('attack3','TankComboAnimation'),('jump','TankJumpAnimation')]:
   clip=self.boss.get_editor_property('BossSettings').get_editor_property(field);pose=u.AnimPoseExtensions.get_anim_pose_at_time(clip,clip.get_play_length(),opt)
   self.source_poses[case]={n:u.AnimPoseExtensions.get_bone_pose(pose,n,u.AnimPoseSpaces.WORLD).translation for n in ['root_x','hand_l','hand_r','foot_l','foot_r']}
  self.begin('idle')
 def begin(self,case):
  super().begin(case)
  self.boss.set_actor_tick_enabled(case not in ['damage','scaled_damage'])
  if case in ['damage','scaled_damage','death']:
   self.boss.set_actor_rotation(u.Rotator(),False)
   hs=self.hero.status.get_editor_property('State');hs.set_editor_property('Health',100.);self.hero.status.set_editor_property('State',hs)
  if case=='scaled_damage':
   bs=self.boss.get_editor_property('BossSettings');bs.set_editor_property('TankHitAnimation',bs.get_editor_property('TankBallEndAnimation'));self.boss.set_editor_property('BossSettings',bs)
  if case=='rotate':
   ns=self.boss.get_editor_property('Settings');ns.set_editor_property('MoveSpeed',0.);self.boss.set_editor_property('Settings',ns)
  if case=='roll':
   bs=self.boss.get_editor_property('BossSettings');bs.set_editor_property('TankHitAnimation',self.original_hit);bs.set_editor_property('RollDuration',1.);bs.set_editor_property('RollDurationMax',1.);self.boss.set_editor_property('BossSettings',bs)
   self.ready('NextRollAt');self.roll_states=set();self.roll_scales=set();self.roll_hero_floor=self.floor(self.anchor)
 def record(self,now):
  if not u.SystemLibrary.is_valid(self.boss):return
  model=self.boss.get_editor_property('TankModel');state=self.boss.get_editor_property('State');attack=self.boss.get_editor_property('Attack');pivot=self.boss.get_editor_property('TankTurnPivot').get_world_location()
  allowed=attack==u.MCNutBossAttack.ROLL and state in [u.MCNutBossState.TELEGRAPH,u.MCNutBossState.EXECUTING,u.MCNutBossState.RECOVERY]
  scale_error=max((model.get_socket_transform(n,u.RelativeTransformSpace.RTS_COMPONENT).scale3d-self.reference_scales[str(n)]).length() for n in self.bones)
  root=model.get_socket_location('root_x');component_scale=model.get_editor_property('relative_scale3d')
  row={'case':self.case,'time':now-self.at,'game_time':now,'state':str(state),'state_age':now-self.boss.get_editor_property('StateStartedAt'),'state_seconds':self.boss.get_editor_property('AttackEndAt')-self.boss.get_editor_property('StateStartedAt'),'health':self.boss.get_editor_property('Health'),'scale_exception':allowed,'bone_scale_error':scale_error,'component_scale':list(component_scale.to_tuple()),'component_scale_error':(component_scale-self.initial_scale).length(),'pivot':list(pivot.to_tuple()),'body':list(root.to_tuple()),'body_radius_from_pivot':math.hypot(root.x-pivot.x,root.y-pivot.y),'yaw':self.boss.get_actor_rotation().yaw}
  if self.case in self.source_poses:
   row['end_pose_error']=max(model.get_socket_transform(n,u.RelativeTransformSpace.RTS_COMPONENT).translation.distance(v) for n,v in self.source_poses[self.case].items())
  self.rows.append(row)
  if now-self.lastcap>=1/15-.001:
   self.lastcap=now;aim=self.boss.get_actor_location()+u.Vector(0,0,20);eye=aim+u.Vector(1100,-1700,800)
   if self.case in ['rotate','damage','scaled_damage']:aim=self.anchor+u.Vector(0,0,20);eye=aim+u.Vector(1100,-1700,800)
   self.capture.set_actor_location_and_rotation(eye,u.MathLibrary.find_look_at_rotation(eye,aim),False,True);self.cc.capture_scene()
   name=f'{self.case}_{len(self.captures):04}.png';u.RenderingLibrary.export_render_target(self.world,self.target,str(self.frames),name)
   self.captures.append({'case':self.case,'time':now-self.at,'game_time':now,'file':str(self.frames/name)})
 def finish(self,error=None):
  if not error:
   rows=[r for r in self.rows if not r['scale_exception']]
   try:
    self.check('reference bone scale is preserved through all non-ball clips and crossfades',max(r['bone_scale_error'] for r in rows)<.0002,max_error=max(r['bone_scale_error'] for r in rows))
    self.check('component size stays fixed including damage and final death frames',max(r['component_scale_error'] for r in rows)<.0001,max_error=max(r['component_scale_error'] for r in rows))
   except Exception:error=traceback.format_exc()
  super().finish(error)
 def tick(self,dt):
  if self.done or self.busy:return
  self.busy=True
  try:
   assert time.monotonic()-self.started<180,'Playback observation timed out'
   if u.GameplayStatics.is_game_paused(self.world):u.GameplayStatics.set_game_paused(self.world,False)
   now=self.now()
   if now<=self.last:return
   self.last=now;age=now-self.at;valid=u.SystemLibrary.is_valid(self.boss)
   if valid:
    if self.case=='jump':self.place(distance=600)
    elif self.case=='roll':
     self.hero.set_actor_location(self.roll_hero_floor+u.Vector(0,0,self.hero.capsule_component.get_scaled_capsule_half_height()+3),False,True)
     hs=self.hero.status.get_editor_property('State');hs.set_editor_property('Health',100.);self.hero.status.set_editor_property('State',hs)
    else:self.place(-1 if self.case in ['damage','scaled_damage'] or self.case=='rotate' and age>.6 else 1,distance=250 if self.case=='rotate' else 280)
   self.record(now)
   if self.case=='idle' and age>.6:self.begin('attack1')
   elif self.case.startswith('attack') or self.case=='jump':
    state=self.boss.get_editor_property('State')
    if state==u.MCNutBossState.EXECUTING:self.seen=True
    if self.seen and state==u.MCNutBossState.IDLE:
     if self.case in self.source_poses:
      rows=[r for r in self.rows if r['case']==self.case and r['state']==str(u.MCNutBossState.RECOVERY)]
      final=[r for r in rows if r['state_seconds']-r['state_age']<.22]
      self.check(self.case+' reaches and holds the complete final source pose',len(final)>3 and min(r['end_pose_error'] for r in final)<.35,minimum_end_pose_error=min(r['end_pose_error'] for r in final),recovery_seconds=rows[-1]['state_seconds'])
     if self.case=='attack3':self.check('three native combo contacts retained',abs(self.hp0-self.hp()-36.)<.01)
     self.begin('attack2' if self.case=='attack1' else 'attack3' if self.case=='attack2' else 'jump' if self.case=='attack3' else 'rotate')
    assert age<9.,'Native action did not complete'
   elif self.case=='rotate' and age>2.:
    rows=[r for r in self.rows if r['case']=='rotate'];self.check('turn axis remains under the body between the feet',max(r['body_radius_from_pivot'] for r in rows)<20.,max_body_radius_cm=max(r['body_radius_from_pivot'] for r in rows))
    self.check('ground pivot stays fixed during a native 180-degree turn',max(math.dist(r['pivot'][:2],rows[0]['pivot'][:2]) for r in rows)<.01)
    self.begin('damage')
   elif self.case in ['damage','scaled_damage']:
    if age>.2:self.press('hit')
    changed=self.boss.get_editor_property('Health')<self.boss_hp0
    hit_at=self.boss.get_editor_property('VisualHitAt')
    if changed and now-hit_at>(3. if self.case=='scaled_damage' else 1.2):
     self.check(self.case+' delivered through actual Pickaxe input',abs(self.boss_hp0-self.boss.get_editor_property('Health')-40.)<.01)
     self.begin('scaled_damage' if self.case=='damage' else 'roll')
    assert age<9.,'Tool input failed'
   elif self.case=='roll':
    state=self.boss.get_editor_property('State');self.roll_states.add(str(state))
    if self.rows[-1]['bone_scale_error']>.03:self.roll_scales.add(str(state))
    if state==u.MCNutBossState.EXECUTING:self.seen=True
    if self.seen and state==u.MCNutBossState.IDLE:
     self.check('ball entry and exit keep their authored scale exceptions',all(str(s) in self.roll_scales for s in [u.MCNutBossState.TELEGRAPH,u.MCNutBossState.RECOVERY]))
     self.anchor=self.boss.get_actor_location();self.begin('death')
    assert age<8.,'Short fixture roll did not finish'
   elif self.case=='death':
    if age>.15 and valid and not self.boss.get_editor_property('bDefeated'):self.press('death')
    if valid and self.boss.get_editor_property('bDefeated') and self.death_at is None:self.death_at=now
    if not valid:
     self.check('full death also retains its fixed size until actor removal',self.death_at is not None and now-self.death_at>5.4);self.finish()
    assert age<8.,'Death did not finish'
  except Exception:self.finish(traceback.format_exc())
  finally:self.busy=False

builtins.mc_nut_knight_check=PlaybackCheck()
