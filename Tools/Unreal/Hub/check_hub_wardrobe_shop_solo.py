"""Owned short solo proof of the saved Hub wardrobe and empty vending-machine shop.

Prepare the rebuilt/saved WBP roles and real station bounds first; open the saved
Hub in an idle editor launched with -MCHubOwned. This script never changes maps,
authors actors, saves packages, starts peers, or records microphone audio.

DISCLOSED REVIEW FIXTURE: the real player is placed inside each actual station's
OBB, only after a walkable floor hit and capsule-clearance test. E uses the normal
SetHandleInputHeld callback, color edits use the actual palette controls, and the
visible Apply/Cancel buttons broadcast their normal OnClicked delegates. The
2D palette and hue strip use their actual normalized pointer-selection handler,
not individual RGB channel sliders. Escape
is delivered to the focused real Shop widget by the engine Slate inspector.
Local appearance preferences, including the original false custom-color flag,
and affected Saved/Config INI bytes are restored after the owned PIE ends.

Globals: MCHUB_WARDROBE_SHOP_OUTPUT (fresh E: folder),
MCHUB_WARDROBE_SHOP_CAPTURE_HZ (0..10, default 8),
MCHUB_WARDROBE_SHOP_MAX_FRAMES (0..600, default 450),
MCHUB_WARDROBE_COLOR_QUEUE_FIXTURE (default False; two disclosed direct color RPCs).
Cancel: builtins.mc_hub_wardrobe_shop_solo.cancel().
"""
import builtins
import hashlib
import importlib.util
import json
import re
import time
import traceback
from pathlib import Path

import unreal as u


ROOT = Path('E:/DEVGAME/MessControl')
OUT = Path(globals().get('MCHUB_WARDROBE_SHOP_OUTPUT',
    'E:/CodexWork/HubWardrobeRewards_20261022/WardrobeShopSolo01')).resolve()
KEY = 'mc_hub_wardrobe_shop_solo'
spec = importlib.util.spec_from_file_location('mc_wardrobe_shop_owned_hub_base',
    ROOT/'Tools/Unreal/Hub/check_hub_pier_fishing_solo.py')
base = importlib.util.module_from_spec(spec)
base.MCHUB_PIER_OUTPUT = str(OUT)
spec.loader.exec_module(base)
require, valid, path, vec, dirty = base.require, base.valid, base.path, base.vec, base.dirty


def rgba(color):
    return [float(color.r), float(color.g), float(color.b), float(color.a)]


def same_color(a, b, tolerance=.002):
    return all(abs(x-y) < tolerance for x, y in zip(rgba(a), rgba(b)))


def restore_ini_bool(data, section, key, value):
    """Edit one scalar in its existing section, retaining encoding/other lines."""
    bom, encoding = (b'', 'utf-8')
    for marker, codec in ((b'\xff\xfe', 'utf-16-le'), (b'\xfe\xff', 'utf-16-be'), (b'\xef\xbb\xbf', 'utf-8')):
        if data.startswith(marker):
            bom, encoding = marker, codec; break
    text = data[len(bom):].decode(encoding)
    lines = text.splitlines(keepends=True)
    newline = '\r\n' if '\r\n' in text else '\n'
    section_pattern = re.compile(r'^\s*\[([^\]]+)\]\s*(?:[;#].*)?$')
    key_pattern = re.compile(r'^(\s*)'+re.escape(key)+r'\s*=')
    found_section = found_key = active = False
    output = []
    for line in lines:
        header = section_pattern.match(line.rstrip('\r\n'))
        if header:
            if active and not found_key:
                if output and not output[-1].endswith(('\n', '\r')):
                    output[-1] += newline
                output.append(key+'='+('True' if value else 'False')+newline)
                found_key = True
            active = header.group(1).casefold() == section.casefold()
            found_section = found_section or active
        match = key_pattern.match(line) if active else None
        if match:
            ending = '\r\n' if line.endswith('\r\n') else '\n' if line.endswith('\n') else ''
            line = match.group(1)+key+'='+('True' if value else 'False')+ending
            found_key = True
        output.append(line)
    require(found_section, 'Settings INI is missing the exact native class section: '+section)
    if active and not found_key:
        if output and not output[-1].endswith(('\n', '\r')):
            output[-1] += newline
        output.append(key+'='+('True' if value else 'False')+newline)
    return bom+''.join(output).encode(encoding)


def has_outer(obj, owner):
    for _ in range(24):
        if not valid(obj):
            return False
        if obj == owner:
            return True
        obj = obj.get_outer()
    return False


class HubWardrobeShopSolo(base.HubPierSolo):
    def __init__(self):
        super().__init__()
        require(hasattr(u, 'MCHubWardrobeWidget') and hasattr(u, 'MCHubShopWidget') and hasattr(u, 'MCWardrobeColorPalette'),
            'New native widget classes are not loaded; rebuild/restart first')
        self.preferences = u.MCGameUserSettings.get()
        require(valid(self.preferences), 'Project GameUserSettings class is unavailable')
        self.before_custom = bool(self.preferences.has_custom_bag_color())
        self.before_saved = self.preferences.get_saved_bag_color()
        self.config_root = Path(u.Paths.generated_config_dir()).resolve()
        require(self.config_root.drive.upper() == 'E:', 'Generated config must remain on E:')
        self.config_before = {p: p.read_bytes() for p in self.config_files()}
        for filename, data in self.config_before.items():
            backup = OUT/'PreferenceBackup'/filename.relative_to(self.config_root)
            backup.parent.mkdir(parents=True, exist_ok=True)
            backup.write_bytes(data)
            require(backup.read_bytes() == data, 'Preference backup failed verification')
        self.old_slate_throttling = u.SystemLibrary.get_console_variable_int_value('Slate.bAllowThrottling')
        self.slate = u.get_default_object(u.load_class(None, '/Script/SlateInspectorToolset.SlateInspectorToolset'))
        require(valid(self.slate), 'SlateInspectorToolset is required to test the actual Escape route')
        self.wardrobe = self.shop = self.widget = None
        self.before_color = self.committed_color = self.queue_last = None
        self.palette_from = self.palette_target = None
        self.max_walk_speed = 0.
        self.queue_fixture = bool(globals().get('MCHUB_WARDROBE_COLOR_QUEUE_FIXTURE', False))
        self.capture_hz = max(0., min(10., float(globals().get('MCHUB_WARDROBE_SHOP_CAPTURE_HZ', 8.))))
        self.max_frames = max(0, min(600, int(globals().get('MCHUB_WARDROBE_SHOP_MAX_FRAMES', 450))))
        self.report.update(scope=__doc__, teleports=True, review_fixtures=[],
            capture_hz=self.capture_hz, direct_color_queue_fixture=self.queue_fixture,
            source_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            preferences_before=dict(has_custom_color=self.before_custom, saved_color=rgba(self.before_saved)),
            config_before={str(p): hashlib.sha256(data).hexdigest() for p, data in self.config_before.items()},
            limitations=[
                'One local authority PIE; no multiplayer, packaged game, cook or performance benchmark.',
                'The actual player is temporarily placed at collision-checked station bounds; no walking approach or discoverability proof.',
                'E uses the normal held-input callback; palette pointer-selection/button controls are exercised without physical mouse hardware.',
                'Escape uses engine Slate key delivery to the focused real widget; operating-system keyboard delivery is not tested.',
                'The shop is intentionally empty. No currency, price, purchase or future merchandise is invented.',
                'Native PNG capture is requested at the reported cadence; repeated video frames are not an FPS benchmark.',
                'Screenshots and any subsequently encoded clips await root visual review.'])

    def config_files(self):
        if not self.config_root.exists():
            return set()
        return {p.resolve() for name in ('GameUserSettings.ini', 'EditorSettings.ini')
            for p in self.config_root.rglob(name) if p.is_file()}

    def start(self):
        require(not u.InputLibrary.modifier_keys_state_is_control_down(u.InputLibrary.get_modifier_keys_state()),
            'Release Ctrl before starting; Unreal otherwise starts spectator PIE')
        u.SystemLibrary.execute_console_command(self.editor_world, 'Slate.bAllowThrottling 0')
        super().start()

    def owned_widget(self, cls):
        matches = [item for item in u.ObjectIterator(cls.static_class()) if valid(item)
            and item.get_owning_player() == self.pc and item.is_in_viewport() and item.is_visible()]
        require(len(matches) <= 1, 'Duplicate live owned viewport widget: '+str(cls))
        return matches[0] if matches else None

    def controls(self, cls=u.Widget):
        return [item for item in u.ObjectIterator(cls.static_class()) if valid(item) and has_outer(item, self.widget)]

    def control(self, name, cls):
        matches = [item for item in self.controls(cls) if item.get_name() == name]
        require(len(matches) == 1, 'Expected one authored control: '+name)
        return matches[0]

    def button(self, name):
        button = self.control(name, u.Button)
        require(button.get_is_enabled(), 'Visible action is disabled: '+name)
        button.on_clicked.broadcast()
        self.report.setdefault('input_requests', []).append(dict(method='Actual UButton.OnClicked.broadcast',
            control=name, game_seconds=self.now()))

    def press_e(self):
        self.hero.set_handle_input_held(False)
        self.hero.set_handle_input_held(True)
        self.hero.set_handle_input_held(False)
        self.report.setdefault('input_requests', []).append(dict(method='MCToothCharacter.SetHandleInputHeld true/false',
            meaning='Normal E interaction callback; no direct TryOpenWardrobe/TryOpenHubShop call', game_seconds=self.now()))

    def commit_color_fixture(self, color):
        # ServerSetPlayerColor is a reflected RPC, but is intentionally NOT
        # BlueprintCallable, so Python exposes it through call_method only.
        # Exact declaration: UFUNCTION(Server,Reliable) void ...(FLinearColor Color).
        self.pc.call_method('ServerSetPlayerColor', args=(color,))

    def material_color(self):
        # BagMaterial is protected. Follow only public mesh-slot/parent data;
        # the native BeginPlay chooses the LAST MID descended from MI_Bag.
        mesh = self.hero.mesh
        bag_base = '/Game/Gameplay/CharacterCurrent/Materials/MI_Bag.MI_Bag'
        matches = []
        for index in range(mesh.get_num_materials()):
            candidate = mesh.get_material(index)
            if not isinstance(candidate, u.MaterialInstanceDynamic):
                continue
            parent = candidate
            for _ in range(12):
                if path(parent) == bag_base:
                    matches.append((index, candidate)); break
                if not isinstance(parent, u.MaterialInstance):
                    break
                parent = parent.get_editor_property('Parent')  # Public BlueprintReadOnly UPROPERTY.
        require(matches, 'No real mesh-slot MID descended from MI_Bag was found')
        self.report['bag_material_slots'] = [dict(slot=index, material=path(item)) for index, item in matches]
        material = matches[-1][1]
        return material.get_vector_parameter_value('Albedo Color')

    def player_state(self):
        state = self.pc.get_editor_property('PlayerState')
        require(valid(state), 'PlayerState is unavailable')
        return state

    def station(self, cls):
        matches = [actor for actor in u.GameplayStatics.get_all_actors_of_class(self.world, cls)
            if actor.get_editor_property('bEnabled')]
        require(len(matches) == 1, 'Expected exactly one real enabled '+str(cls)+' station')
        return matches[0]

    def place_at(self, station):
        """Choose a walkable, unoccupied point inside the saved, transformed OBB."""
        volume = station.get_editor_property('InteractionVolume')
        center, transform = volume.get_world_location(), volume.get_world_transform()
        extent = volume.get_unscaled_box_extent()
        capsule = self.hero.capsule_component
        half_height, radius = capsule.get_scaled_capsule_half_height(), capsule.get_scaled_capsule_radius()
        statics = u.get_default_object(u.GameplayStatics)
        traces = []
        target = None
        for fx, fy in ((0, 0), (0, .35), (0, -.35), (.35, 0), (-.35, 0),
                       (.35, .35), (-.35, .35), (.35, -.35), (-.35, -.35)):
            point = u.MathLibrary.transform_location(transform, u.Vector(extent.x*fx, extent.y*fy, 0))
            hit = u.SystemLibrary.line_trace_single(self.world, point+u.Vector(0, 0, 600),
                point-u.Vector(0, 0, 1200), u.TraceTypeQuery.TRACE_TYPE_QUERY1, True,
                [self.hero], u.DrawDebugTrace.NONE, True)
            if not hit:
                continue
            parts = statics.call_method('BreakHitResult', args=(hit,))
            row = dict(local_fraction_xy=[fx, fy], blocking=bool(parts[0]),
                actor=path(parts[9]), impact_point_cm=vec(parts[5]), normal=vec(parts[7]))
            traces.append(row)
            if not parts[0] or parts[7].z < .75:
                continue
            candidate = parts[5]+u.Vector(0, 0, half_height+3.)
            local = u.MathLibrary.inverse_transform_location(transform, candidate)
            if any(abs(a) > b-3. for a, b in zip(vec(local), vec(extent))):
                continue
            clearance = u.SystemLibrary.capsule_trace_single(self.world, candidate, candidate+u.Vector(0, 0, .1),
                radius, half_height, u.TraceTypeQuery.TRACE_TYPE_QUERY1, False,
                [self.hero], u.DrawDebugTrace.NONE, True)
            obstruction = statics.call_method('BreakHitResult', args=(clearance,)) if clearance else None
            row['clearance_blocked'] = bool(obstruction and obstruction[0])
            if row['clearance_blocked']:
                continue
            target = candidate
            break
        require(target is not None, 'No walkable, capsule-clear review placement exists inside '+path(station))
        before = self.hero.get_actor_location()
        self.hero.character_movement.stop_movement_immediately()
        self.hero.set_actor_location_and_rotation(target, station.get_actor_rotation(), False, True)
        require((self.hero.get_actor_location()-target).length() < 3. and station.contains_pawn(self.hero),
            'Review placement failed or actual ContainsPawn rejects the OBB point')
        self.report['review_fixtures'].append(dict(kind='Player placement ONLY in owned PIE',
            reason='Isolate existing station UI; not a walking approach/discoverability proof',
            station=path(station), before_cm=vec(before), after_cm=vec(self.hero.get_actor_location()),
            bounds_center_cm=vec(center), bounds_half_extent_cm=vec(extent), floor_checks=traces))

    def initialize_world(self, worlds):
        if not worlds:
            return False
        require(len(worlds) == 1 and u.SystemLibrary.is_server(worlds[0]), 'Expected one standalone authority PIE')
        if self.world is None:
            self.world = worlds[0]
            self.instance = u.GameplayStatics.get_game_instance(self.world)
            require(u.GameplayStatics.get_current_level_name(self.world, True) == 'Hub', 'Probe requires saved Hub')
        require(worlds[0] == self.world and valid(self.instance), 'Owned PIE was replaced')
        self.pc = u.GameplayStatics.get_player_controller(self.world, 0)
        if not isinstance(self.pc, u.MCPlayerController):
            return False
        self.hero = self.pc.get_controlled_pawn()
        if not isinstance(self.hero, u.MCToothCharacter) or self.now() < 2. or not self.pc.is_world_presentation_ready() or self.pc.is_move_input_ignored():
            return False
        self.wardrobe, self.shop = self.station(u.MCHubWardrobeStation), self.station(u.MCHubShopStation)
        self.pawn_path = path(self.hero)
        self.before_color = self.hero.get_player_color()
        self.report.update(world=path(self.world), pawn=self.pawn_path,
            wardrobe_station=path(self.wardrobe), shop_station=path(self.shop),
            player_color_before=rgba(self.before_color))
        self.stage('spawn_ready')
        self.capture('01_ActualHubSpawn.png')
        return True

    def begin_palette(self, target, phase):
        self.palette_from = vec(self.control('WardrobeColorPalette', u.MCWardrobeColorPalette).get_hsv())
        self.palette_target = target
        self.stage(phase, 'WardrobeRGB')

    def animate_palette(self, age):
        def ease(progress):
            progress = min(1., max(0., progress))
            return progress*progress*(3.-2.*progress)
        # Two sequential normal gestures: choose hue, then drag inside SV.
        hue = self.palette_from[0]+(self.palette_target[0]-self.palette_from[0])*ease(age/.8)
        saturation, value = [a+(b-a)*ease((age-.8)/1.6)
            for a, b in zip(self.palette_from[1:], self.palette_target[1:])]
        self.control('WardrobeHueStrip', u.MCWardrobeColorPalette).select_normalized_point(u.Vector2D(.5, hue))
        self.control('WardrobeColorPalette', u.MCWardrobeColorPalette).select_normalized_point(u.Vector2D(saturation, 1.-value))
        return age >= 3.

    def check_preview(self, prefix):
        draft = self.widget.get_draft_color()
        self.check(prefix+'_draft_changes', not same_color(draft, self.before_color))
        self.check(prefix+'_bag_mid_matches_draft', same_color(self.material_color(), draft))
        self.check(prefix+'_replicated_color_unchanged', same_color(self.hero.get_player_color(), self.before_color))
        self.check(prefix+'_player_state_unchanged', same_color(self.player_state().get_editor_property('PlayerColor'), self.before_color))
        self.check(prefix+'_saved_flag_unchanged', self.preferences.has_custom_bag_color() == self.before_custom)
        self.check(prefix+'_saved_color_unchanged', same_color(self.preferences.get_saved_bag_color(), self.before_saved))
        palette = self.control('WardrobeColorPalette', u.MCWardrobeColorPalette)
        hue_strip = self.control('WardrobeHueStrip', u.MCWardrobeColorPalette)
        self.check(prefix+'_palette_hue_strip_synchronized', (palette.get_hsv()-hue_strip.get_hsv()).length() < .00001)
        self.check(prefix+'_palette_reaches_selected_hsv', all(abs(a-b) < .00001
            for a, b in zip(vec(palette.get_hsv()), self.palette_target)))

    def advance(self):
        age = self.now()-self.phase_at
        if self.phase == 'spawn_ready' and age >= 1.:
            self.place_at(self.wardrobe); self.stage('wardrobe_placement_settle')
        elif self.phase == 'wardrobe_placement_settle' and age >= .7:
            self.check('wardrobe_fixture_on_walkable_floor', not self.hero.character_movement.is_falling())
            self.check('wardrobe_available_at_actual_bounds', self.pc.is_wardrobe_available())
            self.press_e(); self.stage('wardrobe_open_wait', 'WardrobeRGB')
        elif self.phase == 'wardrobe_open_wait':
            self.widget = self.owned_widget(u.MCHubWardrobeWidget)
            if not valid(self.widget):
                require(age < 3., 'Normal E did not open Wardrobe'); return
            self.check('authored_wardrobe_wbp', self.widget.get_class().get_path_name() ==
                '/Game/Gameplay/UI/WBP_HubWardrobe.WBP_HubWardrobe_C')
            self.check('wardrobe_blocks_move_and_look', self.pc.is_move_input_ignored() and self.pc.is_look_input_ignored())
            slots = [self.control('WardrobeDefaultSlot', u.Border)]+[
                self.control('WardrobeEmptySlot'+str(i), u.Border) for i in range(1, 6)]
            self.check('wardrobe_six_skin_slots', len(slots) == 6)
            self.check('wardrobe_default_only_enabled', slots[0].get_is_enabled() and all(not s.get_is_enabled() for s in slots[1:]))
            self.control('WardrobeColorPalette', u.MCWardrobeColorPalette)
            self.control('WardrobeHueStrip', u.MCWardrobeColorPalette)
            self.check('individual_rgb_controls_removed', not any(item.get_name() in
                ('WardrobeRSlider', 'WardrobeGSlider', 'WardrobeBSlider') for item in self.controls()))
            self.check('configure_preserves_authoritative_linear_color', same_color(self.widget.get_draft_color(), self.before_color, .00001))
            self.capture('02_Wardrobe_DefaultAndEmptySlots.png'); self.stage('wardrobe_baseline')
        elif self.phase == 'wardrobe_baseline' and age >= 1.5:
            self.begin_palette([.93, .75, .82], 'wardrobe_palette_preview')
        elif self.phase == 'wardrobe_palette_preview' and self.animate_palette(age):
            self.check_preview('preview_cancel'); self.capture('03_Wardrobe_RGBDraft.png')
            self.stage('wardrobe_cancel_hold')
        elif self.phase == 'wardrobe_cancel_hold' and age >= 1.2:
            self.button('WardrobeCancel'); self.stage('wardrobe_cancelled')
        elif self.phase == 'wardrobe_cancelled' and age >= .6:
            self.check('cancel_closes_wardrobe', not self.pc.is_wardrobe_open())
            self.check('cancel_restores_actual_mid', same_color(self.material_color(), self.before_color))
            self.check('cancel_leaves_authority_unchanged', same_color(self.hero.get_player_color(), self.before_color))
            self.check('cancel_releases_move_and_look', not self.pc.is_move_input_ignored() and not self.pc.is_look_input_ignored())
            self.capture('04_CancelRestoresColor.png'); self.press_e(); self.stage('wardrobe_reopen')
        elif self.phase == 'wardrobe_reopen' and age >= .6:
            self.widget = self.owned_widget(u.MCHubWardrobeWidget)
            require(valid(self.widget), 'Wardrobe did not reopen through E')
            self.check('reopen_discards_cancelled_draft', same_color(self.widget.get_draft_color(), self.before_color, .00001))
            self.begin_palette([.45, .78, .72], 'wardrobe_palette_commit')
        elif self.phase == 'wardrobe_palette_commit' and self.animate_palette(age):
            self.check_preview('preview_apply'); self.committed_color = self.widget.get_draft_color()
            self.capture('05_Wardrobe_BeforeApply.png'); self.stage('wardrobe_apply_hold')
        elif self.phase == 'wardrobe_apply_hold' and age >= 1.2:
            self.button('WardrobeApply'); self.stage('wardrobe_applied')
        elif self.phase == 'wardrobe_applied' and age >= .8:
            color = self.committed_color
            self.check('apply_closes_wardrobe', not self.pc.is_wardrobe_open())
            self.check('apply_updates_authoritative_pawn', same_color(self.hero.get_player_color(), color))
            self.check('apply_updates_player_state', same_color(self.player_state().get_editor_property('PlayerColor'), color))
            self.check('apply_updates_visible_mid', same_color(self.material_color(), color))
            self.check('apply_saves_custom_flag', self.preferences.has_custom_bag_color())
            self.check('apply_saves_rgb', same_color(self.preferences.get_saved_bag_color(), color))
            self.preferences.load_settings(True)
            self.check('saved_rgb_survives_settings_reload', self.preferences.has_custom_bag_color() and
                same_color(self.preferences.get_saved_bag_color(), color))
            self.report['confirmed_color'] = rgba(color)
            self.capture('06_AppliedAndSaved.png')
            if self.queue_fixture:
                first, self.queue_last = u.LinearColor(.31, .62, .21, 1), u.LinearColor(.8, .15, .55, 1)
                self.commit_color_fixture(first); self.commit_color_fixture(self.queue_last)
                self.report['review_fixtures'].append(dict(kind='Optional two immediate direct ServerSetPlayerColor RPCs',
                    reason='Coalescing regression only; not a normal wardrobe button workflow',
                    colors=[rgba(first), rgba(self.queue_last)], game_seconds=self.now()))
                self.stage('color_queue_wait')
            else:
                self.stage('wardrobe_finish_hold')
        elif self.phase == 'color_queue_wait' and age >= .6:
            self.check('coalescing_keeps_final_pawn_rgb', same_color(self.hero.get_player_color(), self.queue_last))
            self.check('coalescing_keeps_final_state_rgb', same_color(self.player_state().get_editor_property('PlayerColor'), self.queue_last))
            self.commit_color_fixture(self.committed_color); self.stage('wardrobe_finish_hold')
        elif self.phase == 'wardrobe_finish_hold' and age >= 1.3:
            self.clip = None
            self.place_at(self.shop); self.stage('shop_placement_settle')
        elif self.phase == 'shop_placement_settle' and age >= .7:
            self.check('shop_fixture_on_walkable_floor', not self.hero.character_movement.is_falling())
            self.check('shop_available_at_actual_bounds', self.pc.is_hub_shop_available())
            self.press_e(); self.stage('shop_open_wait', 'EmptyShop')
        elif self.phase == 'shop_open_wait':
            self.widget = self.owned_widget(u.MCHubShopWidget)
            if not valid(self.widget):
                require(age < 3., 'Normal E did not open Shop'); return
            self.check('authored_shop_wbp', self.widget.get_class().get_path_name() ==
                '/Game/Gameplay/UI/WBP_HubShop.WBP_HubShop_C')
            slots = [self.control('ShopEmptySlot'+str(i), u.Border) for i in range(6)]
            self.check('shop_six_disabled_empty_slots', len(slots) == 6 and all(not slot.get_is_enabled() for slot in slots))
            self.check('shop_empty_copy_present', bool(str(self.control('ShopEmptyMessage', u.TextBlock).get_text()).strip()))
            self.check('shop_no_economy_controls', not any(token in item.get_name().lower()
                for item in self.controls() for token in ('price', 'currency', 'purchase', 'buybutton')))
            self.check('shop_blocks_move_and_look', self.pc.is_move_input_ignored() and self.pc.is_look_input_ignored())
            self.capture('07_EmptyShop_ActualVendingMachine.png'); self.stage('shop_hold')
        elif self.phase == 'shop_hold' and age >= 2.5:
            self.widget.set_user_focus(self.pc); self.stage('shop_escape_focus')
        elif self.phase == 'shop_escape_focus' and age >= .25:
            self.check('shop_has_focus_before_escape', self.widget.has_user_focus(self.pc) or self.widget.has_user_focused_descendants(self.pc))
            require(self.slate.call_method('PressKey', args=('Escape',)), 'Engine Slate Escape delivery was rejected')
            self.report.setdefault('input_requests', []).append(dict(method='USlateInspectorToolset.PressKey',
                key='Escape', target=path(self.widget), game_seconds=self.now()))
            self.stage('shop_escape_wait')
        elif self.phase == 'shop_escape_wait' and age >= .5:
            self.check('escape_closes_shop_not_pie', not self.pc.is_hub_shop_open())
            self.check('shop_close_releases_move_and_look', not self.pc.is_move_input_ignored() and not self.pc.is_look_input_ignored())
            self.capture('08_EscapeReturnsToHub.png')
            self.walk_start = self.hero.get_actor_location(); self.stage('shop_movement_restored')
        elif self.phase == 'shop_movement_restored':
            if age < .35:
                self.hero.add_movement_input(self.hero.get_actor_forward_vector(), .4, False)
                self.max_walk_speed = max(self.max_walk_speed, self.hero.get_velocity().length())
            elif age >= .7:
                self.hero.character_movement.stop_movement_immediately()
                self.check('normal_movement_resumes_after_shop', self.max_walk_speed > 5. or
                    (self.hero.get_actor_location()-self.walk_start).length() > 2.)
                if self.capture_hz and self.max_frames:
                    self.check('both_clips_have_native_frames', all(len(self.report['clips'].get(name, [])) >= 2
                        for name in ('WardrobeRGB', 'EmptyShop')))
                self.begin_finish()

    def tick(self, _delta):
        if self.done or self.busy:
            return
        self.busy = True
        try:
            if self.finishing:
                worlds = list(u.EditorLevelLibrary.get_pie_worlds(True))
                if not worlds and not self.levels.is_in_play_in_editor():
                    self.report['owned_pie_stopped'] = True; self.finish()
                elif time.monotonic()-self.phase_wall > 12.:
                    self.error = self.error or 'Owned PIE stop timed out'; self.finish()
                return
            require(time.monotonic()-self.started < 150., 'Scoped Wardrobe/Shop check exceeded 150 wall seconds')
            worlds = list(u.EditorLevelLibrary.get_pie_worlds(True))
            if self.phase == 'loading':
                require(time.monotonic()-self.started < 70., 'Saved Hub did not become ready within 70 seconds')
                self.initialize_world(worlds); return
            require(len(worlds) == 1 and valid(self.world) and worlds[0] == self.world,
                'Owned PIE ended, travelled or was replaced')
            require(valid(self.hero) and self.pc.get_controlled_pawn() == self.hero and path(self.hero) == self.pawn_path,
                'The subject pawn changed')
            current = time.monotonic()
            if self.clip and self.capture_hz and current >= self.frame_at:
                if len(self.report['frames']) < self.max_frames and self.frame_bytes < 700*1024*1024:
                    self.frame_at = current+1./self.capture_hz; self.capture()
                else:
                    self.report['capture_limit_reached'] = True
            self.advance()
        except Exception:
            self.begin_finish(traceback.format_exc())
        finally:
            self.busy = False

    def finish(self):
        if self.done:
            return
        try:
            changed = self.preferences.has_custom_bag_color() != self.before_custom or not same_color(
                self.preferences.get_saved_bag_color(), self.before_saved, .000001)
            if changed:
                # Save current non-appearance memory preferences first. Then
                # change ONLY the protected bool in the generated settings INI.
                # UE5.8 LoadSettings(true)->LoadConfigIni(true) explicitly forces
                # FConfigContext to reload disk, rather than reuse GConfig.
                other_names = ('master_volume', 'music_volume', 'ambience_volume', 'sfx_volume',
                    'language_code', 'mouse_sensitivity', 'gamepad_sensitivity', 'invert_y')
                other_before = {name: getattr(self.preferences, 'get_'+name)() for name in other_names}
                ini_before_save = {p: p.read_bytes() for p in self.config_files() if p.name == 'GameUserSettings.ini'}
                self.preferences.set_saved_bag_color(self.before_saved)
                self.preferences.save_settings()
                if self.preferences.has_custom_bag_color() != self.before_custom:
                    section = self.preferences.get_class().get_path_name()
                    candidates = [p for p in self.config_files() if p.name == 'GameUserSettings.ini'
                        and any(('['+section+']').encode(codec) in p.read_bytes()
                            for codec in ('utf-8', 'utf-16-le', 'utf-16-be'))]
                    # Prefer the file written by this SaveSettings call; accept
                    # the sole existing class-section match if no bytes changed.
                    written = [p for p in candidates if ini_before_save.get(p) != p.read_bytes()]
                    targets = written if written else candidates
                    require(len(targets) == 1, 'Cannot uniquely identify this task\'s generated GameUserSettings INI')
                    target = targets[0]
                    require(target.is_relative_to(self.config_root), 'Refusing to edit config outside the project-generated directory')
                    temporary_bytes = restore_ini_bool(target.read_bytes(), section, 'bHasCustomBagColor', self.before_custom)
                    target.write_bytes(temporary_bytes)
                    self.preferences.load_settings(True)
                    self.report['appearance_flag_restore'] = dict(method='Public SaveSettings, one INI bool, public LoadSettings(true)',
                        generated_ini=str(target), section=section, key='bHasCustomBagColor', value=self.before_custom,
                        force_reload_source='UGameUserSettings.cpp:603-622/700-743; MCGameUserSettings.cpp:121-135')
                require(all(getattr(self.preferences, 'get_'+name)() == value for name, value in other_before.items()),
                    'Restoring appearance unexpectedly changed unrelated memory preferences')
            self.check('original_custom_flag_restored', self.preferences.has_custom_bag_color() == self.before_custom)
            self.check('original_saved_rgb_restored', same_color(self.preferences.get_saved_bag_color(), self.before_saved, .000001))
            self.report['preferences_restored'] = dict(has_custom_color=self.preferences.has_custom_bag_color(),
                saved_color=rgba(self.preferences.get_saved_bag_color()))
        except Exception:
            self.restore_errors.append(traceback.format_exc())
        try:
            # Disk restoration still runs if the in-memory restoration fails.
            for filename in self.config_files()-set(self.config_before):
                require(filename.is_relative_to(self.config_root) and filename.name in ('GameUserSettings.ini', 'EditorSettings.ini'),
                    'Refusing to remove an out-of-scope config file')
                filename.unlink()  # Only these absent-before INIs created by this owned test.
            for filename, data in self.config_before.items():
                filename.parent.mkdir(parents=True, exist_ok=True); filename.write_bytes(data)
            self.report['config_bytes_restored'] = self.config_files() == set(self.config_before) and all(
                filename.read_bytes() == data for filename, data in self.config_before.items())
            require(self.report['config_bytes_restored'], 'Original affected INI bytes were not restored exactly')
        except Exception:
            self.restore_errors.append(traceback.format_exc())
        try:
            u.SystemLibrary.execute_console_command(self.editor_world, 'Slate.bAllowThrottling '+str(self.old_slate_throttling))
        except Exception:
            self.restore_errors.append(traceback.format_exc())
        super().finish()


def main():
    previous = getattr(builtins, KEY, None)
    require(previous is None or getattr(previous, 'done', True), 'This scoped probe is already active')
    prior = getattr(builtins, base.KEY, None)
    require(prior is None or getattr(prior, 'done', True), 'An owned Hub pier check is already active')
    probe = HubWardrobeShopSolo(); setattr(builtins, KEY, probe)
    u.EditorPythonScripting.set_keep_python_script_alive(True)
    try:
        probe.start()
    except Exception:
        probe.begin_finish(traceback.format_exc())
    return probe


if __name__ == '__main__':
    main()
