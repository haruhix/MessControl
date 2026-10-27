"""One solo PIE presentation/use probe; no Computer Use or OS input.
The existing editor review fixture stops the live director and places the player.
Skills are granted through the canonical authority API. The ownerless chest then
uses normal falling/approach/offer code. This is not an earned-round playthrough.
"""
import builtins,hashlib,json,runpy,time,traceback
from pathlib import Path
import unreal as u
ROOT=Path(u.Paths.project_dir()).resolve()
OUT=Path('E:/CodexWork/MessControl/RoguelikeFoundation_20261027/Solo9')
assert not (OUT/'Report.json').exists(),'Keep previous evidence; use a fresh output folder'
assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages()
assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages()
support=runpy.run_path(str(ROOT/'Tools/Unreal/check_approved_environment_solo.py'),
 run_name='foundation_support',init_globals={'MC_ENV_OUTPUT':OUT,'MC_ENV_CAPTURE_HZ':0})
Base,prop=support['ApprovedEnvironmentSolo'],support['prop']
class FoundationSolo(Base):
 def __init__(self):
  super().__init__();self.report.update(scope='Canonical skills HUD, real use and personal chest presentation in one solo PIE',
   limitations=['Explicit editor review fixture stops the director; diagnostic grants are not an earned run.',
    'No multiplayer, microphone, packaged game or full new screenshot perk effects checked.'],checks={})
  self.report.pop('fixture_tuning',None);self.report.pop('tuning_reason',None)
  self.chest=None;self.perks=None;self.record_at=0;self.mode_at=0;self.states={};self.attempt=0;self.candidate=0
 def write(self):
  self.output.mkdir(parents=True,exist_ok=True)
  (self.output/'Report.json').write_text(json.dumps(self.report,ensure_ascii=False,indent=2,allow_nan=False),encoding='utf-8')
 def check(self,name,condition):
  self.report['checks'][name]=bool(condition);self.require(condition,name)
 def snapshot(self):
  return [{'id':str(prop(s,'perk_id')),'charges':int(prop(s,'charges')),
   'instance':str(prop(s,'instance_id')),'cooldown':float(self.perks.get_skill_cooldown_remaining(i))}
   for i,s in enumerate(prop(self.perks,'active_skill_slots'))]
 def spawn_chest(self):
  at=self.hero.get_actor_location();floor=self.floor(at+u.Vector(180,0,0));self.require(floor is not None,'Chest fixture has arena support')
  landing=floor+u.Vector(0,0,3);pose=u.Transform(location=landing)
  zone=self.statics.call_method('BeginDeferredActorSpawnFromClass',args=(self.world,u.MCRewardDropZone.static_class(),pose,u.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN,None,u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
  self.statics.call_method('FinishSpawningActor',args=(zone,pose,u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
  zone.set_editor_properties(dict(allowed_landing_surface=self.tongue,allow_reward_drops=False,inward_margin=20.))
  prop(zone,'area').set_box_extent(u.Vector(500,500,400),False)
  cls=u.load_class(None,'/Game/Gameplay/Roguelike/BP_RewardChest.BP_RewardChest_C')
  actor=self.statics.call_method('BeginDeferredActorSpawnFromClass',args=(self.world,cls,pose,
   u.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN,None,u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
  actor.initialize_team_stage_reward(landing,landing+u.Vector(0,0,450),7721+self.attempt,u.load_asset('/Game/Gameplay/Roguelike/DT_Perks'),zone)
  self.statics.call_method('FinishSpawningActor',args=(actor,pose,u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT));self.chest=actor
 def tick(self,delta):
  if self.done:return
  try:
   wall=time.monotonic()-self.started;self.require(wall<115,'Solo probe wall-time limit')
   worlds=list(u.EditorLevelLibrary.get_pie_worlds(True))
   if self.world is None:
    if not worlds:return
    self.require(len(worlds)==1 and u.SystemLibrary.is_server(worlds[0]),'One standalone authority PIE')
    self.world=worlds[0];self.pc=u.GameplayStatics.get_player_controller(self.world,0)
    if not self.pc or not self.pc.get_controlled_pawn():self.world=None;return
    self.hero=self.pc.get_controlled_pawn();self.engine.set_editor_property('bUseFixedFrameRate',False);self.tongue=self.actors(u.MCTongue)[0]
    self.report['world']=self.world.get_path_name();self.phase='readiness';self.mode_at=wall;return
   self.require(self.world in worlds,'Owned PIE still active')
   if self.phase=='readiness':
    loading=[w for w in u.ObjectIterator(u.load_class(None,'/Script/MessControl.MCLoadingWidget')) if w.get_owning_player()==self.pc]
    if any(w.is_in_viewport() and w.get_visibility() not in [u.SlateVisibility.COLLAPSED,u.SlateVisibility.HIDDEN] for w in loading):return
    if wall-self.mode_at<2:return
    self.report['fixture_result']=u.MCRoguelikeEditorLibrary.review_rogue_gameplay(self.world,'level')
    self.ps=u.GameplayStatics.get_player_state(self.world,0);self.perks=self.ps.get_component_by_class(u.MCPerkComponent)
    granted=[]
    for i in range(1,36):
     ident='P%02d'%i
     if self.perks.try_acquire_perk(ident):granted.append(ident)
     if len(granted)==9:break
    self.check('Nine different passives have no eight-item gameplay cap',len(granted)==9)
    self.report['passives']=granted
    for ident in ['Active_BrushFury','Consumable_RepairKit','Fallback_Mouthwash','Fallback_Grenade']:
     self.check('Acquire '+ident,self.perks.try_acquire_perk(ident))
    self.check('Owned duplicate is excluded',not self.perks.can_offer_perk('Consumable_RepairKit'))
    self.report['initial_slots']=self.snapshot();self.phase='hud';self.mode_at=wall
   elif self.phase=='hud' and wall-self.mode_at>1.2:
    self.screenshot('01_four_skills_nine_passives.png')
    status=self.hero.get_component_by_class(u.MCToothStatusComponent);state=status.get_editor_property('State')
    state.set_editor_property('Health',60.);status.set_editor_property('State',state)
    self.report['health_fixture']='Set 60 HP on the owned PIE tooth; ApplyDamage is not connected to this game status component.'
    self.report['health_before_use']=float(prop(self.hero.get_component_by_class(u.MCToothStatusComponent),'state').get_editor_property('Health'))
    self.report['use_repair']=u.MCRoguelikeEditorLibrary.review_rogue_gameplay(self.world,'skill2')
    self.report['used_slots']=self.snapshot()
    self.check('Using repair kit consumes one of three charges',self.report['used_slots'][1]['charges']==2)
    self.report['use_active']=u.MCRoguelikeEditorLibrary.review_rogue_gameplay(self.world,'skill1')
    self.phase='cooldown';self.mode_at=wall
   elif self.phase=='cooldown' and wall-self.mode_at>2:
    self.screenshot('02_cooldown_and_two_charges.png');self.report['cooldown_slots']=self.snapshot()
    self.check('Active has its own running cooldown',self.report['cooldown_slots'][0]['cooldown']>0)
    self.check('Other slots are independently ready',self.report['cooldown_slots'][2]['cooldown']==0)
    self.phase='wait_fury';self.mode_at=wall
   elif self.phase=='wait_fury' and wall-self.mode_at>4:
    self.spawn_chest();self.phase='chest';self.mode_at=wall
   elif self.phase=='chest':
    self.report['last_chest_state']=dict(stage=str(prop(self.chest,'stage')),position=str(self.chest.get_actor_location()),player=str(self.hero.get_actor_location()))
    if self.pc.is_reward_menu_open():
     ids=list(self.chest.call_method('GetStageOfferIDs',args=(self.ps,)));kinds=[self.perks.get_perk_kind(ident) for ident in ids]
     self.report.setdefault('issued_offers',[]).append(dict(ids=[str(i) for i in ids],kinds=[str(k) for k in kinds]))
     choices=[i for i,k in enumerate(kinds) if k in [u.MCPerkKind.ACTIVE,u.MCPerkKind.CONSUMABLE]]
     if not choices:
      self.pc.skip_reward_perk();self.chest.destroy_actor();self.attempt+=1
      self.require(self.attempt<12,'Disclosed reward fixtures did not offer an active item within the bounded sample')
      self.phase='retry_chest';self.mode_at=wall
     else:
      self.candidate=choices[0];self.replacement_id=str(ids[self.candidate]);self.screenshot('03_personal_reward_cards.png')
      self.report['before_choice_slots']=self.snapshot();self.phase='choice_capture';self.mode_at=wall
    self.require(wall-self.mode_at<30,'Chest opens through normal solo approach')
   elif self.phase=='retry_chest' and wall-self.mode_at>.8:
    self.spawn_chest();self.phase='chest';self.mode_at=wall
   elif self.phase=='choice_capture' and wall-self.mode_at>2:
    self.pc.choose_reward_perk(self.candidate);self.phase='after_choice';self.mode_at=wall
   elif self.phase=='after_choice' and wall-self.mode_at>2:
    self.screenshot('04_full_slots_choose_replacement.png')
    self.check('No replacement before explicit confirmation',self.snapshot()[0]['id']=='Active_BrushFury')
    self.pc.choose_reward_slot(0);self.phase='replacement_done';self.mode_at=wall
   elif self.phase=='replacement_done' and wall-self.mode_at>1.3:
    self.screenshot('05_explicit_slot_replacement.png');self.report['final_slots']=self.snapshot()
    self.check('Explicit replacement equips the chosen item',self.report['final_slots'][0]['id']==self.replacement_id)
    self.check('Replaced item may return to offers',self.perks.can_offer_perk('Active_BrushFury'))
    self.check('Other slot retains its two remaining charges',self.report['final_slots'][1]['charges']==2)
    self.check('Selection closes the personal reward menu',not self.pc.is_reward_menu_open())
    self.finish();return
   if self.phase not in ['waiting_pie','readiness'] and wall>=self.record_at:
    self.record_at=wall+1/4;self.screenshot('%05d.png'%len(self.report['frames']),sequence=True)
  except Exception:self.finish(traceback.format_exc())
probe=FoundationSolo();builtins.mc_roguelike_foundation_solo=probe
try:probe.start()
except Exception:probe.finish(traceback.format_exc());raise
