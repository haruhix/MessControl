"""Render each root fixture on a separate editor tick.
Wait for PreviewChecks.json status=complete before using the output as evidence.
No saved defaults, player language, or UI selection are changed.
"""
import builtins
import json
import traceback
from pathlib import Path
import unreal as u

assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
assert not getattr(builtins, 'mc_root_preview_job', None), 'A preview job is already active'
out=Path('E:/CodexWork/MessControl/Validation/UIRootPSD_20261103')
out.mkdir(parents=True,exist_ok=True)
cls=u.load_class(None,'/Game/Gameplay/UI/V2/WBP_UIRoot.WBP_UIRoot_C')
assert cls
cases=[
    ('Menu_RU','Menu',1920,1080,'ru',1),
    ('Menu_EN','Menu',1920,1080,'en',1),
    ('Menu_DE_720','Menu',1280,720,'de',1),
    ('Menu_RU_Ultrawide','Menu',2560,1080,'ru',1),
    ('Menu_JA_DPI150','Menu',1920,1080,'ja',1.5),
    ('Gameplay_RU_Prototype','Gameplay',1920,1080,'ru',1),
    ('Gameplay_DE_720','Gameplay',1280,720,'de',1),
    ('Gameplay_JA_720','Gameplay',1280,720,'ja',1),
    ('Gameplay_RU_Ultrawide','Gameplay',2560,1080,'ru',1),
]
job={'class':cls,'cases':cases,'version':u.MCGameplayUIFactory.get_presentation_version(),
     'culture':u.InternationalizationLibrary.get_current_language(),'frames':0,
     'report':{'root':'/Game/Gameplay/UI/V2/WBP_UIRoot','status':'running','renders':[],
               'renderScheduling':'one fixture per separate editor frame; same-frame batch exports were incomplete'}}
builtins.mc_root_preview_job=job

def write_report():
    (out/'PreviewChecks.json').write_text(json.dumps(job['report'],ensure_ascii=False,indent=2),encoding='utf-8')

def finish():
    u.unregister_slate_post_tick_callback(job['handle'])
    builtins.mc_root_preview_job=None

def render_next(delta):
    job['frames']+=1
    if job['frames']%3:
        return
    try:
        assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
        name,screen,width,height,language,scale=job['cases'].pop(0)
        file=out/(name+'.png')
        assert u.MCGameplayUIEditorLibrary.render_gameplay_ui_preview(
            job['class'],str(file),u.IntPoint(width,height),screen,language,0,scale,True,u.MCUIVersion.NEW)
        assert file.is_file() and file.stat().st_size
        job['report']['renders'].append({'file':str(file),'screen':screen,'language':language,
                                        'size':[width,height],'scale':scale})
        assert u.MCGameplayUIFactory.get_presentation_version()==job['version']
        assert u.InternationalizationLibrary.get_current_language()==job['culture']
        if not job['cases']:
            job['report']['status']='complete'
            job['report']['isolation']='Runtime UI selection and language unchanged'
            finish()
        write_report()
    except Exception:
        job['report']['status']='failed'
        job['report']['error']=traceback.format_exc()
        finish()
        write_report()
        u.log_error(job['report']['error'])

job['handle']=u.register_slate_post_tick_callback(render_next)
write_report()
print('UI_ROOT_PREVIEWS_STARTED; await PreviewChecks.json status=complete')
