"""Capture every actual canonical clothing row in its native Row Editor preview.
Owned idle Editor only. No row edits, imports or saves. Bounded Slate ticks.
Restores the expected initially selected Cowboy row and front view.
"""
import json,time,hashlib,traceback
from pathlib import Path
import unreal as u
OUT=Path(globals().get('MC_CLOTHING_FIT_ROWS_OUTPUT','E:/CodexWork/MessControl/Validation/Clothing_20261104/FitRowsBuild03')).resolve()
assert OUT.drive.upper()=='E:' and not OUT.exists()
assert '-MCUIFoundationOwned' in u.SystemLibrary.get_command_line()
assert not u.EditorLevelLibrary.get_pie_worlds(True)
TABLE='/Game/Data/DT_CharacterCosmetics.DT_CharacterCosmetics';ORIGINAL='Hat_Head_CowboyHat'
LIB=u.MCClothingFitEditorLibrary
initial=json.loads(LIB.inspect_open_preview(TABLE,ORIGINAL));assert initial.get('valid')
table=u.load_asset(TABLE)
rows=json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(table))
assert rows and all(r['bUseCanonicalCommerce'] for r in rows)
def dirty():return sorted(p.get_path_name() for p in list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages())+list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages()))
before_dirty=dirty();before_rows=rows
package=Path('E:/DEVGAME/MessControl/Content/Data/DT_CharacterCosmetics.uasset')
before_sha=hashlib.sha256(package.read_bytes()).hexdigest();OUT.mkdir(parents=True)
scenarios=[(r['Name'],0) for r in rows]+[(ORIGINAL,1),(ORIGINAL,2)]
state={'index':0,'phase':'open','age':0.,'began':time.monotonic(),'handle':None}
report={'passed':False,'scope':'Actual selected native DataTable row/reference-pose preview; no gameplay/animation claim','frames':[]}
def finish(error=None):
 try:
  LIB.open_table_row(table,ORIGINAL);LIB.set_preview_preset(TABLE,ORIGINAL,0)
  assert json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(table))==before_rows
  assert dirty()==before_dirty and hashlib.sha256(package.read_bytes()).hexdigest()==before_sha
  report['passed']=not error;report['sourceSHA256']=before_sha;report['dirtyAfter']=dirty()
  if error:report['error']=error
 except Exception:report['error']=traceback.format_exc();report['passed']=False
 finally:
  (OUT/'Report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
  if state['handle'] is not None:u.unregister_slate_post_tick_callback(state['handle'])
  u.log('MC_CLOTHING_FIT_ROWS '+str(report['passed']))
def tick(delta):
 try:
  assert not u.EditorLevelLibrary.get_pie_worlds(True)
  assert time.monotonic()-state['began']<90,'Bounded preview capture timed out'
  name,preset=scenarios[state['index']];state['age']+=delta
  if state['phase']=='open':
   assert LIB.open_table_row(table,name);state.update(phase='preset',age=0.)
  elif state['phase']=='preset' and state['age']>=.4:
   assert LIB.set_preview_preset(TABLE,name,preset);state.update(phase='capture',age=0.)
  elif state['phase']=='capture' and state['age']>=.4:
   info=json.loads(LIB.inspect_open_preview(TABLE,name))
   assert info.get('valid') and info.get('synchronized') and info.get('attachedToActualBody') and not info.get('iconMissing'),info
   assert info['bodyMesh']=='/Game/Gameplay/CharacterCurrent/SK_CharacterGameplay.SK_CharacterGameplay'
   file=OUT/(name+'_'+str(preset)+'.png');assert LIB.capture_open_preview(TABLE,name,str(file))
   report['frames'].append({'row':name,'preset':preset,'image':str(file),'inspection':info})
   state['index']+=1
   if state['index']==len(scenarios):finish();return
   state.update(phase='open',age=0.)
 except Exception:finish(traceback.format_exc())
state['handle']=u.register_slate_post_tick_callback(tick)
u.log('MC_CLOTHING_FIT_ROWS scheduled '+str(OUT))
