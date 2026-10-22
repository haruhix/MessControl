"""Check the saved Wash n Smash UI and real 3D camera in one owned solo PIE.

Keeps native studio-intro and preload gates. Exercises actual button OnClicked
delegates for Settings/Credits/Join/Exit-confirmation and their Back buttons,
then actual Play -> Hub. No physical mouse/gamepad delivery, Tutorial travel,
Steam search/invites/peers, mic use, settings changes, saves, build or day run.
Hard limit180s +12s cleanup. Existing helpers restore PIE settings/CVars/map.

Only the exact task-owned EditorWorker command path + its current-PID Ready
manifest is accepted as an alternative to the existing owned-editor flags.
Optional MC_WASH_MENU_SOLO_OUTPUT selects a fresh E: folder. The task worker
stays alive afterwards. Cancel: builtins.mc_washnsmash_menu_solo.cancel().
"""
import builtins
import hashlib
import importlib.util
import time
import traceback
from pathlib import Path

import unreal as u

ROOT = Path('E:/DEVGAME/MessControl')
OUT = Path(globals().get('MC_WASH_MENU_SOLO_OUTPUT',
    'E:/CodexWork/MessControlMenuShowcase_20261021/MenuSolo01')).resolve()
KEY = 'mc_washnsmash_menu_solo'
EXPECTED = '/Game/Gameplay/UI/WBP_WashNSmashMenu.WBP_WashNSmashMenu_C'
WORKER = 'E:/CodexWork/MessControlMenuShowcase_20261021/EditorWorker.py'

spec = importlib.util.spec_from_file_location('mc_washnsmash_frontend_helpers',
    ROOT / 'Tools/Unreal/UI/check_frontend_hub_solo.py')
frontend = importlib.util.module_from_spec(spec)
spec.loader.exec_module(frontend)
frontend.OUT = OUT
frontend.helpers.OUT = OUT
frontend.MC_FRONTEND_HUB_CAPTURE_HZ = 0
frontend.MC_FRONTEND_HUB_RETURN_TO_MENU = False
helpers = frontend.helpers
valid, path, prop, require = helpers.valid, helpers.path, helpers.prop, helpers.require


def in_page(widget, page):
    for _ in range(24):
        if not valid(widget):
            return False
        if widget == page:
            return True
        widget = widget.get_parent()
    raise RuntimeError('Unexpected cyclic/deep widget ancestry')


class WashNSmashMenuSolo(frontend.FrontendHubSolo):
    task_owned_editor_worker = WORKER

    def __init__(self):
        super().__init__()
        self.ui_index = 0
        self.ui_steps = (
            ('SETTINGS', 'MenuSettings_Page', 'settings'),
            ('BACK', 'MenuMain_Page', 'back_from_settings'),
            ('CREDITS', 'MenuCredits_Page', 'credits'),
            ('BACK', 'MenuMain_Page', 'back_from_credits'),
            ('JOINLOBBY', 'MenuJoin_Page', 'join_friends'),
            ('BACK', 'MenuMain_Page', 'back_from_join'),
            ('EXIT', 'MenuExit_Page', 'exit_confirmation'),
            ('BACK', 'MenuMain_Page', 'back_from_exit'),
        )
        self.report.update(scope=__doc__, source_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            expected_menu_class=EXPECTED, accepted_task_worker=WORKER, page_checks=[],
            limitations=[
                'Actual OnClicked delegates exercise native routes; physical mouse/gamepad hit delivery is not tested.',
                'Tutorial is checked as an enabled authored action, but its travel and gameplay are deferred.',
                'Exit opens its confirmation only. The application is never asked to quit.',
                'No Steam search/join/invite, external peer, microphone, settings apply, save, build or day run.',
                'Warm solo PIE does not establish packaged/cold-cache performance or multiplayer behaviour.'])

    def current_menu(self, expected_page=None):
        require(valid(self.menu) and self.menu.get_class().get_path_name() == EXPECTED,
            'The saved Wash n Smash menu was not the actual viewport root')
        page = self.menu.get_active_menu_page_root()
        require(valid(page), 'No active authored menu page')
        if expected_page:
            require(page.get_name() == expected_page,
                'Expected ' + expected_page + ', got ' + page.get_name())
        return page

    def action_button(self, action):
        page = self.current_menu()
        matches = [item for item in u.ObjectIterator(u.MCMainMenuActionButton.static_class())
            if valid(item) and helpers.has_outer(item, self.menu) and in_page(item, page)
            and frontend.effectively_visible(item) and item.get_is_enabled()
            and helpers.enum_name(prop(item, 'action')) == action]
        require(len(matches) == 1, 'Expected one visible enabled current action: ' + action)
        return matches[0]

    def request_action(self, action, phase):
        button = self.action_button(action)
        self.report['requests'].append(dict(method='MCMainMenuActionButton.OnClicked.broadcast',
            action=action, button=path(button), active_page=path(self.current_menu()),
            preparation_ready=bool(self.preparation.is_ready()), physical_pointer=False,
            wall_seconds=time.monotonic()-self.started))
        self.stage(phase)
        button.on_clicked.broadcast()

    def verify_main(self):
        self.current_menu('MenuMain_Page')
        title = self.control(u.TextBlock, 'MenuTitle', self.menu)
        require(valid(title) and str(title.get_text()) == 'Wash n Smash', 'Wrong actual menu title')
        actions = ('CREATELOBBY', 'JOINLOBBY', 'TUTORIAL', 'SETTINGS', 'CREDITS', 'EXIT')
        buttons = [self.action_button(action) for action in actions]
        play_label = self.control(u.TextBlock, 'MenuMain_Action1Label', self.menu)
        table = u.load_asset('/Game/Gameplay/UI/Data/DT_UI_Localization')
        expected_play = str(u.MCUIDataLibrary.resolve_text(table, u.Name('UI.WashMenu.Play'), u.Text(''), ''))
        require(valid(play_label) and str(play_label.get_text()) == expected_play,
            'Actual primary label did not use the localized Play text')
        self.check('saved_washnsmash_menu_is_live', True, '')
        self.check('six_authored_actions_visible_and_enabled', len(buttons) == 6, '')
        self.check('actual_brand_and_play_localized', True, '')
        target = self.pc.get_view_target()
        require(isinstance(target, u.CameraActor), 'Real authored CameraActor is not the view target')
        self.report['actual_menu'] = dict(widget=path(self.menu), title=str(title.get_text()),
            primary_label=str(play_label.get_text()), actions=list(actions),
            button_paths=[path(button) for button in buttons], view_target=path(target),
            camera_class=target.get_class().get_path_name(), camera_location=helpers.vec(target.get_actor_location()),
            camera_or_player_replaced=False)

    def click_saved_play(self):
        # The inherited startup path reaches this only after native intro,
        # preload readiness and loading-cover removal; do not bypass them.
        self.verify_main()
        self.capture('05_washnsmash_real_camera_and_umg.png')
        self.request_action(self.ui_steps[0][0], 'check_wash_menu_page')

    def advance(self, ready, loading, splash):
        if self.phase != 'check_wash_menu_page':
            return super().advance(ready, loading, splash)
        require(self.current_map == 'L_MainMenu' and ready and not valid(loading) and not valid(splash),
            'Menu page inspection unexpectedly changed/bypassed the prepared world')
        if time.monotonic()-self.phase_wall < .45:
            return
        action, expected_page, label = self.ui_steps[self.ui_index]
        page = self.current_menu(expected_page)
        self.check('actual_page_' + label, True, '')
        self.report['page_checks'].append(dict(name=label, page=path(page),
            action=action, wall_seconds=time.monotonic()-self.started))
        if action != 'BACK':
            self.capture('%02d_%s_actual_ui.png' % (6+self.ui_index, label))
        self.ui_index += 1
        if self.ui_index < len(self.ui_steps):
            self.request_action(self.ui_steps[self.ui_index][0], 'check_wash_menu_page')
            return
        self.verify_main()
        button = self.action_button('CREATELOBBY')
        self.report['requests'].append(dict(method='MCMainMenuActionButton.OnClicked.broadcast',
            presentation='saved_washnsmash_menu', action='CREATELOBBY', button=path(button),
            blueprint_class=EXPECTED, physical_pointer=False,
            preparation_ready=bool(self.preparation.is_ready()), wall_seconds=time.monotonic()-self.started))
        self.play_requested = True
        self.stage('travel_hub')
        button.on_clicked.broadcast()

    def tick(self, delta):
        if not self.done and self.phase != 'ending_pie' and time.monotonic()-self.started >= 180.:
            self.begin_finish('Wash n Smash front-end exceeded180s; native readiness was not overridden')
        super().tick(delta)

    def finish(self):
        if self.done:
            return
        try:
            super().finish()
        finally:
            # FirstRelaxSolo's command-line runner normally exits after cleanup.
            # This check was dispatched inside a still-needed owned worker.
            u.EditorPythonScripting.set_keep_python_script_alive(True)
        self.report['cleanup']['task_worker_kept_alive'] = True
        self.write()
        u.log('MC_WASH_MENU_SOLO_' + ('FAIL ' if self.error else 'PASS ') + str(OUT / 'Report.json'))


def main():
    for key in (KEY, frontend.KEY, helpers.KEY, 'mc_resource_loading_solo', 'mc_hub_solo'):
        prior = getattr(builtins, key, None)
        require(prior is None or getattr(prior, 'done', True), 'Preserve an already active owned probe: ' + key)
    probe = WashNSmashMenuSolo()
    setattr(builtins, KEY, probe)
    u.EditorPythonScripting.set_keep_python_script_alive(True)
    probe.write()
    probe.callback = u.register_slate_post_tick_callback(probe.tick)


if __name__ == '__main__':
    main()
