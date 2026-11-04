"""Prepare-only bounded native Slate Emotes check in an existing owned solo Hub PIE.
Run only when authorized: python -B .../check_unified_emotes_solo.py --pid PID --output E:/.../Emotes/Take01
No PIE start/stop, travel, pawn/profile/rules/colour mutators or direct emote RPC.
Begin uses the real reflected owner API; selection/hover/T release/F3 use native Slate.
"""
import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import re
import sys
import time

PROJECT = Path('E:/DEVGAME/MessControl')
TEMP = Path('E:/CodexTemp/MessControl-EmotesQA')
SLATE = 'SlateInspectorToolset.SlateInspectorToolset'
REMOTE = r"""
import json,os
from pathlib import Path
import unreal as u
assert os.getpid()==EXPECTED_PID, 'Wrong Editor PID'
assert '-mchubtransientprofile' in u.SystemLibrary.get_command_line().lower(), 'Requires transient profile'
worlds=u.EditorLevelLibrary.get_pie_worlds(True)
assert len(worlds)==1 and u.SystemLibrary.is_standalone(worlds[0]), 'One existing standalone PIE only'
world=worlds[0]
assert u.GameplayStatics.get_current_level_name(world,True)=='Hub'
assert not u.GameplayStatics.is_game_paused(world)
pc=u.GameplayStatics.get_player_controller(world,0)
hero=pc.get_controlled_pawn() # Controller.K2_GetPawn: ScriptName=GetControlledPawn
assert isinstance(hero,u.MCToothCharacter) and pc.is_world_presentation_ready()
ps=u.GameplayStatics.get_player_state(world,0)
hub=u.GameplayStatics.get_game_state(world)
assert isinstance(hub,u.MCHubGameState) and hub.get_participant_count()==1 and hub.get_editor_property('phase')==u.MCHubPhase.GATHERING
expression=hero.get_editor_property('expression')
assert expression is not None
sources=[x for x in u.ObjectIterator(u.MCEmoteWidget) if x.get_world()==world and x.get_owning_player()==pc and x.is_in_viewport()]
assert len(sources)==1, 'Original retained source missing/ambiguous'
source=sources[0] # Exact original typed owner, no unavailable native-only PC property alias.

def path(x): return x.get_path_name() if x is not None else None

def owner(cls):
    matches=[x for x in u.ObjectIterator(cls) if x.get_world()==world and x.get_owning_player()==pc]
    assert len(matches)==1,(str(cls),len(matches))
    return matches[0]
root=owner(u.MCUnifiedUIWidget)
view=owner(u.MCUnifiedEmoteWidget)

def tree(widget):
    for name in ('WidgetTree_0','WidgetTree'):
        item=u.find_object(widget,name)
        if isinstance(item,u.WidgetTree) and item.get_outer()==widget:return item
    found=[x for x in u.ObjectIterator(u.WidgetTree) if x.get_outer()==widget]
    assert len(found)==1, ('Owned tree missing',path(widget))
    return found[0]

def find(widget,name,required=True):
    owned=tree(widget);item=u.find_object(owned,name)
    if required:assert isinstance(item,u.Widget) and item.get_outer()==owned,name
    return item

def text(widget):return str(widget.get_text()) if widget is not None else ''

def field(data,key):
    found=[v for k,v in data.items() if rekey(k)==rekey(key)]
    assert len(found)==1,('Missing reflected field',key,list(data))
    return found[0]
def rekey(value):return str(value).replace('_','').lower()

def snapshot():
    telemetry=json.loads(u.MCGameplayUIEditorLibrary.inspect_pie_emote_presentation(world))
    assert telemetry.get('valid'),telemetry
    for key,obj in [('world',world),('controller',pc),('pawn',hero),('source',source),('sourceTree',tree(source)),('expression',expression)]:
        assert telemetry[key]==path(obj),('Readonly helper owner mismatch',key)
    category=int(telemetry['category'])
    choices=[]
    for index,button in enumerate(find(source,'EmoteItems').get_all_children()):
        caption=button.get_content();frame=find(view,'NewEmoteChoice'+str(index),False)
        proxy=find(view,'NewEmoteChoiceButton'+str(index),False)
        item=dict(index=index,sourceButton=path(button),sourceText=text(caption),sourceEnabled=button.get_is_enabled(),
            newText=text(find(view,'NewEmoteChoiceLabel'+str(index),False)),newSelected=bool(frame.get_editor_property('selected')) if frame else None,
            newEnabled=proxy.get_is_enabled() if proxy else False,newVisible=proxy.is_visible() if proxy else False)
        assert index<len(telemetry['choices'])
        native=telemetry['choices'][index]
        assert native['index']==index and native['sourceButton']==path(button) and native['label']==item['sourceText'], 'Actual source membership mismatch'
        item['id']=native['id'] if category==0 and native['alarm']==0 else None
        item['sourceSelected']=native['selected']
        choices.append(item)
    colour=ps.get_editor_property('player_color')
    return dict(world=path(world),controller=path(pc),pawn=path(hero),playerState=path(ps),source=path(source),sourceTree=path(tree(source)),
        root=path(root),rootTree=path(tree(root)),view=path(view),viewTree=path(tree(view)),expression=path(expression),
        version=str(u.MCGameplayUIFactory.get_presentation_version()),rootScreen=find(root,'InterfaceScreens').get_active_widget_index(),
        held=pc.is_holding_emote_selection(),moveIgnored=pc.is_move_input_ignored(),sourceVisible=source.is_visible(),sourceInnerVisibility=str((find(source,'EmoteViewport',False) or find(source,'EmoteScale',False) or find(source,'EmoteRoot')).get_visibility()),
        viewVisible=view.is_visible(),rootVisible=root.is_visible(),newFocus=view.has_user_focus(pc) or view.has_user_focused_descendants(pc),
        sourceFocus=source.has_user_focus(pc) or source.has_user_focused_descendants(pc),category=category,
        sourceHeading=text(find(source,'EmoteCategoryTitle')),newHeading=text(find(view,'NewEmoteHeading')),
        choices=choices,emote=telemetry['emote'],selectedChoice=telemetry['selectedChoice'],chosenId=telemetry['chosenId'],colour=[colour.r,colour.g,colour.b,colour.a],
        alarm=str(ps.get_editor_property('alarm')),alarmUntil=float(ps.get_editor_property('alarm_until')))
"""

class SkipCheck(RuntimeError): pass

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid',type=int,required=True)
    parser.add_argument('--output',required=True,help='Fresh output directory on E:')
    args=parser.parse_args();out=Path(args.output).resolve()
    assert out.drive.upper()=='E:' and not out.exists(), 'Fresh E: output only'
    out.mkdir(parents=True);TEMP.mkdir(parents=True,exist_ok=True)
    os.environ['TEMP']=os.environ['TMP']=str(TEMP);os.environ['PYTHONDONTWRITEBYTECODE']='1';sys.dont_write_bytecode=True
    sys.path.insert(0,str(PROJECT/'Tools/Unreal'))
    sys.path.insert(0,'E:/UE/UE_5.8/Engine/Plugins/Experimental/PythonScriptPlugin/Content/Python')
    from native_mcp_client import NativeMCP
    from record_unified_catch_solo import owned_window,process_identity
    import remote_execution as remote
    report=dict(status='RUNNING',passed=False,pid=args.pid,startedUTC=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()),
        input='Owner Begin/End UFUNCTION + native Slate Windows/Hover/PressKey; not hardware input',steps=[],cleanup={},
        scope='One transient-profile Hub player; at most one accepted face emote; no colour/alarm/gesture or gameplay changes')
    session=remote.RemoteExecution();client=None;observer=None;baseline=None;started=False;window=None
    state_file=out/'CurrentState.json'
    def save(): (out/'Report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    def statement(extra='',read=True):
        scope={'__name__':'__main__','__file__':str(Path(__file__).resolve()),'EXPECTED_PID':args.pid}
        code=REMOTE+'\n'+extra
        if read:code+='\nPath('+repr(str(state_file))+').write_text(json.dumps(snapshot(),ensure_ascii=False,indent=2),encoding="utf-8")\n'
        session.run_command('exec('+repr(code)+','+repr(scope)+')',exec_mode=remote.MODE_EXEC_STATEMENT,unattended=True,raise_on_failure=True)
        return json.loads(state_file.read_text(encoding='utf-8')) if read else None
    def call(name,**arguments):
        value=client.tool('call_tool',{'toolset_name':SLATE,'tool_name':name,'arguments':arguments})
        assert not value.get('isError'),value
        payload=json.loads(value['content'][0]['text']);return payload['returnValue']
    def slate():return call('Snapshot',ref=window,maxDepth=50,bIncludeSourceLocations=False)
    def stable(state):
        if baseline:
            for key in ('world','controller','pawn','playerState','source','sourceTree','root','rootTree','view','viewTree','expression'):
                assert state[key]==baseline[key],('Retained identity changed',key)
    def wait(check,label,timeout=6):
        deadline=time.monotonic()+timeout;last=None
        while time.monotonic()<deadline:
            last=statement();stable(last)
            if check(last):return last
            time.sleep(.1)
        raise AssertionError((label,last))
    def serial(state):return state['emote']['serial']
    def unchanged(state,reference):
        assert serial(state)==serial(reference) and state['colour']==baseline['colour']
        assert (state['alarm'],state['alarmUntil'])==(baseline['alarm'],baseline['alarmUntil'])
    def capture(label,state):
        (out/(label+'.json')).write_text(json.dumps(state,ensure_ascii=False,indent=2),encoding='utf-8')
        (out/(label+'.txt')).write_text(slate(),encoding='utf-8')
        result=call('Screenshot',ref=window);(out/(label+'.png')).write_bytes(base64.b64decode(result['data']))
        report['steps'].append(dict(name=label,state=state));save()
    def neutral_cursor():
        # Native Slate geometry is desktop-space. Pick a real upper-left Hub leaf outside the future right-hand choice list.
        snap=slate();lines=snap.splitlines();m=re.search(r'\[pos=(-?\d+),(-?\d+) size=(\d+),(\d+)\]',lines[0])
        assert m, 'Window native geometry missing'
        wx,wy,ww,wh=map(float,m.groups());candidates=[]
        for line in lines[1:]:
            pos=re.search(r'\[pos=(-?\d+),(-?\d+) size=(\d+),(\d+)\].*\[ref=([^\]]+)\]',line)
            if not pos:continue
            x,y,w,h=map(float,pos.groups()[:4]);ref=pos.group(5)
            if w>0 and h>0 and wx+8<x+w*.5<wx+ww*.25 and wy+24<y+h*.5<wy+wh*.2:candidates.append((y+h*.5,x+w*.5,ref,line))
        if not candidates:raise SkipCheck('No verified neutral Hub Slate leaf; no held selection opened')
        picked=min(candidates);assert call('Hover',ref=picked[2]);report['neutralCursor']=picked[3]
    def begin():
        neutral_cursor();initial=statement('pc.begin_emote_selection()');
        if not initial['held']:raise SkipCheck('Original native owner did not admit held Emotes')
        time.sleep(.55);state=statement();stable(state)
        if not state['held'] or not state['sourceVisible']:
            raise SkipCheck('Native held owner cancelled after genuine window select; application-active gate not established')
        state=wait(lambda x:x['rootScreen']==18 and x['viewVisible'] and x['newFocus'],'New Emotes/focus')
        assert state['selectedChoice']==-1 and not any(x['newSelected'] for x in state['choices']), 'Initial cursor selected a choice; cancel without release'
        return state
    try:
        report['process']=process_identity(args.pid);owned=owned_window(args.pid);report['ownedWindow']=owned
        report['sourceHashes']={name:hashlib.sha256((PROJECT/name).read_bytes()).hexdigest() for name in (
            'Source/MessControl/Private/MCEmoteWidget.cpp','Source/MessControl/Private/MCUnifiedEmoteWidget.cpp','Source/MessControl/Private/MCPlayerController.cpp','Source/MessControlEditor/Private/MCGameplayUIEditorLibrary.cpp',
            'Tools/Unreal/UI/check_unified_emotes_solo.py','Tools/Unreal/native_mcp_client.py','Tools/Unreal/UI/record_unified_catch_solo.py')}
        session.start();deadline=time.monotonic()+30;nodes=[]
        while time.monotonic()<deadline:
            nodes=[n for n in session.remote_nodes if n.get('project_name')=='MessControl']
            if len(nodes)==1:break
            time.sleep(.2)
        assert len(nodes)==1,'One MessControl remote Editor required'
        session.open_command_connection(nodes[0]['node_id'])
        baseline=statement();assert baseline['rootScreen']==2 and not baseline['held'] and not baseline['sourceVisible'] and not baseline['moveIgnored']
        assert 'NEW' in baseline['version'].upper();report['baseline']=baseline
        client=NativeMCP()
        listed=json.loads(call('Windows',action='list',index=-1));matches=[w for w in listed if w['title']==owned['title'] and 'NetMode: Standalone' in w['title']]
        if len(matches)!=1:raise SkipCheck('Cannot uniquely match native Slate window to explicit owned HWND/PID')
        assert call('Windows',action='select',index=matches[0]['index'])=='OK'
        observer=call('Observe',ref='',maxDepth=50);roots=call('Snapshot',ref='',maxDepth=50,bIncludeSourceLocations=False)
        rows=[(title,ref) for title,ref in re.findall(r'^window "([^"]+)".*\[ref=(w\d+)\]',roots,re.M) if title==owned['title']]
        assert len(rows)==1;window=rows[0][1];started=True
        first=begin();unchanged(first,baseline);capture('01_HeldNoChoice',first)
        assert call('PressKey',key='T') # Down is a no-op while held, native KeyUp routes original End.
        released=wait(lambda x:not x['held'] and not x['sourceVisible'],'No-choice release');unchanged(released,baseline)
        statement('pc.end_emote_selection()');unchanged(statement(),baseline);capture('02_NoChoiceReleased',released)
        opened=begin()
        for key,category in (('Two',1),('Three',2),('One',0)):
            assert call('PressKey',key=key)
            current=wait(lambda x:x['category']==category and x['sourceHeading']==x['newHeading'],'Category '+key);unchanged(current,baseline)
        # Cosmetic version switch, save=False, preserves original source/tree/held state; physical F3 is a separate cancellation case.
        statement('assert u.MCGameplayUIFactory.set_presentation_version(u.MCUIVersion.CURRENT,False)')
        current=wait(lambda x:x['held'] and x['sourceVisible'] and x['sourceFocus'],'Current source focus');unchanged(current,baseline)
        statement('assert u.MCGameplayUIFactory.set_presentation_version(u.MCUIVersion.NEW,False)')
        current=wait(lambda x:x['held'] and x['rootScreen']==18 and x['newFocus'],'New source continuity');unchanged(current,baseline);capture('03_CategoriesContinuity',current)
        assert call('PressKey',key='Escape');wait(lambda x:not x['held'] and not x['sourceVisible'],'Category cancel')
        before=begin();assert before['category']==0
        candidates=[x for x in before['choices'] if x.get('id') and x['sourceEnabled'] and x['newEnabled'] and x['newVisible'] and x['sourceText']==x['newText']]
        assert candidates,'No enabled source-backed face choice'
        candidates=[c for c in candidates if sum(x['newText']==c['newText'] for x in before['choices'])==1]
        assert candidates,'No uniquely labelled enabled face choice'
        choice=candidates[0];snap=slate()
        matches=[]
        for line in snap.splitlines():
            match=re.match(r'^(\s*)(\w+) "'+re.escape(choice['newText'])+r'"[^\n]*\[ref=([^\]]+)\]',line)
            if match:matches.append((len(match.group(1)),match.group(2),match.group(3),line))
        assert matches,('Native target inaccessible',choice['newText'])
        buttons=[m for m in matches if m[1]=='button']
        target=min(buttons) if buttons else max(matches) # Outer proxy or real text centre; nested Panel surfaces can share its label.
        report['hoverTarget']=target[3]
        assert call('Hover',ref=target[2]);hovered=wait(lambda x:any(c['index']==choice['index'] and c['newSelected'] for c in x['choices']),'Real hover selection')
        unchanged(hovered,baseline);capture('04_HoverNoCommit',hovered)
        assert call('PressKey',key='T')
        accepted=wait(lambda x:not x['held'] and not x['sourceVisible'] and serial(x)==((serial(baseline)+1)&65535),'One accepted real face emote')
        assert accepted['emote']['id']==choice['id'];statement('pc.end_emote_selection()');time.sleep(.2)
        after=statement();assert serial(after)==serial(accepted),'Duplicate End emitted twice';capture('05_ReleasedOnce',after)
        f3begin=begin();assert call('PressKey',key='F3')
        cancelled=wait(lambda x:not x['held'] and not x['sourceVisible'],'Existing F3 cancellation')
        assert serial(cancelled)==serial(accepted)
        statement('pc.end_emote_selection()');assert serial(statement())==serial(accepted)
        assert call('PressKey',key='F3');final=wait(lambda x:x['rootScreen']==2 and not x['held'] and not x['sourceVisible'],'Hub restored')
        assert final['colour']==baseline['colour'] and (final['alarm'],final['alarmUntil'])==(baseline['alarm'],baseline['alarmUntil'])
        capture('06_F3CancelledHub',final);report.update(status='PASS',passed=True,acceptedId=choice['id'],acceptedSerial=serial(accepted))
    except SkipCheck as error:report.update(status='SKIP',reason=str(error))
    except Exception as error:report.update(status='FAIL',error=repr(error))
    finally:
        if started:
            try:
                rows=json.loads((Path(__file__).with_name('ui_v2_texts.json')).read_text(encoding='utf-8'))
                switch_labels={value for row in rows if row['Name'] in ('UI.V2.Switch.ToNew','UI.V2.Switch.ToCurrent') for key,value in row.items() if key not in ('Name','Comment') and isinstance(value,str) and value}
                visible=re.findall(r'button "([^"\n]+)"[^\n]*\[ref=(b\d+)\]',slate())
                if sum(label in switch_labels for label,ref in visible)==1:
                    assert call('PressKey',key='F3')
                    report['cleanup']['f3Closed']=True
                statement('pc.close_emotes(False)\nu.MCGameplayUIFactory.set_presentation_version(u.MCUIVersion.NEW,False)',read=False)
                report['cleanup']['source']=statement();report['cleanup']['heldReleased']=not report['cleanup']['source']['held']
                assert report['cleanup']['heldReleased'] and 'NEW' in report['cleanup']['source']['version'].upper(), 'Cleanup did not release held UI/restore version'
                report['cleanup']['note']='Actual accepted face state/serial is retained to expire naturally; no fabricated state restore.'
            except Exception as error:report['cleanup']['error']=repr(error);report.update(status='FAIL',passed=False)
        if observer is not None:
            try:call('Unobserve',identifier=observer)
            except Exception as error:report['cleanup']['observerError']=repr(error)
        session.stop();report['finishedUTC']=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime());save()
    print(json.dumps({'status':report['status'],'passed':report['passed'],'report':str(out/'Report.json')},ensure_ascii=False),flush=True)
    return 0 if report['status'] in ('PASS','SKIP') else 1

if __name__=='__main__':
    raise SystemExit(main())
