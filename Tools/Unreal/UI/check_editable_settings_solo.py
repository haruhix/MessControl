"""Exercise the saved settings WBP in one owned solo PIE; restore local preferences.

Normal menu callbacks and UMG controls are used, not physical mouse/key injection.
No network, cook, gameplay overrides, cache clearing or asset saves. Screenshots
are real game-window captures. Launch via the owned Editor harness with -MCHubOwned.
"""
import builtins
import importlib.util
import time
import traceback
from pathlib import Path

import unreal as u

ROOT = Path('E:/DEVGAME/MessControl')
OUT = Path(globals().get('MC_SETTINGS_OUTPUT',
    'E:/CodexWork/MessControlEditableUI_20261020/SettingsSolo')).resolve()
spec = importlib.util.spec_from_file_location('mc_settings_loading_helpers',
    ROOT / 'Tools/Unreal/check_resource_loading_solo.py')
loading = importlib.util.module_from_spec(spec)
loading.MC_RESOURCE_LOADING_OUTPUT = str(OUT)
loading.MC_RESOURCE_LOADING_MAP = 'L_MainMenu'
loading.MC_RESOURCE_LOADING_CAPTURE_HZ = 4.
spec.loader.exec_module(loading)
helpers = loading.helpers
require, valid, path = helpers.require, helpers.valid, helpers.path


class SettingsSolo(loading.ResourceLoadingSolo):
    def __init__(self):
        super().__init__()
        self.preferences = u.MCGameUserSettings.get()
        require(valid(self.preferences), 'Project GameUserSettings class was not loaded')
        self.preference_fields = ('master_volume', 'music_volume', 'ambience_volume',
            'sfx_volume', 'language_code', 'mouse_sensitivity', 'gamepad_sensitivity', 'invert_y')
        self.before_preferences = {name: getattr(self.preferences, 'get_' + name)()
            for name in self.preference_fields}
        self.graphics_fields = ('screen_resolution', 'fullscreen_mode', 'frame_rate_limit',
            'view_distance_quality', 'anti_aliasing_quality', 'shadow_quality',
            'global_illumination_quality', 'reflection_quality', 'post_processing_quality',
            'texture_quality', 'visual_effect_quality', 'foliage_quality', 'shading_quality')
        self.before_graphics = {name: getattr(self.preferences, 'get_' + name)()
            for name in self.graphics_fields}
        self.before_scale = float(self.preferences.get_resolution_scale_information_ex()[1])
        self.before_vsync = self.preferences.is_v_sync_enabled()
        self.menu = None
        self.menu_root = None
        self.step = 0
        self.report.update(scope=__doc__, settings_before=self.before_preferences,
            graphics_before={name: str(value) for name, value in self.before_graphics.items()},
            render_scale_before=self.before_scale, vsync_before=self.before_vsync,
            limitations=['Local solo PIE; no packaged game or multiplayer.',
                'Normal callbacks/control setters are used; physical input is not injected.',
                'Audio routing/gains are checked; this silent capture is not a listening test.',
                'Display resolution and mode are kept unchanged; rollback is covered by native tests.'])

    def control(self, name, cls):
        matches = [item for item in u.ObjectIterator(cls.static_class())
            if valid(item) and item.get_name() == 'MenuSettings_' + name
            and helpers.has_outer(item, self.menu)]
        require(len(matches) == 1, 'Missing or duplicate authored control: ' + name)
        return matches[0]

    def action(self, name):
        self.menu.handle_action(getattr(u.MCMainMenuAction, name))
        self.report['requests'].append(dict(action=name,
            wall_seconds=time.monotonic() - self.started))

    def near(self, name, actual, expected):
        self.check(name, abs(float(actual) - expected) < .002,
            '%s: expected %s, got %s' % (name, expected, actual))

    def advance_loading(self, row, overlay):
        if self.phase == 'observe_preparation':
            if not row['ready'] or valid(overlay):
                return
            self.menu = helpers.owned_root_widget(u.MCMainMenuWidget, self.pc)
            if not valid(self.menu):
                return
            self.check('authored_menu_class', self.menu.get_class().get_path_name()
                == '/Game/Gameplay/UI/WBP_MainMenu.WBP_MainMenu_C', 'Native fallback menu is active')
            self.action('SETTINGS')
            self.menu_root = path(self.menu.get_active_menu_page_root())
            self.stage('settings_tabs')
            return
        if self.phase == 'capture_flush':
            if time.monotonic() - self.phase_wall > .5:
                self.begin_finish()
            return
        if self.phase != 'settings_tabs' or time.monotonic() - self.phase_wall < 1.:
            return
        self.phase_wall = time.monotonic()
        require(path(self.menu.get_active_menu_page_root()) == self.menu_root,
            'Switching a settings tab replaced its authored page')
        if self.step == 0:
            for name in ('ResolutionPicker', 'WindowModePicker', 'QualityPicker', 'FrameRatePicker'):
                self.check('populated_' + name, self.control(name, u.ComboBoxString).get_option_count() > 0,
                    'Empty runtime picker: ' + name)
            self.capture('01_graphics.png')
            self.action('SETTINGS_AUDIO')
        elif self.step == 1:
            for name, value in (('MasterSlider', .8), ('MusicSlider', .4),
                    ('AmbienceSlider', .3), ('SFXSlider', .6)):
                self.control(name, u.Slider).set_value(value)
            self.menu.refresh_localization()
            self.capture('02_audio.png')
            self.near('audio_stays_pending_before_apply',
                self.preferences.get_music_volume(), self.before_preferences['music_volume'])
            self.action('SETTINGS_CONTROLS')
        elif self.step == 2:
            self.control('MouseSlider', u.Slider).set_value(1.4)
            self.control('GamepadSlider', u.Slider).set_value(1.2)
            self.control('InvertYCheck', u.CheckBox).set_is_checked(True)
            self.menu.refresh_localization()
            self.capture('03_controls.png')
            self.action('SETTINGS_LANGUAGE')
        elif self.step == 3:
            picker = self.control('LanguagePicker', u.ComboBoxString)
            self.check('ten_language_choices', picker.get_option_count() == 10, 'Expected ten table culture choices')
            picker.set_selected_index(0)  # Russian, through the ordinary pending selector.
            self.capture('04_language.png')
            self.near('audio_pending_survives_tab_switches', self.control('MusicSlider', u.Slider).get_value(), .4)
            self.action('APPLY_SETTINGS')
        elif self.step == 4:
            p = self.preferences
            for name, expected in (('master_volume', .8), ('music_volume', .4),
                    ('ambience_volume', .3), ('sfx_volume', .6),
                    ('mouse_sensitivity', 1.4), ('gamepad_sensitivity', 1.2)):
                self.near('applied_' + name, getattr(p, 'get_' + name)(), expected)
            self.check('applied_invert_y', p.get_invert_y(), 'Invert Y was not committed')
            self.check('applied_language', p.get_language_code() == 'ru'
                and u.InternationalizationLibrary.get_current_language() == 'ru', 'Language did not apply')
            self.near('absolute_music_gain', p.get_effective_music_volume(), .32)
            self.near('absolute_ambience_gain', p.get_effective_ambience_volume(), .24)
            self.near('absolute_sfx_gain', p.get_effective_sfx_volume(), .48)
            p.set_music_volume(.12)
            p.load_settings(True)
            p.apply_gameplay_settings()
            self.near('saved_settings_reload', p.get_music_volume(), .4)
            self.capture('05_applied.png')
            self.action('BACK')
            self.action('SETTINGS')
        elif self.step == 5:
            self.action('SETTINGS_AUDIO')
            self.near('reopened_saved_music', self.control('MusicSlider', u.Slider).get_value(), .4)
            self.control('MusicSlider', u.Slider).set_value(.9)
            self.action('BACK')
            self.action('SETTINGS')
            self.action('SETTINGS_AUDIO')
            self.near('back_discards_unapplied_edit', self.control('MusicSlider', u.Slider).get_value(), .4)
            self.capture('06_reopened_saved_settings.png')
            self.stage('capture_flush')
        self.step += 1

    def finish(self):
        if self.done:
            return
        try:
            for name, value in self.before_preferences.items():
                result = getattr(self.preferences, 'set_' + name)(value)
                if name == 'language_code':
                    require(result, 'Original language preference was rejected')
            for name, value in self.before_graphics.items():
                getattr(self.preferences, 'set_' + name)(value)
            self.preferences.set_resolution_scale_value_ex(self.before_scale)
            self.preferences.set_v_sync_enabled(self.before_vsync)
            self.preferences.apply_non_resolution_settings()
            self.preferences.save_settings()
            u.InternationalizationLibrary.set_current_language_and_locale(self.initial_culture[0], False)
            u.InternationalizationLibrary.set_current_locale(self.initial_culture[1], False)
            self.report['settings_restored'] = {name: getattr(self.preferences, 'get_' + name)()
                for name in self.preference_fields}
            require(self.report['settings_restored'] == self.before_preferences,
                'User preferences were not restored exactly')
            require(all(getattr(self.preferences, 'get_' + name)() == value
                for name, value in self.before_graphics.items()), 'Graphics preferences were not restored')
            require(abs(self.preferences.get_resolution_scale_information_ex()[1] - self.before_scale) < .002
                and self.preferences.is_v_sync_enabled() == self.before_vsync,
                'Render scale/VSync were not restored')
            self.report['graphics_restored'] = True
        except Exception:
            self.error = self.error or traceback.format_exc()
        super().finish()


def main():
    old = getattr(builtins, 'mc_settings_solo', None)
    require(old is None or getattr(old, 'done', True), 'A settings check is already active')
    probe = SettingsSolo()
    builtins.mc_settings_solo = probe
    u.EditorPythonScripting.set_keep_python_script_alive(True)
    probe.write()
    probe.callback = u.register_slate_post_tick_callback(probe.tick)


if __name__ == '__main__':
    main()
