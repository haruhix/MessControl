"""Replace one reviewed clip with its corrected render, keeping provenance."""
import hashlib
import json
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT/'Artifacts/WizardMixamoPool'
REPAIR = ROOT/'Artifacts/WizardMixamoPoolRepair'
NAME = 'Channel_SummonSustain'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    path = OUTPUT/'RenderManifest.json'
    fixed_path = REPAIR/'RenderManifest_16_16.json'
    manifest = json.loads(path.read_text(encoding='utf8'))
    fixed = json.loads(fixed_path.read_text(encoding='utf8'))
    assert manifest['status'] == fixed['status'] == 'complete'
    assert len(fixed['clips']) == 1 and fixed['clips'][0]['name'] == NAME
    for key in ('fps','size','camera','ortho_scale'):
        assert manifest[key] == fixed[key], key
    previous = next(row for row in manifest['clips'] if row['name']==NAME)
    corrected = dict(fixed['clips'][0], ordinal=previous['ordinal'])
    assert previous['frame_count'] == corrected['frame_count']
    assert len(fixed['frames']) == corrected['frame_count']
    for source in manifest['sources']+fixed['sources']:
        assert sha(Path(source['path'])) == source['sha256']
    old_folder, new_folder = OUTPUT/NAME, REPAIR/NAME
    backup = ROOT/'Saved/Codex/WizardPoolSuperseded'/NAME
    # Verify the absolute source and destination paths before moving folders.
    for folder in (old_folder,new_folder,backup):
        assert folder.resolve().is_relative_to(ROOT.resolve())
    assert old_folder.is_dir() and new_folder.is_dir() and not backup.exists()
    assert len(list(new_folder.glob('frame_*.png'))) == corrected['frame_count']
    backup.parent.mkdir(parents=True,exist_ok=True)
    (backup.parent/'RenderManifest_before_seam_fix.json').write_bytes(path.read_bytes())
    shutil.move(str(old_folder),str(backup))
    shutil.move(str(new_folder),str(old_folder))
    frames = [row for row in manifest['frames'] if row['clip']!=NAME]
    for record in fixed['frames']:
        record=dict(record,path=str(old_folder/Path(record['path']).name))
        assert sha(Path(record['path'])) == record['sha256']
        frames.append(record)
    manifest['clips'] = [corrected if row['name']==NAME else row for row in manifest['clips']]
    order = {row['name']:index for index,row in enumerate(manifest['clips'])}
    manifest['frames']=sorted(frames,key=lambda row:(order[row['clip']],row['index']))
    sources={row['path']:row for row in manifest['sources']+fixed['sources']}
    manifest['sources']=list(sources.values())
    manifest['corrected_clips']=[dict(name=NAME,render_manifest=str(fixed_path),sha256=sha(fixed_path),
        reason='Eight-frame lower-body recovery after source-pose hold; upper motion unchanged.')]
    manifest['final_review_blend']=fixed['sources'][0]
    manifest['rendered_png_count']=len(manifest['frames'])
    assert len(manifest['clips'])==32 and manifest['rendered_png_count']==7260
    temporary=path.with_suffix('.json.tmp')
    temporary.write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf8')
    temporary.replace(path)
    print('POOL_SEAM_RENDER_REPLACED',NAME,corrected['frame_count'],flush=True)


if __name__=='__main__':
    main()
