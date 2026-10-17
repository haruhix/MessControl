"""Join the two completed locomotion camera manifests without rerendering."""
import json,hashlib
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
DIRECTORY=ROOT/'Artifacts/CarriesStage2/HandSupportWalk_v4'

def main():
    parts=[];sources=[]
    for view in ('ThreeQuarter','Side'):
        path=DIRECTORY/('RenderManifest_'+view+'.json')
        raw=path.read_bytes();part=json.loads(raw)
        assert part['status']=='complete' and part['source_files_unchanged']
        parts.append(part);sources.append(dict(path=str(path),sha256=hashlib.sha256(raw).hexdigest()))
    for key in ('fps','size','sources','author_report_sha256','source_fps','scope','limitations'):
        assert parts[0][key]==parts[1][key],key
    clips=[];frames=[]
    for part in parts:
        for clip in part['clips']:
            rows=sorted((f for f in part['frames']if f['clip']==clip['name']),key=lambda row:row['index'])
            assert [f['index']for f in rows]==list(range(clip['frame_count']))
            clips.append(dict(clip,ordinal=len(clips)+1));frames+=rows
    output=DIRECTORY/'RenderManifest.json'
    assert not output.exists(),'Preserve previous review'
    manifest={k:v for k,v in parts[0].items()if k not in ('clips','frames','view','camera','ortho_scale','wall_seconds')}
    manifest.update(clips=clips,frames=frames,rendered_png_count=len(frames),merged_parts=sources)
    output.write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf8')
    print('CARRIES_WALK_MERGED',len(clips),len(frames),flush=True)

if __name__=='__main__': main()
