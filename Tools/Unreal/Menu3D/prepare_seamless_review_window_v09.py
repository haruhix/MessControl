"""Session-only 1280x720 PIE capture window; restore after the bounded review."""
import json
from pathlib import Path
import unreal as u

assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
root=Path(u.Paths.project_dir())
request=json.loads((root/'Tools/Unreal/Menu3D/Evidence/review_window_request.json').read_text(encoding='utf8'))
record_name=request.get('record_name','v09_review_window_session')
assert Path(record_name).name==record_name and record_name.startswith('v09_review_window_')
receipt=root/('Tools/Unreal/Menu3D/Evidence/'+record_name+'.json')
settings=u.get_default_object(u.load_class(None,'/Script/UnrealEd.LevelEditorPlaySettings'))
fields=('NewWindowWidth','NewWindowHeight','CenterNewWindow')
if request['action']=='prepare':
    assert not receipt.exists()
    previous={name:settings.get_editor_property(name) for name in fields}
    receipt.write_text(json.dumps({'previous':previous,'restored':False},indent=2),encoding='utf8')
    for name,value in zip(fields,(1280,720,True)):
        settings.set_editor_property(name,value)
elif request['action']=='restore':
    record=json.loads(receipt.read_text(encoding='utf8'))
    assert not record['restored']
    for name,value in record['previous'].items():
        settings.set_editor_property(name,value)
    record['restored']=True
    receipt.write_text(json.dumps(record,indent=2),encoding='utf8')
else:
    raise RuntimeError(request['action'])
u.log('MC_REVIEW_WINDOW_SESSION_'+request['action'].upper())
