"""Import a sample-exact three-track Hub rotation; alter only Hub.Music.

Native scene repetition plays 1 -> 2 -> 3 -> 1 at .5 authored gain.
Original downloads remain read-only. All working/source files stay on E:.
"""
import hashlib
import json
import shutil
import struct
from pathlib import Path
import unreal as u

ROOT=Path(u.Paths.project_dir()).resolve()
WORK=Path('E:/CodexWork/HubMusic_20261024')
SOURCE=Path('C:/Users/user/Downloads/WAV-20260923T155817Z-1-001/WAV')
NAMES=['Ambient 1 - Guitars Medium Jingle Preview.wav','Ambient 2 - Synths and Guitars Preview.wav','Ambient 3 - Guitars Calm Preview.wav']
ASSET='/Game/Audio/Hub/S_Hub_AmbientRotation'
def require(ok,message):
    if not ok: raise RuntimeError(message)
def sha(data): return hashlib.sha256(data).hexdigest()
def chunks(data):
    require(data[:4]==b'RIFF' and data[8:12]==b'WAVE','Not a RIFF WAV')
    result={};p=12
    while p+8<=len(data):
        kind=data[p:p+4];size=struct.unpack_from('<I',data,p+4)[0]
        result[kind]=data[p+8:p+8+size];p+=8+size+(size&1)
    require(b'fmt ' in result and b'data' in result,'Missing WAV format/data')
    return result
def chunk(kind,data): return kind+struct.pack('<I',len(data))+data+(b'\0' if len(data)&1 else b'')
def main():
    require(ROOT.drive.upper()=='E:','Outputs must be on E:')
    if '-run=pythonscript' not in u.SystemLibrary.get_command_line().lower():
        require(not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor(),'Preserving existing PIE')
    WORK.mkdir(parents=True,exist_ok=True)
    source_dir=ROOT/'ArtSource/Audio/Hub';source_dir.mkdir(parents=True,exist_ok=True)
    raw=[];tracks=[];fmt=None;offset=0
    for index,name in enumerate(NAMES,1):
        original=SOURCE/name
        if not original.is_file(): original=source_dir/('Ambient%02d_Source.wav'%index)
        data=original.read_bytes();parsed=chunks(data)
        if fmt is None: fmt=parsed[b'fmt ']
        require(parsed[b'fmt ']==fmt,'WAV formats differ; no implicit resampling permitted')
        copied=source_dir/('Ambient%02d_Source.wav'%index)
        if not copied.exists() or sha(copied.read_bytes())!=sha(data): copied.write_bytes(data)
        payload=parsed[b'data'];raw.append(payload)
        tracks.append(dict(index=index,name=name,source=str(copied),source_sha256=sha(data),pcm_sha256=sha(payload),byte_offset=offset,pcm_bytes=len(payload)))
        offset+=len(payload)
    tag,channels,rate,byte_rate,align,bits=struct.unpack_from('<HHIIHH',fmt)
    require(align>0 and all(len(d)%align==0 for d in raw),'Invalid sample alignment')
    pcm=b''.join(raw)
    body=b'WAVE'+chunk(b'fmt ',fmt)+chunk(b'fact',struct.pack('<I',len(pcm)//align))+chunk(b'data',pcm)
    target=source_dir/'Hub_AmbientRotation.wav';target.write_bytes(b'RIFF'+struct.pack('<I',len(body))+body)
    verified=chunks(target.read_bytes())[b'data']
    for track in tracks:
        start=track['byte_offset'];end=start+track['pcm_bytes']
        require(sha(verified[start:end])==track['pcm_sha256'],'Concatenation changed source samples')
        track.update(start_seconds=start/byte_rate,duration=track['pcm_bytes']/byte_rate)
    palette=u.load_asset('/Game/Data/DA_MouthSounds');require(palette,'Missing palette')
    backup=WORK/'DA_MouthSounds.Before.uasset'
    if not backup.exists(): shutil.copy2(ROOT/'Content/Data/DA_MouthSounds.uasset',backup)
    old={str(k):v.export_text() for k,v in palette.get_editor_property('scenes').items()}
    wave=u.load_asset(ASSET) if u.EditorAssetLibrary.does_asset_exist(ASSET) else None
    if not wave:
        task=u.AssetImportTask()
        task.set_editor_properties(dict(filename=str(target),destination_path='/Game/Audio/Hub',destination_name='S_Hub_AmbientRotation',automated=True,replace_existing=False,save=False,factory=u.SoundFactory()))
        u.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task]);wave=u.load_asset(ASSET)
    require(isinstance(wave,u.SoundWave),'Native WAV import failed')
    wave.set_editor_property('looping',False);wave.set_editor_property('volume',1.)
    wave.set_editor_property('loading_behavior',u.SoundWaveLoadingBehavior.PRIME_ON_LOAD)
    wave.set_editor_property('compression_quality',80)
    require(abs(wave.get_editor_property('duration')-len(pcm)/byte_rate)<.01,'Imported duration mismatch')
    require(u.EditorAssetLibrary.save_loaded_asset(wave,only_if_is_dirty=False),'Wave save failed')
    scenes=dict(palette.get_editor_property('scenes'));key=next(k for k in scenes if str(k)=='Hub')
    hub=scenes[key];music=hub.get_editor_property('music')
    music.set_editor_properties(dict(sound=wave,volume=.5,bRepeatOnFinish=True,fade_in_seconds=1.,fade_out_seconds=1.))
    hub.set_editor_property('music',music);scenes[key]=hub;palette.set_editor_property('scenes',scenes)
    require(u.EditorAssetLibrary.save_loaded_asset(palette,only_if_is_dirty=False),'Palette save failed')
    new={str(k):v.export_text() for k,v in palette.get_editor_property('scenes').items()}
    require(all(new[k]==v for k,v in old.items() if k!='Hub'),'Unrelated scene changed')
    require(new['Hub'].split('Ambience=')[1]==old['Hub'].split('Ambience=')[1],'Hub ambience changed')
    for name in ('SC_Hub_AmbientRotation','S_Hub_Ambient01_GuitarsMedium','S_Hub_Ambient02_SynthsGuitars','S_Hub_Ambient03_GuitarsCalm'):
        path='/Game/Audio/Hub/'+name
        asset=u.load_asset(path) if u.EditorAssetLibrary.does_asset_exist(path) else None
        if asset:
            u.get_editor_subsystem(u.AssetEditorSubsystem).close_all_editors_for_asset(asset)
            require(u.EditorAssetLibrary.delete_asset(path),'Could not remove own unused experiment')
    report=dict(status='saved',asset=wave.get_path_name(),duration=float(wave.get_editor_property('duration')),volume=.5,repeat=True,order='1 -> 2 -> 3 -> 1',tracks=tracks,format_tag=tag,channels=channels,rate=rate,bits=bits,combined_source=str(target),source_sample_bytes_preserved=True,unrelated_scenes_unchanged=True,native_cpp_changed=False,runtime_tested=False,packaged_tested=False)
    (WORK/'Authoring.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    manifest=dict(order=[t['name'] for t in tracks],volume=.5,repeat=True,duration=report['duration'],
        combined_pcm_sha256=sha(pcm),tracks=[{k:v for k,v in t.items() if k!='source'} for t in tracks])
    (source_dir/'Hub_AmbientRotation.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
    print('MC_HUB_MUSIC_SAVED '+json.dumps(report))
if __name__=='__main__': main()
