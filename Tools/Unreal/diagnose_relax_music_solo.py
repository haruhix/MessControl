"""Observe the real controller's automatic audio route, then a forced Relax route.

One owned standalone PIE, original palette and imported track remain unchanged.
Master mixer capture supplements real source/voice/state observations. Forced
Relax is a temporary controller override for diagnosis, not a delivered fix.
"""
import unreal as u,builtins,json,time,traceback
from pathlib import Path

class RelaxDiagnosis:
 def __init__(self):
  self.root=Path(u.Paths.project_dir());self.out=self.root/'Artifacts/RelaxMusic_20261016';self.out.mkdir(exist_ok=True)
  self.level=u.get_editor_subsystem(u.LevelEditorSubsystem);assert not self.level.is_in_play_in_editor()
  play=u.get_default_object(u.load_class(None,'/Script/UnrealEd.LevelEditorPlaySettings'));assert play.get_editor_property('PlayNumberOfClients')==1 and not play.get_editor_property('bLaunchSeparateServer')
  try:assert 'STANDALONE' in str(play.get_editor_property('PlayNetMode')).upper()
  except TypeError as e:assert "Cannot pythonize '0' (int64)" in str(e)
  self.perf=u.get_default_object(u.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'));self.old_throttle=self.perf.get_editor_property('bThrottleCPUWhenNotForeground');self.perf.set_editor_property('bThrottleCPUWhenNotForeground',False)
  self.cvars={k:u.SystemLibrary.get_console_variable_int_value(k) for k in ['t.IdleWhenNotForeground','au.DisableAppVolume']}
  ed=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
  for k,v in [('t.IdleWhenNotForeground',0),('au.DisableAppVolume',1)]:u.SystemLibrary.execute_console_command(ed,f'{k} {v}')
  self.started=time.monotonic();self.phase='waiting';self.rows=[];self.world=None;self.pc=None;self.recording=False;self.done=False;self.last=0
  self.handle=u.register_slate_post_tick_callback(self.tick);self.level.editor_request_begin_play();print('RELAX_DIAGNOSIS_STARTED')
 def tick(self,dt):
  if self.done:return
  try:
   now=time.monotonic();assert now-self.started<60,'Audio diagnostic timeout'
   if self.phase=='waiting':
    self.world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_game_world()
    if not self.world:return
    self.pc=u.GameplayStatics.get_player_controller(self.world,0)
    if not self.pc or not self.pc.get_editor_property('SceneAudio'):return
    self.component=self.pc.get_editor_property('SceneAudio');self.override=self.pc.get_editor_property('BackgroundAudioContextOverride');self.at=now;self.phase='automatic'
    u.AudioMixerLibrary.start_recording_output(self.world,20.,None);self.recording=True
   age=now-self.at
   if now-self.last>.1:
    self.last=now;gs=u.GameplayStatics.get_game_state(self.world);director=gs.get_editor_property('DirectorState')
    voices=[{'component':v.get_path_name(),'sound':v.get_editor_property('sound').get_path_name() if v.get_editor_property('sound') else None,'playing':v.is_playing(),'state':str(v.get_play_state())} for v in self.component.get_voice_components()]
    self.rows.append({'stage':self.phase,'time':age,'world_time':u.GameplayStatics.get_time_seconds(self.world),'context':str(self.component.get_editor_property('SceneContext')),'phase':str(gs.get_editor_property('Phase')),'tutorial':gs.get_editor_property('bTutorialActive'),'lobby':gs.get_editor_property('bLobbyWaiting'),'pacing':str(director.get_editor_property('Pacing')),'voices':voices})
   if self.phase=='automatic' and age>8:
    self.pc.set_background_audio_context('Relax');self.phase='forced_relax';print('RELAX_DIAGNOSIS_FORCED')
   if age>17:self.finish()
  except Exception:self.finish(traceback.format_exc())
 def finish(self,error=None):
  if self.done:return
  self.done=True;u.unregister_slate_post_tick_callback(self.handle)
  if self.recording:u.AudioMixerLibrary.stop_recording_output(self.world,u.AudioRecordingExportType.WAV_FILE,'Diagnosis',str(self.out),None)
  if self.pc:self.pc.set_background_audio_context(self.override)
  if self.world:
   for k,v in self.cvars.items():u.SystemLibrary.execute_console_command(self.world,f'{k} {v}')
  self.perf.set_editor_property('bThrottleCPUWhenNotForeground',self.old_throttle)
  self.level.editor_request_end_play()
  (self.out/'Diagnosis.json').write_text(json.dumps({'passed':error is None,'error':error,'samples':self.rows,'scope':__doc__},indent=2));print('RELAX_DIAGNOSIS_FINISHED',error)

builtins.mc_relax_diagnosis=RelaxDiagnosis()
