"""Exercise distinct old/new menu roots through Epic Slate MCP in solo PIE."""
import base64,json,re,sys,time
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from native_mcp_client import NativeMCP
c=NativeMCP();S='SlateInspectorToolset.SlateInspectorToolset'
out=Path('E:/CodexWork/MessControl/Validation/UIRootPSD_20261103');out.mkdir(parents=True,exist_ok=True)
def call(name,args=None):
    r=c.tool('call_tool',{'toolset_name':S,'tool_name':name,'arguments':args or {}})
    if r.get('isError'):raise RuntimeError(r)
    return json.loads(r['content'][0]['text'])['returnValue']
observer=call('Observe',{'ref':'','maxDepth':40})
roots=call('Snapshot',{'ref':'','maxDepth':40,'bIncludeSourceLocations':False})
windows=re.findall(r'^window .*NetMode: Standalone[^\n]*\[ref=(w\d+)\]',roots,re.M)
assert len(windows)==1
window=windows[0]
def snapshot():return call('Snapshot',{'ref':window,'maxDepth':40,'bIncludeSourceLocations':False})
def wait(check):
    end=time.monotonic()+25
    while time.monotonic()<end:
        text=snapshot()
        if check(text):return text
        time.sleep(.2)
    raise AssertionError('Menu state not reached')
def buttons(text):
    return [(name.replace(chr(92)+'r','').replace(chr(92)+'n',chr(10)).replace(chr(13),'').splitlines()[-1],ref) for name,ref in re.findall(r'button "([^"]+)"[^\n]*\[ref=(b\d+)\]',text)]
def button(text,label):
    matches=[ref for name,ref in buttons(text) if name==label];assert len(matches)==1,(label,matches)
    return matches[0]
def capture(name):
    text=snapshot();(out/(name+'.txt')).write_text(text,encoding='utf-8')
    picture=call('Screenshot',{'ref':window});(out/(name+'.png')).write_bytes(base64.b64decode(picture['data']))
    return text
labels=json.loads(Path(__file__).with_name('ui_v2_texts.json').read_text(encoding='utf-8'))
new={row[key] for row in labels if row['Name']=='UI.V2.Switch.ToNew' for key in ('RU','EN','DE','FR','ES','PT_BR','ZH_HANS','ZH_HANT','JA','KO')}
current={row[key] for row in labels if row['Name']=='UI.V2.Switch.ToCurrent' for key in ('RU','EN','DE','FR','ES','PT_BR','ZH_HANS','ZH_HANT','JA','KO')}
def selector(text):return next(((name,ref) for name,ref in buttons(text) if name in new|current),None)
report={'passed':False,'input':'Epic native Slate pointer/key route','scope':'Main menu only; HUD is Designer prototype'}
original=None
try:
    baseline=wait(lambda text:('Играть' in text or 'Play' in text) and '[focused]' in text)
    play='Играть' if 'button "Играть"' in baseline else 'Play'
    settings='Настройки' if 'button "Настройки"' in baseline else 'Settings'
    back='Назад' if settings=='Настройки' else 'Back'
    call('PressKey',{'key':'F3'});opened=wait(lambda text:selector(text) is not None)
    original=selector(opened)[0];assert original in new,'Start this check with Current selected'
    legacy_play=button(baseline,play)
    call('Click',{'ref':selector(opened)[1]});wait(lambda text:selector(text) and selector(text)[0] in current)
    capture('Menu_F3_New')
    call('PressKey',{'key':'Escape'});changed=wait(lambda text:not selector(text) and play in {name for name,ref in buttons(text)})
    assert button(changed,play)!=legacy_play,'New mode must use a different actual control'
    assert legacy_play not in {ref for name,ref in buttons(changed)},'Legacy menu must be absent from visible controls'
    capture('Menu_New_Runtime')
    call('Click',{'ref':button(changed,settings)})
    nested=wait(lambda text:'button "'+back+'"' in text and 'slider' in text)
    capture('Menu_New_SettingsRoute')
    call('Click',{'ref':button(nested,back)});returned=wait(lambda text:play in {name for name,ref in buttons(text)} and 'slider' not in text)
    assert button(returned,play)==button(changed,play),'Returning reuses the new root'
    call('PressKey',{'key':'F3'});panel=wait(lambda text:selector(text) is not None)
    call('Click',{'ref':selector(panel)[1]});wait(lambda text:selector(text) and selector(text)[0] in new)
    call('PressKey',{'key':'Escape'});restored=wait(lambda text:not selector(text) and legacy_play in {ref for name,ref in buttons(text)})
    assert '[focused]' in restored
    capture('Menu_Current_Restored')
    call('PressKey',{'key':'F3'});wait(lambda text:selector(text) is not None)
    call('PressKey',{'key':'F3'});wait(lambda text:not selector(text))
    report.update(passed=True,distinct_menu_controls=True,legacy_hidden=True,settings_forward_and_back=True,same_new_root_on_return=True,current_root_restored=True,focus_restored=True)
finally:
    if original:
        panel=selector(snapshot())
        if not report['passed'] and not panel:
            call('PressKey',{'key':'F3'});panel=selector(wait(lambda text:selector(text) is not None))
        if panel:
            if panel[0]!=original:call('Click',{'ref':panel[1]})
            call('PressKey',{'key':'Escape'})
    call('Unobserve',{'identifier':observer})
    (out/'MenuSoloReport.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(report,ensure_ascii=False))
