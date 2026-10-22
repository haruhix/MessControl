"""Render deterministic actual UMG frames, one per Slate callback, without PIE.

These are the saved runtime widget/material sampled at 60 fps, not a movie of
an editor window. Does not save any package or modify the live viewport.
"""
import builtins
import json
import time
from pathlib import Path
import unreal as u

OUT=Path(globals().get('MC_BRANCH9_RENDER_OUTPUT','E:/CodexWork/Branch9CleanIntro_20261022/UMG60'))
KEY='mc_branch9_clean_render'
assert OUT.drive.upper()=='E:' and (not OUT.exists() or not any(OUT.iterdir()))
assert not getattr(builtins,KEY,None)
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages()
assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages()
OUT.mkdir(parents=True,exist_ok=True)

class Render:
 def __init__(self):
  self.cls=u.load_class(None,'/Game/Gameplay/UI/WBP_StudioSplash.WBP_StudioSplash_C')
  self.index=0;self.started=time.monotonic()
  self.report={'status':'rendering','fps':60,'duration':3,'resolution':[1920,1080],
   'source':'Actual saved WBP_StudioSplash and M_Branch9_Kinetic through FWidgetRenderer, deterministic timeline samples; not realtime PIE footage',
   'frames':[]}
  self.callback=u.register_slate_post_tick_callback(self.tick)
 def finish(self,error=None):
  self.report['status']='failed' if error else 'complete'
  self.report['error']=str(error) if error else None
  self.report['wall_seconds']=time.monotonic()-self.started
  (OUT/'Render.json').write_text(json.dumps(self.report,indent=2),encoding='utf8')
  u.unregister_slate_post_tick_callback(self.callback)
  delattr(builtins,KEY)
  u.log('BRANCH9_RENDER_DONE '+self.report['status'])
 def tick(self,delta):
  try:
   if self.index>=180:
    # Aspect checks use the same authoritative widget; no alternate projection.
    for w,h in [(1024,768),(2560,1080)]:
     target=OUT/('Aspect_%dx%d.png'%(w,h))
     assert u.MCGameplayUIEditorLibrary.render_gameplay_ui_preview(self.cls,str(target),u.IntPoint(w,h),'Default','en',1.60)
    self.finish();return
   path=OUT/('Frame_%04d.png'%self.index)
   t=self.index/60.
   assert u.MCGameplayUIEditorLibrary.render_gameplay_ui_preview(self.cls,str(path),u.IntPoint(1920,1080),'Default','en',t)
   self.report['frames'].append({'frame':self.index,'time':t,'file':str(path)})
   self.index+=1
   if self.index%30==0:
    (OUT/'Render.json').write_text(json.dumps(self.report,indent=2),encoding='utf8')
    u.log('BRANCH9_RENDER_PROGRESS '+str(self.index)+'/180')
  except Exception as error:self.finish(error)

setattr(builtins,KEY,Render())
