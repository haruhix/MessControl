"""Join the two completed review cameras, starting with the dynamic samples."""
import json,hashlib
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
DIRECTORY=ROOT/'Artifacts/CarriesStage2/ArtistReview'


def main():
    parts=[]
    paths=[]
    for view in ('ThreeQuarter','Front'):
        path=DIRECTORY/('RenderManifest_'+view+'.json')
        raw=path.read_bytes();part=json.loads(raw)
        assert part['status']=='complete' and part['source_files_unchanged']
        parts.append(part);paths.append(dict(path=str(path),sha256=hashlib.sha256(raw).hexdigest()))
    for key in ('fps','size','sources','author_report_sha256','source_fps','scope','limitations'):
        assert parts[0][key]==parts[1][key],key
    clips={c['name']:c for p in parts for c in p['clips']}
    frames={name:[]for name in clips}
    for part in parts:
        for record in part['frames']:frames[record['clip']].append(record)
    names=[motion+'_'+view for motion in ('GroundSlam','Sweep','Hurt','Idle')for view in ('ThreeQuarter','Front')]
    assert set(names)==set(clips)
    ordered=[];records=[]
    for ordinal,name in enumerate(names,1):
        clip=clips[name]
        batch=sorted(frames[name],key=lambda row:row['index'])
        assert [r['index']for r in batch]==list(range(clip['frame_count']))
        ordered.append(dict(clip,render_ordinal=clip['ordinal'],ordinal=ordinal))
        records.extend(batch)
    manifest={k:v for k,v in parts[0].items()if k not in ('clips','frames','view','camera','ortho_scale','wall_seconds')}
    manifest.update(status='complete',clips=ordered,frames=records,rendered_png_count=len(records),
        cameras=[dict(view=p['view'],camera=p['camera'],ortho_scale=p['ortho_scale'])for p in parts],merged_parts=paths)
    output=DIRECTORY/'RenderManifest.json'
    assert not output.exists(),'Preserve prior complete reviews'
    output.write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf8')
    print('CARRIES_REVIEW_MERGED',len(ordered),len(records),flush=True)


if __name__=='__main__':main()
