"""One native Status::Damage death/respawn check in an existing disposable solo Arena PIE.

Requires a ready saved L_Mouth, New UI, -MCHubTransientProfile, one local player,
available real reserve teeth and the approved Editor-only ApplyPIEToothDamage API.
The API calls Hero->Status->Damage(Amount) with ordinary/default perk gates.
This is native gameplay damage from a QA bridge, not hardware attack input.
Missing API or an uncertain preflight gate => SKIP before damage or input changes.

Run this recipe in a fresh Editor-Python namespace through remote execution WITHOUT
the generic runner's gc.collect(), e.g. compile/read/exec with __file__ set.
It does not start/stop PIE, travel, change preferences, save packages, set UI state,
alter health/dead/reserve/director/run flags, teleport, spawn actors or force GC.
Native hit SFX/VFX, death, reserve consumption and replacement-pawn possession run
unchanged. They are not rolled back; use a disposable transient-profile run.

Only this probe's Slate callback is registered. It unregisters on completion,
failure, cancel or <=30 seconds after the single damage call. Cancel with:
    builtins.mc_unified_hud_extras_solo.cancel()
PNG captures before/death/respawn and JSON go to drive E. Swim/drowning is deferred:
this probe does not invent a water stage, alter geometry or move the pawn.
"""
import builtins
import hashlib
import json
import math
import os
import sys
import time
import traceback
from pathlib import Path
import unreal as u

sys.dont_write_bytecode = True
os.environ["TEMP"] = os.environ["TMP"] = "E:/CodexTemp/MessControl-UIHubReward"
OUT = Path(globals().get("MC_HUD_EXTRAS_OUTPUT",
    "E:/CodexWork/MessControl/Validation/UICatch_20261104/Arena"))
assert OUT.resolve().drive.upper() == "E:", "Output must remain on drive E"
OUT.mkdir(parents=True, exist_ok=True)
KEY = "mc_unified_hud_extras_solo"
MAX_AFTER_DAMAGE = 30.0

class SkipProbe(Exception):
    pass

def path(obj):
    return obj.get_path_name() if obj is not None else None

def prop(obj, name):
    return obj.get_editor_property(name)

def tree(owner):
    for name in ("WidgetTree_0", "WidgetTree"):
        found = u.find_object(owner, name)
        if isinstance(found, u.WidgetTree) and found.get_outer() == owner:
            return found
    matches = [x for x in u.ObjectIterator(u.WidgetTree) if x.get_outer() == owner]
    assert len(matches) == 1, ("Missing/ambiguous owned WidgetTree", path(owner))
    return matches[0]

def named(owner, name, kind=None):
    owned = tree(owner)
    item = u.find_object(owned, name)
    assert isinstance(item, kind or u.Widget) and item.get_outer() == owned, (path(owner), name)
    return item

def visible(item):
    if item is None:
        return False
    child = item
    for _ in range(48):
        if not child.is_visible():
            return False
        parent = child.get_parent()
        if parent is None:
            return True
        if isinstance(parent, u.WidgetSwitcher) and parent.get_active_widget() != child:
            return False
        child = parent
    raise AssertionError("Unexpected widget ancestry cycle")

def read_text(owner, name):
    return str(named(owner, name, u.TextBlock).get_text())

def read_progress(owner, name):
    # UProgressBar::GetPercent is a native property Getter, not a UFUNCTION.
    item = named(owner, name)
    return float(prop(item, "percent" if isinstance(item, u.ProgressBar) else "progress"))

class HUDExtrasSolo:
    def __init__(self):
        self.callback = None
        self.world = self.pc = self.initial_hero = self.source = self.root = self.extras = self.gs = None
        self.damage_at = self.dead_at = self.respawn_at = None
        self.started = time.monotonic()
        self.last_sample = -1.0
        self.phase = "preflight"
        self.report = {
            "passed": False, "skipped": False, "phase": self.phase, "damageCalls": 0,
            "nativeDamageAccepted": None, "damagePipeline": "Editor-only ApplyPIEToothDamage -> Status::Damage ordinary/defaults",
            "samples": [], "captures": {}, "cleanup": {}, "profileWrite": False,
            "hardwareInput": False, "inputChangesByProbe": [], "uiSettersByProbe": [],
            "healthFlagReserveDirectorWrites": False, "forceGC": False, "maximumAfterDamageSeconds": MAX_AFTER_DAMAGE,
            "swimDrowning": {"status": "deferred", "reason": "No verified existing water stage/native input route; no geometry or pawn-pose changes."},
            "sourceScriptSHA256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        }

    def owned(self, cls, require_viewport=False, require_parent=False):
        matches = [x for x in u.ObjectIterator(cls) if x.get_world() == self.world
            and x.get_owning_player() == self.pc and (not require_viewport or x.is_in_viewport())
            and (not require_parent or x.get_parent() is not None)]
        assert len(matches) == 1, ("Missing/ambiguous actual runtime owner", str(cls), len(matches))
        return matches[0]

    def write(self):
        self.report.update(phase=self.phase, wallSeconds=time.monotonic() - self.started)
        (OUT / "HUDExtrasSoloReport.json").write_text(
            json.dumps(self.report, ensure_ascii=False, indent=2), encoding="utf-8")

    def capture(self, stage):
        capture = getattr(getattr(u, "MCGameplayUIEditorLibrary", None), "capture_pie_game_window", None)
        if not callable(capture):
            raise SkipProbe("Approved game-window capture API unavailable")
        filename = OUT / ("HUDExtras_" + stage + ".png")
        ok = bool(capture(self.world, str(filename)))
        self.report["captures"][stage] = {"path": str(filename), "captured": ok}
        assert ok, "Game-window capture failed: " + stage

    def gates(self):
        assert self.world in u.EditorLevelLibrary.get_pie_worlds(True), "Original owned PIE world ended"
        assert u.SystemLibrary.is_standalone(self.world), "Network play is outside this probe"
        assert u.GameplayStatics.get_current_level_name(self.world, True) == "L_Mouth"
        assert not u.GameplayStatics.is_game_paused(self.world), "Pause/reward cannot own input"
        assert self.pc.is_world_presentation_ready(), "Native location readiness barrier not released"
        assert u.MCGameplayUIFactory.get_presentation_version() == u.MCUIVersion.NEW, "Probe never changes UI version"
        assert not self.pc.is_boss_intro_playing() and not self.pc.is_reward_menu_open() and not self.pc.is_waiting_for_perk_team()
        assert not prop(self.gs, "bTutorialActive") and not prop(self.gs, "bLobbyWaiting")
        assert prop(self.gs, "phase") not in (u.MCShiftPhase.WON, u.MCShiftPhase.LOST), "Run already finished"
        active = named(self.root, "InterfaceScreens", u.WidgetSwitcher).get_active_widget()
        assert active and active.get_name() == "Screen_Gameplay" and visible(self.root), "New gameplay root must stay selected"
        assert self.source.get_parent() is not None and self.source.is_visible(), "Retained original HUD owner disappeared"
        assert path(self.source) == self.source_path and path(self.root) == self.root_path
        assert visible(self.extras), "New supplemental overlay hidden"
        # No native FGeometry Python readback, UI setters or unreflected snapshot calls.
        for actor in u.GameplayStatics.get_all_actors_of_class(self.world, u.MCBossCharacter):
            assert not actor.is_boss_alive(), ("Active living boss makes this probe unsafe", path(actor))
        return active.get_name()

    def snapshot(self):
        active = self.gates()
        hero = self.pc.get_controlled_pawn()
        assert isinstance(hero, u.MCToothCharacter), "Current pawn is not the actual source tooth character"
        status = hero.get_component_by_class(u.MCToothStatusComponent)
        assert status is not None
        state = prop(status, "state")
        health, maximum = float(prop(state, "health")), float(prop(state, "max_health"))
        hint, new_hint = read_text(self.source, "ActionHint"), read_text(self.root, "V2ActionHint")
        source_contact = read_progress(self.source, "ContactProgress")
        new_contact = read_progress(self.root, "V2ContactProgress")
        source_clean, new_clean = read_progress(self.source, "CleanProgress"), read_progress(self.root, "V2CleanBar")
        new_health = read_progress(self.root, "V2HealthBar")
        source_reserves, new_reserves = read_text(self.source, "ReserveValue"), read_text(self.root, "V2ReserveCount")
        source_texts = {name: read_text(self.source, name) for name in
            ("TimerLabel", "TimerValue", "TaskLabel", "TaskValue")}
        return {
            "wall": time.monotonic() - self.started, "game": u.GameplayStatics.get_time_seconds(self.world),
            "pawn": path(hero), "basicSourcePath": path(self.source), "sourceHUD": path(self.source), "root": path(self.root), "rootScreen": active,
            "version": str(u.MCGameplayUIFactory.get_presentation_version()), "alive": bool(status.is_alive()),
            "health": health, "maxHealth": maximum, "newHealthProgress": new_health,
            "healthMatchesPawn": math.isclose(new_health, health / max(1., maximum), abs_tol=.005),
            "sourceHint": hint, "newHint": new_hint, "hintMatches": hint == new_hint,
            "sourceContactProgress": source_contact, "newContactProgress": new_contact,
            "contactMatches": math.isclose(source_contact, new_contact, abs_tol=.005),
            "sourceCleanProgress": source_clean, "newCleanProgress": new_clean,
            "cleanMatches": math.isclose(source_clean, new_clean, abs_tol=.005),
            "sourceReserveText": source_reserves, "newReserveText": new_reserves,
            "reserveMatches": source_reserves.split("/", 1)[0].strip() == new_reserves.strip(),
            "availableRealTeeth": int(self.gs.available_arena_teeth()), "spectating": bool(self.pc.is_spectating()),
            "spectatorTarget": path(self.pc.get_spectator_target()), "respawnRemaining": float(self.pc.get_respawn_seconds_remaining()),
            "sourceTexts": source_texts, "runPhase": str(prop(self.gs, "phase")),
            "mouthHealth": float(prop(self.gs, "mouth_health")), "tasksLeft": int(prop(self.gs, "tasks_left")),
            "tasksTotal": int(prop(self.gs, "tasks_total")), "runLoadId": prop(self.gs, "gameplay_load_id").to_string(),
        }

    def start(self):
        # All uncertainty is resolved before the one approved native call.
        assert "-mchubtransientprofile" in u.SystemLibrary.get_command_line().lower()
        worlds = u.EditorLevelLibrary.get_pie_worlds(True)
        assert len(worlds) == 1 and u.SystemLibrary.is_standalone(worlds[0]), "Requires exactly one existing solo PIE world"
        self.world = worlds[0]
        assert u.GameplayStatics.get_current_level_name(self.world, True) == "L_Mouth"
        self.pc = u.GameplayStatics.get_player_controller(self.world, 0)
        assert isinstance(self.pc, u.MCPlayerController) and self.pc.is_local_controller()
        assert len(u.GameplayStatics.get_all_actors_of_class(self.world, u.MCPlayerController)) == 1
        self.gs = u.GameplayStatics.get_game_state(self.world)
        assert isinstance(self.gs, u.MCGameState)
        self.source = self.owned(u.MCGameplayHUD, require_parent=True)
        self.root = self.owned(u.MCUnifiedUIWidget, require_viewport=True)
        self.extras = named(self.root, "UnifiedHUDExtras", u.MCUnifiedHUDExtrasWidget)
        self.source_path, self.root_path = path(self.source), path(self.root)
        self.initial_hero = self.pc.get_controlled_pawn()
        assert isinstance(self.initial_hero, u.MCToothCharacter)
        assert not self.pc.is_move_input_ignored() and not self.pc.is_look_input_ignored()
        # These are readonly checks. The probe owns no held input and changes none.
        for owner_class in (u.MCGameplayMenuWidget, u.MCPerkChoiceWidget, u.MCEmoteWidget, u.MCScoreboardWidget):
            for owner in u.ObjectIterator(owner_class):
                if owner.get_world() == self.world and owner.get_owning_player() == self.pc and owner.is_in_viewport():
                    assert not visible(owner), ("Another player-facing panel owns the run", path(owner))
        for name in ("ExtrasFlowBounds", "ExtrasFlowScroll", "ExtrasFlowRows", "ExtrasResultsBounds",
            "ExtrasSwimBounds", "ExtrasBoosterBounds", "ExtrasPassivesBounds", "ExtrasCataclysmBounds",
            "ExtrasResultTitle", "ExtrasResultDetail", "ExtrasSwimCaption", "ExtrasSwimValue", "ExtrasCataclysmInstruction"):
            named(self.extras, name)
        assert not str(prop(named(self.extras, "ExtrasSwimProgress", u.MCUniversalUIWidget), "label")), "Default panel label leaked onto supplemental progress shape"
        self.report["extrasReadyEvidence"] = {
            "basis": "Native world-ready barrier + visible actual New overlay + actual saved shape/control nodes",
            "preparedFlagDirectRead": False, "reason": "bPrepared/typed snapshot are native-only; no fabricated Python access",
            "extras": path(self.extras)}
        before = self.snapshot()
        self.report["before"] = before
        assert before["alive"] and before["health"] > 0 and before["availableRealTeeth"] > 0
        assert before["healthMatchesPawn"] and before["hintMatches"] and before["contactMatches"] and before["cleanMatches"] and before["reserveMatches"], "Wait for actual UI source/New cadence to settle"
        delay = float(prop(u.GameplayStatics.get_game_mode(self.world), "respawn_delay"))
        assert .5 <= delay <= 20, "Actual native respawn delay outside observable 30-second scope"
        bridge = getattr(getattr(u, "MCGameplayUIEditorLibrary", None), "apply_pie_tooth_damage", None)
        if not callable(bridge):
            raise SkipProbe("ApplyPIEToothDamage Editor API unavailable; generic ApplyDamage is not a verified player Status route")
        assert callable(getattr(u.MCGameplayUIEditorLibrary, "capture_pie_game_window", None))
        amount = min(100000., max(before["maxHealth"] * 100., before["health"] + 1000.))
        assert math.isfinite(amount) and 0 < amount <= 100000.
        self.report["inputBefore"] = {"moveIgnored": self.pc.is_move_input_ignored(), "lookIgnored": self.pc.is_look_input_ignored(),
            "primaryHeld": "native-only; not read through Python", "policy": "No actor/input mutations, so no input restore is required"}
        self.report["nativeRespawnDelay"] = delay
        self.capture("Before")
        self.phase = "observe_one_native_damage"
        self.write()
        self.callback = u.register_slate_post_tick_callback(self.tick)
        self.damage_at = time.monotonic()
        self.report["damageCalls"] = 1  # No retries, even if a perk rejects or intercepts lethal damage.
        self.report["nativeDamageAmount"] = amount
        self.report["nativeDamageAccepted"] = bool(bridge(self.initial_hero, amount))
        if not self.report["nativeDamageAccepted"]:
            raise AssertionError("Ordinary native damage rejected; preserve all source gates, do not retry")
        self.write()
        u.log("MC_UNIFIED_HUD_EXTRAS_SOLO_STARTED")

    def tick(self, _delta):
        try:
            now = time.monotonic()
            assert self.damage_at is not None
            assert now - self.damage_at <= MAX_AFTER_DAMAGE, "Native death/respawn observation exceeded 30 seconds"
            if now - self.last_sample < .12:
                return
            self.last_sample = now
            sample = self.snapshot()
            self.report["samples"].append(sample)
            if not sample["alive"]:
                if self.dead_at is None:
                    self.dead_at = now
                    self.report["nativeDeath"] = sample
                # Wait for ordinary source and New UI cadence, rather than editing either.
                if now - self.dead_at >= .18 and sample["hintMatches"] and sample["healthMatchesPawn"] and sample["sourceHint"]:
                    assert sample["sourceHint"] != self.report["before"]["sourceHint"], "Death did not reach native contextual hint"
                    if "Death" not in self.report["captures"]:
                        self.report["deathHint"] = sample
                        self.capture("Death")
            elif self.dead_at is None:
                assert now - self.damage_at < 1.0, "One ordinary hit did not cause death (possible native perk mitigation); no retry"
            elif sample["pawn"] != self.report["before"]["pawn"]:
                if self.respawn_at is None:
                    self.respawn_at = now
                settled = sample["healthMatchesPawn"] and sample["hintMatches"] and sample["contactMatches"] and sample["cleanMatches"] and sample["reserveMatches"]
                if settled and now - self.respawn_at >= .3:
                    assert "Death" in self.report["captures"], "Actual death hint/capture was not observed"
                    assert sample["availableRealTeeth"] == self.report["before"]["availableRealTeeth"] - 1, "Expected one native reserve consumption; no reserve repair"
                    assert sample["sourceHUD"] == self.report["before"]["sourceHUD"] and sample["root"] == self.report["before"]["root"]
                    assert sample["runLoadId"] == self.report["before"]["runLoadId"], "Run changed while observing respawn"
                    self.report["after"] = sample
                    self.report["spectatorCase"] = ("observed" if any(x["spectating"] for x in self.report["samples"])
                        else "not applicable: native solo source had no living teammate target; actual respawn hint verified")
                    self.capture("Respawn")
                    self.report["passed"] = True
                    self.finish()
                    return
            self.write()
        except Exception:
            self.finish(traceback.format_exc())

    def cancel(self):
        self.finish("Cancelled by the owning QA author; native damage/respawn is left running")

    def finish(self, error=None, skipped=False):
        if self.callback is not None:
            u.unregister_slate_post_tick_callback(self.callback)
            self.callback = None
        if error:
            self.report.update(passed=False, skipped=skipped, error=error)
        self.report["cleanup"] = {
            "callbackUnregistered": True, "actorInputChanged": False, "uiStateChanged": False,
            "gameplayRestore": "None: accepted native damage, death, reserve and respawn stay owned by the disposable run",
            "profileWrite": False, "PIEStopped": False, "GCForced": False}
        self.phase = "done"
        self.write()
        u.log("MC_UNIFIED_HUD_EXTRAS_SOLO_DONE " + str(self.report["passed"]))
        if getattr(builtins, KEY, None) is self:
            delattr(builtins, KEY)
        for name in ("world", "pc", "initial_hero", "source", "root", "extras", "gs"):
            setattr(self, name, None)

assert not hasattr(builtins, KEY), "Another owned HUD Extras probe is active"
probe = HUDExtrasSolo()
setattr(builtins, KEY, probe)
try:
    probe.start()
except SkipProbe as exc:
    probe.finish(str(exc), skipped=True)
except Exception:
    probe.finish(traceback.format_exc(), skipped=probe.report["damageCalls"] == 0)
