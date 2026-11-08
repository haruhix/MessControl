"""Read-only capture of an ALREADY open selected native clothing row viewport.
Run after compile/registration and ordinary DataTable row selection. No editor
open, row selection, property edit, asset save, level or gameplay mutation.
Optional globals MC_CLOTHING_FIT_TABLE (object path), MC_CLOTHING_FIT_ROW,
MC_CLOTHING_FIT_OUTPUT (fresh E: directory). Camera preset/orbit is set by the
ordinary embedded preview controls before this probe; its current view is kept.
"""
import hashlib
import json
import traceback
from pathlib import Path
import unreal as u

OUT=Path(globals().get('MC_CLOTHING_FIT_OUTPUT',
    'E:/CodexWork/MessControl/Validation/ClothingCatalog_20261104/FitPreview/Take01')).resolve()
TABLE=globals().get('MC_CLOTHING_FIT_TABLE','/Game/Data/DT_CharacterCosmetics.DT_CharacterCosmetics')
ROW=globals().get('MC_CLOTHING_FIT_ROW','Hat_Head_CowboyHat')
EXPECTED_BODY=globals().get('MC_CLOTHING_FIT_BODY','/Game/Gameplay/CharacterCurrent/SK_CharacterGameplay.SK_CharacterGameplay')
assert OUT.drive.upper()=='E:' and not OUT.exists(), 'Fresh E: output only'
assert hasattr(u,'MCClothingFitEditorLibrary'), 'Approved Editor DLL with fitting customization required'
OUT.mkdir(parents=True)
report=dict(scope=__doc__,table=TABLE,row=ROW,passed=False,
    recipeSHA256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
def dirty():
    return sorted(x.get_path_name() for x in list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())
        +list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages()))
try:
    before_dirty=dirty()
    before=json.loads(u.MCClothingFitEditorLibrary.inspect_open_preview(TABLE,ROW))
    report['before']=before
    assert before.get('valid') and before.get('synchronized'), before
    assert before['bodyMesh']==EXPECTED_BODY, ('Body differs from saved actual runtime body evidence',before['bodyMesh'],EXPECTED_BODY)
    assert before['isolatedEditorPreviewWorld'] and before['attachedToActualBody'] and before['registered'], before
    dest=OUT/'FitPreview.png'
    assert u.MCClothingFitEditorLibrary.capture_open_preview(TABLE,ROW,str(dest)), 'Existing viewport capture failed'
    data=dest.read_bytes()
    assert len(data)>24 and data[:8]==b'\x89PNG\r\n\x1a\n'
    after=json.loads(u.MCClothingFitEditorLibrary.inspect_open_preview(TABLE,ROW))
    assert after['rowExport']==before['rowExport'] and after['tableDirty']==before['tableDirty'], 'Read-only probe changed row/dirty state'
    assert dirty()==before_dirty, 'Probe dirtied or cleaned editor packages'
    report.update(passed=True,after=after,image=dict(file=str(dest),sha256=hashlib.sha256(data).hexdigest(),
        width=int.from_bytes(data[16:20],'big'),height=int.from_bytes(data[20:24],'big')),dirtyPackages=before_dirty,
        limitations=['Reference pose only, static accessories; no skinned garment/cloth or live player animator claim.',
            'Inspect one real row/front or side at a time. Native undo/reimport/rename/delete and runtime equip require separate checks.',
            'Generated fitting images never overwrite product Icon or enter Git automatically.'])
except Exception:
    report['error']=traceback.format_exc()
    raise
finally:
    (OUT/'Report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
u.log('MC_CLOTHING_FIT_INSPECTION ' + str(report['passed']))
