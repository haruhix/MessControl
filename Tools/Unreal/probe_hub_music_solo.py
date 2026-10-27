"""Root-run owned Hub PIE: actual three-track PCM rotation and scene gain.

Uses the saved Hub scene, real local controller/scene voice, and native
OnAudioPlaybackPercent callbacks mapped to bit-verified source boundaries. Temporary pitch is limited to the
engine's current global maximum (normally 2), then restored before ending our
PIE. No source asset, user volume, profile, or level is edited or saved.
"""
import builtins
import datetime
import hashlib
import json
from pathlib import Path
import runpy
import struct
import time
import traceback
import uuid
import unreal as u

ROOT=Path(u.Paths.project_dir()).resolve()
SUPPORT=runpy.run_path(str(ROOT/'Tools/Unreal/probe_clean_respawn_solo.py'),run_name='hub_music_support')
Base,prop,valid,require=(SUPPORT[k] for k in ('CleanRespawnSolo','prop','valid','require'))
PLAY=dict(SUPPORT['PLAY'],enableGameSound=True,clientFixedFPS=[0])
Base.start.__globals__['PLAY']=PLAY
KEY='mc_hub_music_solo'
CUE='/Game/Audio/Hub/S_Hub_AmbientRotation.S_Hub_AmbientRotation'
ORDER=[1,2,3,1]
MANIFEST=ROOT/'ArtSource/Audio/Hub/Hub_AmbientRotation.json'

def path(obj): return obj.get_path_name() if valid(obj) else None

class HubMusicSolo(Base):
    def __init__(self):
        self.done=self.requested_pie=False
        self.callback=self.pie_start=self.world=self.pc=self.identity=None
        self.viewport_overrides=[]
        self.phase='waiting_pie';self.started=self.phase_at=time.monotonic()
        self.editor=u.get_editor_subsystem(u.UnrealEditorSubsystem)
        self.levels=u.get_editor_subsystem(u.LevelEditorSubsystem)
        require(not self.levels.is_in_play_in_editor() and not u.EditorLevelLibrary.get_pie_worlds(True),'Preexisting PIE preserved')
        require('-mchubtransientprofile' in u.SystemLibrary.get_command_line().lower(),'Launch with -MCHubTransientProfile')
        self.editor_world=self.editor.get_editor_world();self.editor_path=self.editor_world.get_path_name()
        require(self.editor_path=='/Game/Maps/Hub.Hub','Open saved Hub first')
        dirty=list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())+list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages())
        require(not dirty,'Preserving unrelated dirty packages')
        self.play=u.load_object(None,'/Script/UnrealEd.Default__LevelEditorPlaySettings')
        self.old_play=json.loads(u.ToolsetLibrary.get_object_properties(self.play,list(PLAY)))
        self.engine=next(o for o in u.ObjectIterator(u.load_class(None,'/Script/Engine.Engine')) if o.get_path_name().startswith('/Engine/Transient.'))
        self.old_engine=(prop(self.engine,'bUseFixedFrameRate'),prop(self.engine,'FixedFrameRate'))
        self.perf=u.get_default_object(u.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'))
        self.old_throttle=prop(self.perf,'bThrottleCPUWhenNotForeground')
        self.old_idle=u.SystemLibrary.get_console_variable_int_value('t.IdleWhenNotForeground')
        self.old_appvolume=u.SystemLibrary.get_console_variable_int_value('au.DisableAppVolume')
        stamp=datetime.datetime.now(datetime.timezone.utc).strftime('Run_%Y%m%dT%H%M%SZ_')+uuid.uuid4().hex[:6]
        self.output=Path('E:/CodexWork/HubMusic_20261024/Solo')/stamp
        self.output.mkdir(parents=True,exist_ok=False)
        self.component=None;self.voices={};self.bindings={};self.pitch_before={};self.preferences=None
        self.next_snapshot=0.;self.play_started=None;self.target_pitch=1.;self.callback_armed=False
        self.report=dict(status='running',passed=False,phase=self.phase,editor_world=self.editor_path,
            network_tested=False,profile_files_touched=False,package_save_requested=False,
            transitions=[],snapshots=[],assertions=[],expected_order=ORDER,
            fixtures=['Temporary pitch acceleration on the actual scene voice, limited to the existing global maximum',
                      'One initial FadeIn(0,.5) restart after binding playback callbacks; native AudioComponent samples delegate binding when each ActiveSound starts',
                      'No track skipping, scene replacement, asset writes or user volume preference edits'],
            limitations=['The three original recordings are one ordered, sample-preserved PCM wave; original track boundaries are observed via actual playback percent and verified byte offsets',
                         'Pitch-accelerated technical rotation check, not normal-tempo artistic approval or audible loudness measurement',
                         'No packaged first-launch or network proof'])

    def start(self):
        super().start()
        self.engine.set_editor_property('bUseFixedFrameRate',False)
        u.SystemLibrary.execute_console_command(self.editor_world,'au.DisableAppVolume 1')

    def preference_values(self):
        p=self.preferences
        return dict(master=p.get_master_volume(),music=p.get_music_volume(),ambience=p.get_ambience_volume(),sfx=p.get_sfx_volume(),voice=p.get_voice_volume())

    def check(self,label,ok,**evidence):
        self.report['assertions'].append(dict(label=label,passed=bool(ok),**evidence));self.write();require(ok,label)

    def bind(self,voice):
        name=path(voice)
        if name in self.bindings:return
        self.voices[name]=voice
        self.pitch_before[name]=float(prop(voice,'pitch_multiplier'))
        def percent_callback(wave,percent):
            if self.done or not valid(wave):return
            pct=float(percent)
            if path(wave)!=CUE or pct<0. or pct>1.:return
            if not self.callback_armed:
                # Restarting an existing voice can deliver its old completion
                # sentinel before the new ActiveSound begins at the start.
                # Arm only on an actual early sample, then accept every percent.
                if pct>.05:
                    self.report['ignored_initial_callback_count']=self.report.get('ignored_initial_callback_count',0)+1
                    ignored=self.report.setdefault('ignored_initial_callbacks',[])
                    if len(ignored)<8:ignored.append(dict(percent=pct,asset=path(wave),wall_seconds=time.monotonic()-self.started))
                    return
                self.callback_armed=True
                self.report['callback_armed']=dict(percent=pct,wall_seconds=time.monotonic()-self.started)
            elapsed=pct*self.report['rotation_seconds']
            track=max(row['index'] for row in self.manifest['tracks'] if elapsed+1.e-5>=row['start_seconds'])
            transitions=self.report['transitions']
            if not transitions or transitions[-1]['track']!=track:
                transitions.append(dict(track=track,original=self.manifest['order'][track-1],asset=path(wave),percent=pct,source_seconds=elapsed,
                    wall_seconds=time.monotonic()-self.started,voice=name))
                self.write()
        voice.on_audio_playback_percent.add_callable(percent_callback)
        self.bindings[name]=percent_callback
        voice.set_pitch_multiplier(self.target_pitch)
        # PlayInternal samples callback binding only when the ActiveSound starts.
        # A single disclosed restart arms observation; every subsequent leaf
        # transition and whole-wave repetition must happen naturally.
        voice.fade_in(0.,.5,0.)
        self.report['initial_restart']=dict(voice=name,scene_gain=.5,pitch=self.target_pitch,
            wall_seconds=time.monotonic()-self.started,method='FadeIn(0,.5,0) after callback binding')

    def initialize_scene(self):
        self.component=prop(self.pc,'scene_audio')
        require(valid(self.component),'No actual scene audio component')
        self.check('Actual Hub context selected',str(prop(self.component,'scene_context'))=='Hub')
        palette=prop(self.component,'sound_palette');require(valid(palette),'No actual palette')
        scenes=prop(palette,'scenes');hub=next(v for k,v in scenes.items() if str(k)=='Hub')
        music=prop(hub,'music')
        self.check('Hub scene authored at 50 percent',abs(float(prop(music,'volume'))-.5)<.0001)
        self.check('Whole rotation repeats on natural completion',bool(prop(music,'bRepeatOnFinish')))
        wave=u.load_asset(CUE);require(isinstance(wave,u.SoundWave),'Missing delivered rotation wave')
        self.check('Rotation source has unit base gain',abs(float(prop(wave,'volume'))-1.)<.0001)
        self.check('Natural scene completion owns repetition',not bool(prop(wave,'looping')))
        self.manifest=json.loads(MANIFEST.read_text(encoding='utf-8-sig'))
        pcm_file=ROOT/'ArtSource/Audio/Hub/Hub_AmbientRotation.wav'
        blob=pcm_file.read_bytes();offset=12;pcm=None
        while offset+8<=len(blob):
            chunk,length=struct.unpack_from('<4sI',blob,offset);offset+=8
            if chunk==b'data':pcm=blob[offset:offset+length];break
            offset+=length+(length%2)
        require(pcm is not None,'Combined WAV has no PCM data chunk')
        self.check('Combined PCM matches saved manifest',hashlib.sha256(pcm).hexdigest()==self.manifest['combined_pcm_sha256'])
        for row in self.manifest['tracks']:
            segment=pcm[row['byte_offset']:row['byte_offset']+row['pcm_bytes']]
            self.check('Original track '+str(row['index'])+' sample bytes occur in the authored order',hashlib.sha256(segment).hexdigest()==row['pcm_sha256'])
        self.report['pcm_manifest']=str(MANIFEST)
        self.report['rotation_seconds']=float(prop(wave,'duration'))
        self.check('Runtime wave duration matches ordered source',abs(self.report['rotation_seconds']-float(self.manifest['duration']))<.01)
        audio_settings=u.get_default_object(u.load_class(None,'/Script/Engine.AudioSettings'))
        self.target_pitch=max(1.,min(4.,float(prop(audio_settings,'global_max_pitch_scale'))))
        require(self.target_pitch>=1.5,'Existing global pitch limit too low for this bounded probe')
        self.report.update(sound=CUE,requested_pitch=4.,
            actual_requested_pitch=self.target_pitch,global_pitch_limit=float(prop(audio_settings,'global_max_pitch_scale')))
        self.preferences=u.GameUserSettings.get_game_user_settings()
        require(isinstance(self.preferences,u.MCGameUserSettings),'Expected project user settings')
        self.report['preferences_before']=self.preference_values()
        self.play_started=time.monotonic();self.stage('observe_rotation')

    def tick(self,_dt):
        if self.done:return
        try:
            require(time.monotonic()-self.started<190.,'Hub music probe exceeded 190 seconds')
            worlds=list(u.EditorLevelLibrary.get_pie_worlds(True))
            if self.world is None:
                if not worlds:return
                require(len(worlds)==1 and u.SystemLibrary.is_server(worlds[0]),'Expected own standalone authority PIE')
                self.world=worlds[0];require(u.GameplayStatics.get_current_level_name(self.world,True)=='Hub','Unexpected map')
                self.pc=u.GameplayStatics.get_player_controller(self.world,0)
                require(isinstance(self.pc,u.MCPlayerController),'Expected gameplay controller')
                self.stage('startup_ready')
            require(len(worlds)==1 and worlds[0]==self.world,'PIE replaced; replacement preserved')
            if self.phase=='startup_ready':
                if self.pc.is_world_presentation_ready() and time.monotonic()-self.phase_at>2.:
                    self.initialize_scene()
                return
            if self.phase=='observe_rotation':
                require(str(prop(self.component,'scene_context'))=='Hub','Unexpected scene switch')
                voices=list(self.component.get_voice_components())
                music=[v for v in voices if path(prop(v,'sound'))==CUE]
                require(len(music)==1,'Expected exactly one actual playlist voice')
                self.bind(music[0])
                require(music[0].is_playing(),'Playlist voice stopped unexpectedly')
                require(abs(float(prop(music[0],'volume_multiplier'))-1.)<.0001,'Component base gain changed')
                if time.monotonic()>=self.next_snapshot:
                    self.report['snapshots'].append(dict(wall_seconds=time.monotonic()-self.started,
                        voices=[dict(component=path(v),sound=path(prop(v,'sound')),playing=v.is_playing(),
                            pitch=float(prop(v,'pitch_multiplier')),base_volume=float(prop(v,'volume_multiplier'))) for v in voices]))
                    self.next_snapshot=time.monotonic()+5.;self.write()
                observed=[t['track'] for t in self.report['transitions']]
                require(observed==ORDER[:len(observed)],'Unexpected actual leaf order: '+str(observed))
                if len(observed)>=4:
                    self.check('Actual playback crosses original segments 1 to 2 to 3 then naturally repeats 1',observed[:4]==ORDER,observed=observed)
                    span=self.report['transitions'][3]['wall_seconds']-self.report['transitions'][0]['wall_seconds']
                    self.check('Natural full-cycle timing agrees with temporary pitch',span>=self.report['rotation_seconds']/self.target_pitch*.85,observed_seconds=span)
                    self.check('Existing user volume preferences preserved',self.preference_values()==self.report['preferences_before'])
                    self.check('One retained playlist voice throughout rotation',len(self.voices)==1)
                    self.finish()
                elif time.monotonic()-self.play_started>15. and not observed:
                    raise RuntimeError('No actual playback callbacks; requested source alone is insufficient evidence')
        except Exception:
            self.finish(traceback.format_exc())

    def finish(self,error=None):
        if self.done:return
        cleanup=[]
        for name,callback in self.bindings.items():
            voice=self.voices.get(name)
            if valid(voice):
                try:
                    voice.on_audio_playback_percent.remove_callable(callback)
                    voice.set_pitch_multiplier(self.pitch_before[name])
                except Exception:cleanup.append(traceback.format_exc())
        self.bindings.clear()
        try:u.SystemLibrary.execute_console_command(self.editor_world,'au.DisableAppVolume '+str(self.old_appvolume))
        except Exception:cleanup.append(traceback.format_exc())
        self.report['audio_cleanup_errors']=cleanup
        super().finish(error or ('Audio cleanup failed: '+str(cleanup) if cleanup else None))

def main():
    old=getattr(builtins,KEY,None);require(old is None or old.done,'Hub music probe already active')
    probe=HubMusicSolo();setattr(builtins,KEY,probe)
    try:probe.start()
    except Exception:
        probe.finish(traceback.format_exc());raise
    return probe

if __name__=='__main__':main()
