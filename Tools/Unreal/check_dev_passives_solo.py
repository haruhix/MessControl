"""Owned L_Mouth solo proof of the real F3 -> passive page -> grant/remove path.

Run in the idle current Editor on saved L_Mouth after the
matching native build and saved current tables. No Computer Use, network, microphone,
package/profile/source edits or fabricated grants. The existing native 'level'
review action is the disclosed director-stop/baseline fixture. No reward card
is selected by this harness; unexpected reward UI is a bounded failure.

F3 entry uses the real BlueprintCallable PlayerController.ToggleDevPanel;
page/grant/remove/back use semantic SlateInspector Click refs from fresh Slate
snapshots. F3 closing uses SlateInspector.PressKey('F3'). The normal HUD is
intentionally hidden while F3 has its cursor, so its chip/value is checked only
after closing F3, after both grant and removal.
Public TextBlock.GetText, WidgetSwitcher.GetActiveWidgetIndex, GetMaxStamina and
perk query APIs prove results; no protected widget state or QA-only accessor.
Cancel: builtins.mc_f3_passives_solo.finish('cancelled explicitly').
"""
import builtins
import hashlib
import json
import re
import runpy
import time
import traceback
from pathlib import Path

import unreal as u

ROOT = Path(u.Paths.project_dir()).resolve()
OUT = Path(globals().get('MC_F3_PASSIVES_OUTPUT',
    'E:/CodexWork/MessControl/PassivesRemaining_20261028/F3CurrentA')).resolve()
KEY = 'mc_f3_passives_solo'
IDS = ('B1_HeavyHit', 'B1_LightFoot', 'B1_DeepBreath', 'B1_BigPortion',
       'B1_ConfidentThrow', 'B1_FreshDelivery', 'B1_GreedyHands',
       'B1_WideBristles', 'B1_UnderPressure') + tuple(
           spec['ID'] for spec in json.loads((ROOT/'Tools/Unreal/UI/passive_event_specs.json').read_text(encoding='utf8')))
TARGET = 'B1_DeepBreath'
assert ROOT.as_posix().lower() == 'e:/devgame/messcontrol'
assert OUT.drive.upper() == 'E:'
assert not (OUT/'Report.json').exists(), 'Preserve evidence; provide a fresh MC_F3_PASSIVES_OUTPUT'
assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages(), 'Preserve unsaved maps'
assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages(), 'Preserve unsaved packages'
previous = getattr(builtins, KEY, None)
assert previous is None or getattr(previous, 'done', True), 'F3 probe is already running'
support = runpy.run_path(str(ROOT/'Tools/Unreal/check_approved_environment_solo.py'),
    run_name='f3_passives_support', init_globals={'MC_ENV_OUTPUT': OUT, 'MC_ENV_CAPTURE_HZ': 0})
Base, prop = support['ApprovedEnvironmentSolo'], support['prop']


def outer_contains(obj, owner):
    for _ in range(32):
        if obj is None:
            return False
        if obj == owner:
            return True
        obj = obj.get_outer()
    return False


class F3PassivesSolo(Base):
    def __init__(self):
        super().__init__()
        self.report.update(scope=__doc__, checks={}, stages=[], input_requests=[],
            recipe_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            api_evidence=[
                'Installed UE5.8 SlateInspectorToolset.h: Observe/Snapshot/Click/PressKey',
                'SlateInspectorToolsetSnapshotRenderer.cpp: button [pos=x,y size=w,h] [ref=bN]',
                'SlateBlueprintLibrary.h: GetLocalSize and LocalToAbsolute',
                'MCPlayerController.h: BlueprintCallable ToggleDevPanel',
                'MCDevPanelPassives.cpp: stable named rows and normal RequestDevAction handlers'],
            limitations=[
                'One standalone authority player; no multiplayer/client-only access or microphone check.',
                'Disclosed native director-stop/reset review fixture, not an earned run.',
                'ToggleDevPanel tests the real F3-bound handler; hardware F3 opening is not asserted.',
                'Only DeepBreath is granted/removed here; all current page rows are inspected.',
                'Screenshots are exact PIE SWindow captures; their visual quality needs human/agent review.'])
        self.report.pop('fixture_tuning', None)
        self.report.pop('tuning_reason', None)
        self.capture_hz = max(0., min(6., float(globals().get('MC_F3_CAPTURE_HZ', 5))))
        self.busy = False
        self.mode_at = self.record_at = 0.
        self.ps = self.perks = None
        cls = u.load_class(None, '/Script/SlateInspectorToolset.SlateInspectorToolset')
        self.require(cls is not None, 'Installed SlateInspectorToolset is required')
        self.slate = u.get_default_object(cls)

    def write(self):
        self.output.mkdir(parents=True, exist_ok=True)
        (self.output/'Report.json').write_text(json.dumps(self.report, ensure_ascii=False,
            indent=2, allow_nan=False), encoding='utf8')

    def check(self, name, condition):
        self.report['checks'][name] = bool(condition)
        self.require(condition, name)

    def enter(self, phase):
        self.phase = phase
        self.mode_at = time.monotonic()-self.started
        self.report['stages'].append(dict(name=phase, wall_seconds=self.mode_at,
            game_seconds=float(u.GameplayStatics.get_time_seconds(self.world))))
        self.write()

    def panels(self):
        return [widget for widget in u.ObjectIterator(u.MCDevPanelWidget)
            if widget.get_owning_player() == self.pc and widget.is_in_viewport()
            and widget.get_visibility() not in (u.SlateVisibility.HIDDEN, u.SlateVisibility.COLLAPSED)]

    def panel(self):
        panels = self.panels()
        self.require(len(panels) == 1, 'Exactly one visible player-owned F3 panel')
        return panels[0]

    def control(self, name, cls=u.Widget):
        owner = self.panel()
        matches = [widget for widget in u.ObjectIterator(cls)
            if widget.get_name() == name and outer_contains(widget, owner)]
        self.require(len(matches) == 1, 'One actual named control: '+name)
        return matches[0]

    def text(self, name):
        return str(self.control(name, u.TextBlock).get_text())

    def page_index(self):
        return int(self.control('DevPanelPages', u.WidgetSwitcher).get_active_widget_index())

    def request_f3_handler(self):
        self.pc.toggle_dev_panel()
        self.report['input_requests'].append(dict(method='MCPlayerController.ToggleDevPanel',
            meaning='Real F3-bound handler; no direct widget creation', phase=self.phase))

    def press_f3(self):
        self.require(bool(self.slate.call_method('PressKey', args=('F3',))), 'Slate F3 dispatch succeeded')
        self.report['input_requests'].append(dict(method='SlateInspectorToolset.PressKey', key='F3', phase=self.phase))

    def click(self, name):
        # Public UButton geometry was empty in the Python wrapper even while
        # the actual Slate button was painted. Resolve its fresh semantic label,
        # with the target's unique description anchoring repeated row actions.
        button=self.control(name,u.Button)
        self.require(button.get_is_enabled(),'Real button is enabled: '+name)
        label=str(button.get_content().get_text())
        snapshot=str(self.slate.call_method('Snapshot',args=('',64,True)))
        directory=self.output/'slate';directory.mkdir(parents=True,exist_ok=True)
        destination=directory/('%02d_%s.txt'%(len(self.report['input_requests']),name))
        destination.write_text(snapshot,encoding='utf8')
        lines=snapshot.splitlines()
        normalize=lambda text: ' '.join(text.split())
        anchor=None
        if name.startswith(('PassiveGrant_','PassiveRemove_')):
            description=self.text('PassiveDescription_'+TARGET)
            anchors=[i for i,line in enumerate(lines) if normalize(description) in normalize(line)]
            self.require(len(anchors)==1,'Unique visible description anchor for '+TARGET+': '+repr(anchors))
            anchor=anchors[0]
            others=[self.text('PassiveDescription_'+ident) for ident in IDS if ident!=TARGET]
            end=min([i for i,line in enumerate(lines) if i>anchor and any(normalize(text) in normalize(line) for text in others)]+[len(lines)])
            lines=lines[anchor+1:end]
        matches=[]
        for line in lines:
            if not line.lstrip().startswith('button '):continue
            caption=re.search(r'button "(.*?)"',line)
            ref=re.search(r'\[ref=(b\d+)\]',line)
            if caption and ref and normalize(caption.group(1))==normalize(label):
                matches.append((ref.group(1),line))
        self.require(len(matches)==1,'Unique fresh semantic Slate ref for '+name+': '+repr(matches))
        ref,line=matches[0]
        self.require(bool(self.slate.call_method('Click',args=(ref,'left',False,u.SlateInspectorToolsetModifierKeys()))),'Real Slate click succeeded: '+name)
        self.report['input_requests'].append(dict(method='SlateInspectorToolset.Click',control=name,ref=ref,semantic_label=label,description_anchor=anchor,snapshot=str(destination),snapshot_line=line,phase=self.phase))

    def input_snapshot(self):
        loading = [widget for widget in u.ObjectIterator(u.MCLoadingWidget)
            if widget.get_owning_player() == self.pc and widget.is_in_viewport()
            and widget.get_visibility() not in (u.SlateVisibility.HIDDEN, u.SlateVisibility.COLLAPSED)]
        return dict(world_ready=bool(self.pc.is_world_presentation_ready()),
            reward_menu_open=bool(self.pc.is_reward_menu_open()), loading_visible=bool(loading),
            mouse_cursor=bool(prop(self.pc, 'show_mouse_cursor')),
            move_ignored=bool(self.pc.is_move_input_ignored()), look_ignored=bool(self.pc.is_look_input_ignored()),
            boss_intro=bool(self.pc.is_boss_intro_playing()),
            game_seconds=float(u.GameplayStatics.get_time_seconds(self.world)))

    def owned_ids(self):
        return [str(value) for value in prop(self.perks, 'passive_perk_ids')]

    def stamina_text(self):
        widgets = [widget for widget in u.ObjectIterator(u.MCStaminaWidget)
            if widget.get_owning_player() == self.pc and widget.get_name() == 'StaminaWidget'
            and widget.get_parent() is not None]
        self.require(len(widgets) == 1, 'One attached real stamina HUD widget')
        labels = [label for label in u.ObjectIterator(u.TextBlock)
            if label.get_name() == 'StaminaValue' and outer_contains(label, widgets[0])
            and label.get_parent() is not None]
        self.require(len(labels) == 1, 'One real stamina HUD value label')
        return str(labels[0].get_text())

    def passive_hud_texts(self):
        widgets = [widget for widget in u.ObjectIterator(u.MCSkillLoadoutWidget)
            if widget.get_owning_player() == self.pc and widget.get_parent() is not None]
        self.require(len(widgets) == 1, 'One attached real skill-loadout HUD')
        result = []
        for label in u.ObjectIterator(u.TextBlock):
            if not outer_contains(label, widgets[0]):
                continue
            parent = label.get_parent();found_wrap = found_scroll = False
            visible = True
            for _ in range(32):
                if parent is None:
                    break
                found_wrap |= isinstance(parent, u.WrapBox)
                found_scroll |= isinstance(parent, u.ScrollBox)
                visible &= parent.get_visibility() not in (u.SlateVisibility.HIDDEN, u.SlateVisibility.COLLAPSED)
                parent = parent.get_parent()
            if found_wrap and found_scroll and visible:
                result.append(str(label.get_text()))
        return result

    def row_snapshot(self):
        return dict(name=self.text('PassiveName_'+TARGET), description=self.text('PassiveDescription_'+TARGET),
            owned=self.text('PassiveOwned_'+TARGET), summary=self.text('PassiveSummary'),
            grant_enabled=bool(self.control('PassiveGrant_'+TARGET, u.Button).get_is_enabled()),
            remove_enabled=bool(self.control('PassiveRemove_'+TARGET, u.Button).get_is_enabled()),
            max_stamina=float(self.hero.get_max_stamina()), owned_ids=self.owned_ids())

    def hud_snapshot(self):
        # MCGameplayHUD::RefreshSkillLoadout intentionally hides/does not
        # refresh the loadout while bShowMouseCursor is true (the F3 panel).
        self.require(not self.panels(), 'HUD observation is outside the dev panel')
        return dict(max_stamina=float(self.hero.get_max_stamina()),
            stamina_hud=self.stamina_text(), passive_hud=self.passive_hud_texts(),
            owned_ids=self.owned_ids(), mouse_cursor=bool(prop(self.pc,'show_mouse_cursor')))

    def tick(self, _delta):
        if self.done or self.busy:
            return
        self.busy = True
        try:
            wall = time.monotonic()-self.started
            self.require(wall<150, 'Bounded 150-second F3 solo probe')
            worlds = list(u.EditorLevelLibrary.get_pie_worlds(True))
            if self.world is None:
                if not worlds:
                    return
                self.require(len(worlds)==1 and u.SystemLibrary.is_server(worlds[0]), 'Exactly one owned standalone authority PIE')
                pc = u.GameplayStatics.get_player_controller(worlds[0], 0)
                hero = pc.get_controlled_pawn() if pc else None
                if hero is None:
                    return
                self.require(isinstance(hero, u.MCToothCharacter), 'Real gameplay tooth pawn')
                self.world, self.pc, self.hero = worlds[0], pc, hero
                self.engine.set_editor_property('bUseFixedFrameRate', False)
                self.report['world'] = self.world.get_path_name()
                self.enter('readiness');return
            self.require(len(worlds)==1 and self.owned_pie_is_active(worlds), 'Owned PIE remains active and unreplaced')
            age = wall-self.mode_at
            if self.phase == 'readiness':
                self.require(age<60, 'Presentation readiness is bounded')
                if not self.pc.is_world_presentation_ready() or age<2:
                    return
                self.report['fixture_result'] = u.MCRoguelikeEditorLibrary.review_rogue_gameplay(self.world, 'level')
                self.require(not self.report['fixture_result'].startswith('REJECT'), 'Native level review fixture accepted')
                self.ps = u.GameplayStatics.get_player_state(self.world, 0)
                self.perks = self.ps.get_component_by_class(u.MCPerkComponent)
                self.require(self.perks is not None, 'Real player-owned perk component')
                self.enter('arrival_release');return
            if self.phase == 'arrival_release':
                state = self.input_snapshot();self.report['arrival_observed'] = state
                self.require(age<15, 'Normal arrival release is bounded; no input gate is forged')
                if state['reward_menu_open']:
                    self.require(age<5, 'Unexpected reward menu: probe will not choose or grant a card')
                    return
                if (age<1 or not state['world_ready'] or state['loading_visible'] or state['mouse_cursor']
                        or state['move_ignored'] or state['look_ignored'] or state['boss_intro']):
                    return
                self.check('F3 starts hidden', not self.panels())
                self.check('No passives were auto-granted by the review fixture', not self.owned_ids())
                self.check('Baseline max stamina is the real 100', abs(float(self.hero.get_max_stamina())-100)<.01)
                self.screenshot('01_hidden_baseline.png')
                self.request_f3_handler();self.enter('main_open');return
            if self.capture_hz and wall>=self.record_at:
                self.record_at = wall+1/self.capture_hz
                self.screenshot('%05d.png'%len(self.report['frames']), sequence=True)
            if self.phase == 'main_open':
                self.require(age<5, 'F3 main panel opens')
                if not self.panels() or age<.4:
                    return
                self.check('F3 opens its main page', self.page_index()==0)
                self.check('Opening F3 does not grant passives', not self.owned_ids())
                self.screenshot('02_main_passives_entry.png')
                self.click('OpenPassivesButton');self.enter('passive_page')
            elif self.phase == 'passive_page':
                self.require(age<5, 'Actual passive-page button opens the subpage')
                if self.page_index()!=1 or age<.4:
                    return
                names = {}
                for ident in IDS:
                    names[ident] = dict(name=self.text('PassiveName_'+ident), description=self.text('PassiveDescription_'+ident),
                        owned=self.text('PassiveOwned_'+ident))
                    self.check('Named passive row and description: '+ident, bool(names[ident]['name'] and names[ident]['description']))
                    self.check('Initially unowned row: '+ident, names[ident]['owned']=='Не получено')
                rows = [widget for widget in u.ObjectIterator(u.Border)
                    if widget.get_name().startswith('PassiveRow_') and outer_contains(widget, self.panel())]
                self.check('Exactly the current catalog passive rows', len(rows)==len(IDS))
                self.report['page_rows'] = names
                self.screenshot('03_current_named_rows.png')
                self.control('PassiveScroll', u.ScrollBox).scroll_widget_into_view(
                    self.control('PassiveRow_'+TARGET, u.Border), False, u.DescendantScrollDestination.CENTER, 10.)
                self.enter('deep_breath_visible')
            elif self.phase == 'deep_breath_visible' and age>.45:
                self.report['before_grant'] = self.row_snapshot()
                self.click('PassiveGrant_'+TARGET);self.enter('grant_observed')
            elif self.phase == 'grant_observed':
                self.require(age<5, 'Grant result appears in row and gameplay stamina')
                state = self.row_snapshot();self.report['grant_observed'] = state
                if not (self.perks.get_stacks(TARGET)==1 and abs(state['max_stamina']-120)<.01
                        and state['owned']=='Есть'):
                    return
                self.check('Actual row grant produces 120 max stamina and owned row state', True)
                self.check('Grant disables duplicate and enables remove', not state['grant_enabled'] and state['remove_enabled'])
                self.check('Only the clicked passive is owned', state['owned_ids']==[TARGET])
                if age<1:
                    return
                self.screenshot('04_deep_breath_owned_120.png')
                self.granted_name=state['name']
                self.press_f3();self.enter('granted_hud_closed')
            elif self.phase == 'granted_hud_closed':
                self.require(age<5, 'Granted passive reaches the normal gameplay HUD after closing F3')
                if self.panels() or age<.5:
                    return
                state=self.hud_snapshot();self.report['granted_gameplay_hud']=state
                if not (not state['mouse_cursor'] and self.granted_name in state['passive_hud']
                        and re.search(r'/\s*120(?:\D|$)',state['stamina_hud'])
                        and abs(state['max_stamina']-120)<.01):
                    return
                self.check('Closed F3 reveals the actual owned passive HUD chip and 120 maximum',True)
                self.screenshot('04b_gameplay_hud_owned_120.png')
                self.request_f3_handler();self.enter('reopen_for_remove')
            elif self.phase == 'reopen_for_remove':
                self.require(age<5,'F3 reopens for removal without changing ownership')
                if not self.panels() or age<.4:
                    return
                self.check('Reopening for removal retains the selected passive',self.owned_ids()==[TARGET])
                self.check('Reopening for removal starts on the main page',self.page_index()==0)
                self.click('OpenPassivesButton');self.enter('remove_page')
            elif self.phase == 'remove_page':
                self.require(age<5,'Actual passive button reopens the owned row')
                if self.page_index()!=1 or age<.4:
                    return
                self.check('Reopened passive page shows actual ownership',self.text('PassiveOwned_'+TARGET)=='Есть')
                self.control('PassiveScroll',u.ScrollBox).scroll_widget_into_view(
                    self.control('PassiveRow_'+TARGET,u.Border),False,u.DescendantScrollDestination.CENTER,10.)
                self.enter('remove_visible')
            elif self.phase == 'remove_visible' and age>.45:
                self.click('PassiveRemove_'+TARGET);self.enter('remove_observed')
            elif self.phase == 'remove_observed':
                self.require(age<5, 'Remove result appears in row and gameplay stamina')
                state = self.row_snapshot();self.report['remove_observed'] = state
                if not (self.perks.get_stacks(TARGET)==0 and abs(state['max_stamina']-100)<.01
                        and state['owned']=='Не получено'):
                    return
                self.check('Actual row remove restores 100 and clears the owned row state', True)
                self.check('Remove restores grant availability', state['grant_enabled'] and not state['remove_enabled'])
                if age<1:
                    return
                self.screenshot('05_removed_stamina_100.png')
                self.click('BackButton');self.enter('back_main')
            elif self.phase == 'back_main':
                self.require(age<5, 'Actual back button returns to the F3 main page')
                if self.page_index()!=0 or age<.4:
                    return
                self.check('Back returns to main page', True)
                self.screenshot('06_back_to_main.png');self.press_f3();self.enter('closed')
            elif self.phase == 'closed':
                self.require(age<5, 'F3 closes the actual panel')
                if self.panels() or age<.5:
                    return
                state=self.hud_snapshot();self.report['removed_gameplay_hud']=state
                if not (not state['mouse_cursor'] and self.granted_name not in state['passive_hud']
                        and re.search(r'/\s*100(?:\D|$)',state['stamina_hud'])
                        and abs(state['max_stamina']-100)<.01):
                    return
                self.check('F3 hides the whole dev panel', True)
                self.check('Closed F3 gameplay HUD loses the removed chip and restores 100 maximum',True)
                self.check('Closing F3 leaves the current loadout empty', not self.owned_ids())
                self.screenshot('07_closed_real_hud.png');self.request_f3_handler();self.enter('reopened_main')
            elif self.phase == 'reopened_main':
                self.require(age<5, 'F3 can reopen after closing')
                if not self.panels() or age<.4:
                    return
                self.check('Reopening still auto-grants nothing', not self.owned_ids() and abs(float(self.hero.get_max_stamina())-100)<.01)
                self.check('Reopening starts on main page', self.page_index()==0)
                self.click('OpenPassivesButton');self.enter('reopened_passives')
            elif self.phase == 'reopened_passives':
                self.require(age<5, 'Reopened passive page is usable')
                if self.page_index()!=1 or age<.5:
                    return
                self.check('Reopened page retains the removed state', self.text('PassiveOwned_'+TARGET)=='Не получено')
                self.screenshot('08_reopened_no_auto_grant.png');self.press_f3();self.enter('final_close')
            elif self.phase == 'final_close':
                self.require(age<5, 'F3 also closes directly from the passive page')
                if self.panels() or age<.5:
                    return
                self.check('F3 closes from the passive subpage', True)
                self.check('Final real max stamina remains 100', abs(float(self.hero.get_max_stamina())-100)<.01)
                self.check('Final loadout remains empty', not self.owned_ids())
                self.screenshot('09_final_hidden.png');self.finish()
        except Exception:
            self.finish(traceback.format_exc())
        finally:
            self.busy = False


probe = F3PassivesSolo()
setattr(builtins, KEY, probe)
try:
    probe.start()
except Exception:
    probe.finish(traceback.format_exc())
    raise
