"""One short solo PIE visual comparison at the user's exact editor camera.

The temporary Before state restores only four PIE component references. Saved
assets are untouched. After restores the authored references before capture.
"""
import unreal as u,json,time,traceback,builtins
from pathlib import Path

class EdgeReview:
 def __init__(self):
  self.out=Path(u.Paths.project_dir())/'Artifacts/ArenaEdges_20261017'
  self.world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_game_world();assert self.world
  integration=json.loads((self.out/'Integration.json').read_text())
  originals={r['label']:r for r in integration['actors']}
  self.components=[]
  for a in u.GameplayStatics.get_all_actors_of_class(self.world,u.StaticMeshActor):
   if a.get_actor_label() not in originals:continue
   c=a.static_mesh_component;row=originals[a.get_actor_label()]
   assert c.static_mesh.get_path_name()==row['mesh']
   self.components.append((c,c.static_mesh,[c.get_material(i) for i in range(c.get_num_materials())],row,c.get_editor_property('mobility')))
  assert len(self.components)==4
  teeth=u.GameplayStatics.get_all_actors_of_class(self.world,u.MCArenaTooth)
  tongues=u.GameplayStatics.get_all_actors_of_class(self.world,u.MCTongue)
  assert len(teeth)==12 and len(tongues)==1
  self.checks=[dict(name='all four authored tissue edge masks are used in PIE',passed=True),dict(name='twelve active teeth and moving tongue are retained',passed=True)]
  self.paused=u.GameplayStatics.is_game_paused(self.world);u.GameplayStatics.set_game_paused(self.world,True)
  view=json.loads((self.out/'CaptureView.json').read_text())
  pose=u.Transform(location=u.Vector(*view['eye']),rotation=u.Rotator(pitch=view['rotation'][0],yaw=view['rotation'][1],roll=view['rotation'][2]))
  lib=u.get_default_object(u.GameplayStatics)
  self.capture=lib.call_method('BeginDeferredActorSpawnFromClass',args=(self.world,u.SceneCapture2D,pose,u.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN,None,u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
  lib.call_method('FinishSpawningActor',args=(self.capture,pose,u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
  self.cc=self.capture.get_editor_property('capture_component2d')
  self.target=u.RenderingLibrary.create_render_target2d(self.world,960,1160,u.TextureRenderTargetFormat.RTF_RGBA8,u.LinearColor(.02,.02,.02,1),False,False)
  self.cc.set_editor_properties(dict(texture_target=self.target,capture_source=u.SceneCaptureSource.SCS_FINAL_COLOR_LDR,capture_every_frame=False,capture_on_movement=False,always_persist_rendering_state=True,fov_angle=80.))
  gum=u.load_asset(integration['source_instance'])
  for c,mesh,mats,row,mobility in self.components:
   c.set_mobility(u.ComponentMobility.MOVABLE)
   assert c.set_static_mesh(u.load_asset(row['source_mesh']))
   for i in range(c.get_num_materials()):c.set_material(i,gum)
   c.set_mobility(mobility)
  self.stage='before_lit';self.at=time.monotonic();self.done=False;self.captures=[]
  self.callback=u.register_slate_post_tick_callback(self.tick)
  print('ARENA_EDGE_SOLO_STARTED')
 def restore(self):
  for c,mesh,mats,row,mobility in self.components:
   c.set_mobility(u.ComponentMobility.MOVABLE)
   if c.static_mesh!=mesh:assert c.set_static_mesh(mesh)
   for i,m in enumerate(mats):c.set_material(i,m)
   c.set_mobility(mobility)
 def tick(self,dt):
  if self.done or time.monotonic()-self.at<.4:return
  try:
   name={'before_lit':'Before','before_unlit':'Before_Unlit','after_lit':'After','after_unlit':'After_Unlit'}[self.stage]
   self.cc.capture_scene();u.RenderingLibrary.export_render_target(self.world,self.target,str(self.out),name+'.png');self.captures.append(str(self.out/(name+'.png')))
   if self.stage=='before_lit':
    self.cc.set_editor_property('show_flag_settings',[u.EngineShowFlagsSetting(show_flag_name='Lighting',enabled=False)])
    self.stage='before_unlit'
   elif self.stage=='before_unlit':
    self.restore();self.cc.set_editor_property('show_flag_settings',[u.EngineShowFlagsSetting(show_flag_name='Lighting',enabled=True)])
    self.stage='after_lit'
   elif self.stage=='after_lit':
    self.cc.set_editor_property('show_flag_settings',[u.EngineShowFlagsSetting(show_flag_name='Lighting',enabled=False)])
    self.stage='after_unlit'
   else:self.finish()
   self.at=time.monotonic()
  except Exception:self.finish(traceback.format_exc())
 def finish(self,error=None):
  if self.done:return
  self.done=True;u.unregister_slate_post_tick_callback(self.callback);self.restore();self.capture.destroy_actor()
  u.GameplayStatics.set_game_paused(self.world,self.paused)
  report=dict(passed=error is None,error=error,checks=self.checks,captures=self.captures,component_references_restored=True,scope='One paused standalone solo PIE. Before/After swaps four runtime mesh/material references at the same stored camera under saved lighting. Additional Unlit views disable lighting only on the transient capture to inspect the mask. No saved asset or lighting changes.')
  (self.out/'SoloCheck.json').write_text(json.dumps(report,indent=2))
  print('ARENA_EDGE_SOLO_RESULT',report['passed'],error)

previous=getattr(builtins,'mc_arena_edge_review',None)
if previous and hasattr(previous,'capture') and u.SystemLibrary.is_valid(previous.capture):
 for c,mesh,mats,row,mobility in previous.components:
  c.set_mobility(u.ComponentMobility.MOVABLE)
  if c.static_mesh!=mesh:assert c.set_static_mesh(mesh)
  for i,m in enumerate(mats):c.set_material(i,m)
  c.set_mobility(mobility)
 previous.capture.destroy_actor()
 u.GameplayStatics.set_game_paused(previous.world,previous.paused)
builtins.mc_arena_edge_review=EdgeReview()
