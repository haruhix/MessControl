"""Owned two-player check of cards, shared XP, landing cues and nut combat replication.

Run through remote_python.py after author_single_day.py, in an idle editor on
L_Mouth. This skips teaching by setting only the server tutorial's reflected
Stage=Complete: its native Tick broadcasts the real FinishTutorial handoff.
Actual teaching/tools are covered independently by MCSingleDaySmoke.

The real card button's native Choose callback runs on the next client-world
tick, after the editor Python guard, and sends ServerChooseLevelPerk over the
network. This helper creates no source/content changes, ends only its own PIE,
restores play settings/CVars, and writes Saved/SingleDayNetwork/Validation.json.
"""
import builtins
import json
import time
import traceback
from pathlib import Path

import unreal as u

KEY = "_mc_single_day_network_validation"


def prop(obj, python_name, native_name=None):
    try:
        return obj.get_editor_property(python_name)
    except Exception:
        # Non-Blueprint UPROPERTY fields have no generated snake_case alias.
        reflected_name = native_name or "".join(part[:1].upper()+part[1:] for part in python_name.split("_"))
        return obj.get_editor_property(reflected_name)


class SingleDayNetworkValidation:
    def __init__(self):
        self.levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
        if self.levels.is_in_play_in_editor() or u.EditorLevelLibrary.get_pie_worlds(True):
            raise RuntimeError("An existing PIE is active; leave it untouched and finish it before this owned test")
        if "-MCSingleDayTest" in u.SystemLibrary.get_command_line():
            raise RuntimeError("Use an editor without the native MCSingleDaySmoke flag for this independent network check")
        editor_world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
        if "/Game/Maps/L_Mouth" not in editor_world.get_path_name():
            raise RuntimeError("Open the saved L_Mouth map before starting this check; the helper does not change maps")
        mode_class = u.load_class(None, "/Game/Blueprints/BP_MouthGameMode.BP_MouthGameMode_C")
        if not mode_class or not prop(u.get_default_object(mode_class), "use_single_day_loop", "bUseSingleDayLoop"):
            raise RuntimeError("The saved gameplay mode has not enabled the single-day loop; run author_single_day.py")
        self.play_settings = u.load_object(None, "/Script/UnrealEd.Default__LevelEditorPlaySettings")
        # EPlayNetMode is not exported to Python in UE 5.8. Read/write its real
        # reflected enum name through Epic's toolset serializer, also preserving it.
        settings = dict(playNumberOfClients=2, playNetMode="PIE_ListenServer",
                        runUnderOneProcess=True, bLaunchSeparateServer=False)
        self.previous_settings = json.loads(u.ToolsetLibrary.get_object_properties(self.play_settings, list(settings)))
        self.new_settings = settings
        self.previous_idle = u.SystemLibrary.get_console_variable_int_value("t.IdleWhenNotForeground")
        self.guid_library = u.get_default_object(u.GuidLibrary)
        self.started = time.monotonic()
        self.phase_started = self.started
        self.phase = "waiting_worlds"
        self.callback = None
        self.server_world = self.client_world = None
        self.gs = self.progression = self.director = None
        self.host_pc = self.remote_pc = None
        self.host_id = self.remote_id = None
        self.original_offers = {}
        self.initial_stacks = {}
        self.first_experience = 0
        self.team_tool_mask = 0
        self.target_level = 2
        self.selected = {}
        self.second_offers = {}
        self.landing_shadow_observed = False
        self.combat_observed = set()
        self.failure = None
        self.path = Path(u.Paths.project_saved_dir()) / "SingleDayNetwork" / "Validation.json"
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.report = dict(status="running", passed=False,
                           scope="two real PIE network peers; shared XP, owner-only cards, real client choice RPC, landing shadows and naturally scheduled nut bosses/cues/fireball",
                           teaching_scope="native handoff exercised; teaching deliberately skipped in this network fixture",
                           selection_route="existing card Choose UFUNCTION scheduled on next client world tick; no reflected RPC call",
                           adversarial_rpc_scope="foreign/stale offer validation covered by native progression tests; Python guard forces reflected RPCs local",
                           previous_play_settings=self.previous_settings,
                           previous_idle_cvar=self.previous_idle,
                           checks={}, snapshots=[])

    def write(self):
        self.path.write_text(json.dumps(self.report, ensure_ascii=False, indent=2), encoding="utf-8")

    def require(self, condition, message):
        if not condition:
            raise RuntimeError(message)

    def check(self, key, condition, message):
        self.require(condition, message)
        self.report["checks"][key] = True

    def enter(self, phase):
        self.phase = phase
        self.phase_started = time.monotonic()
        self.report["phase"] = phase
        self.write()

    def start(self):
        self.require(u.ToolsetLibrary.set_object_properties(self.play_settings, json.dumps(self.new_settings)),
                     "Could not configure two in-process listen-server peers")
        u.SystemLibrary.execute_console_command(
            u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world(), "t.IdleWhenNotForeground 0")
        self.callback = u.register_slate_post_tick_callback(self.tick)
        self.levels.editor_request_begin_play()
        self.write()
        u.log("MC_SINGLE_DAY_NETWORK_START " + str(self.path))

    def guid(self, value):
        return self.guid_library.call_method("Conv_GuidToString", args=(value,))

    def valid_guid(self, value):
        return self.guid_library.call_method("IsValid_Guid", args=(value,))

    def player_map(self, world):
        gs = u.GameplayStatics.get_game_state(world)
        return {prop(ps, "player_id"): ps for ps in prop(gs, "player_array") if isinstance(ps, u.MCPlayerState)}

    def progression_tuple(self, world):
        progression = prop(u.GameplayStatics.get_game_state(world), "progression")
        return tuple(prop(progression, name) for name in ("team_level", "total_experience", "experience_in_level", "experience_to_next_level"))

    def offer(self, player):
        value = prop(player, "level_up_offer")
        return dict(id=self.guid(prop(value, "offer_id")), valid=self.valid_guid(prop(value, "offer_id")),
                    level=prop(value, "team_level"), cards=[str(v) for v in prop(value, "perk_ids", "PerkIDs")],
                    pending=prop(player, "pending_level_choices"))

    def stacks(self, player):
        return {str(prop(value, "perk_id", "PerkID")): prop(value, "stacks")
                for value in prop(prop(player, "perks"), "active_perks")}

    def landing_shadow_samples(self, world):
        samples = []
        for shadow in u.GameplayStatics.get_all_actors_of_class(world, u.MCNutLandingShadow):
            try:
                # Short-lived actors and their tongue references use separate
                # channels. Skip a disappearing/incomplete sample and retry on
                # subsequent ticks rather than retaining UObject handles.
                if not prop(shadow, "tongue"):
                    continue
                anchor = prop(shadow, "surface_anchor")
                samples.append(dict(actor=shadow.get_path_name(), authority=shadow.has_authority(),
                                    anchor=[anchor.x, anchor.y, anchor.z],
                                    impact_at=prop(shadow, "impact_at"), radius=prop(shadow, "radius"),
                                    opacity=prop(shadow, "max_opacity"),
                                    active=shadow.is_warning_active(), strength=shadow.get_shadow_strength()))
            except RuntimeError:
                continue
        return samples

    def observe_landing_shadows(self):
        if self.landing_shadow_observed or not self.director or prop(self.director, "stage") != u.MCSingleDayStage.NUTS:
            return
        servers = self.landing_shadow_samples(self.server_world)
        clients = self.landing_shadow_samples(self.client_world)
        for server in servers:
            if not (server["authority"] and server["active"] and server["strength"] > 0):
                continue
            for client in clients:
                # Match replicated identity payload rather than PIE object paths,
                # whose world prefixes and dynamic actor names can differ.
                matches = (not client["authority"] and client["active"] and client["strength"] > 0
                           and all(abs(a-b) < .1 for a, b in zip(server["anchor"], client["anchor"]))
                           and abs(server["impact_at"]-client["impact_at"]) < .001
                           and abs(server["radius"]-client["radius"]) < .01
                           and abs(server["opacity"]-client["opacity"]) < .0001)
                if not matches:
                    continue
                self.check("landing_shadow_authoritative_and_remote", True, "")
                self.check("landing_shadow_replicated_payload_matches", True, "")
                self.check("landing_shadow_active_positive_strength_both_peers", True, "")
                self.report["landing_shadow_sample"] = dict(server=server, remote=client)
                self.landing_shadow_observed = True
                self.write()
                return

    def snapshot(self, label):
        item = dict(label=label, worlds=[])
        for world in (self.server_world, self.client_world):
            gs = u.GameplayStatics.get_game_state(world)
            item["worlds"].append(dict(world=world.get_path_name(), authority=gs.has_authority(),
                                        progression=self.progression_tuple(world),
                                        players={str(pid): dict(offer=self.offer(ps), stacks=self.stacks(ps))
                                                 for pid, ps in self.player_map(world).items()}))
        self.report["snapshots"].append(item)
        self.write()

    @staticmethod
    def vector(value):
        return [value.x, value.y, value.z]

    def combat_samples(self, world):
        result = dict(bosses=[], cues=[], projectiles=[])
        for boss in u.GameplayStatics.get_all_actors_of_class(world, u.MCNutBoss):
            settings = prop(boss, "boss_settings")
            result["bosses"].append(dict(role=str(prop(boss, "boss_role")), state=str(prop(boss, "state")),
                attack=str(prop(boss, "attack")), health=prop(boss, "health"),
                max_health=prop(prop(boss, "settings"), "max_health"),
                tank_health=prop(settings, "tank_health"), mage_health=prop(settings, "mage_health"),
                extra_health=prop(settings, "extra_player_health"),
                state_started_at=prop(boss, "state_started_at"),
                origin=self.vector(prop(boss, "locked_start")), target=self.vector(prop(boss, "locked_target")),
                authority=boss.has_authority()))
        for effect in u.GameplayStatics.get_all_actors_of_class(world, u.MCNutCombatEffect):
            value = prop(effect, "cue")
            result["cues"].append(dict(role=str(prop(value, "role")), type=str(prop(value, "type")),
                started_at=prop(value, "started_at"), origin=self.vector(prop(value, "origin")),
                target=self.vector(prop(value, "target")), radius=prop(value, "radius"),
                windup=prop(value, "windup_seconds"), active=prop(value, "active_seconds"),
                detail_radius=prop(value, "detail_radius"), seed=prop(value, "seed"),
                drops=prop(value, "drop_count"), cadence=prop(value, "drop_cadence"), authority=effect.has_authority()))
        for shot in u.GameplayStatics.get_all_actors_of_class(world, u.MCNutSpellProjectile):
            flight, impact = prop(shot, "flight"), prop(shot, "impact")
            result["projectiles"].append(dict(start=self.vector(prop(flight, "start")), target=self.vector(prop(flight, "target")),
                started_at=prop(flight, "started_at"), seconds=prop(flight, "seconds"), radius=prop(flight, "radius"),
                impacted=prop(impact, "impacted", "bImpacted"), impact_point=self.vector(prop(impact, "point")),
                impact_normal=self.vector(prop(impact, "normal")), impact_at=prop(impact, "at"),
                damage_applications=prop(shot, "damage_applications"), authority=shot.has_authority()))
        return result

    @staticmethod
    def payload_matches(server, remote, keys):
        for key in keys:
            a, b = server[key], remote[key]
            if isinstance(a, list):
                if not all(abs(x-y) < .1 for x, y in zip(a, b)):
                    return False
            elif isinstance(a, (int, float)):
                if abs(a-b) > .001:
                    return False
            elif a != b:
                return False
        return server["authority"] and not remote["authority"]

    def observe_combat(self):
        server, remote = self.combat_samples(self.server_world), self.combat_samples(self.client_world)
        changed = False
        expected_roles = {str(u.MCNutBossRole.TANK), str(u.MCNutBossRole.MAGE)}
        if "boss_roles_settings" not in self.combat_observed and {b["role"] for b in server["bosses"]} == expected_roles:
            pairs = [(a, b) for a in server["bosses"] for b in remote["bosses"]
                     if self.payload_matches(a, b, ("role", "max_health", "tank_health", "mage_health", "extra_health"))]
            if len(pairs) == 2 and all(a["health"] > 0 and b["health"] > 0 for a, b in pairs):
                self.check("two_live_boss_roles_replicate", True, "")
                self.check("boss_party_health_settings_replicate", all(abs(a["max_health"]-
                           (a["tank_health"] if a["role"] == str(u.MCNutBossRole.TANK) else a["mage_health"])*(1+a["extra_health"])) < .01
                           for a, _ in pairs), "The two-player health scaling differs from replicated boss settings")
                self.report["boss_replication_sample"] = dict(server=server["bosses"], remote=remote["bosses"])
                self.combat_observed.add("boss_roles_settings"); changed = True
        if "boss_state" not in self.combat_observed:
            for a in server["bosses"]:
                if a["attack"] == str(u.MCNutBossAttack.NONE) or a["state"] != str(u.MCNutBossState.TELEGRAPH):
                    continue
                for b in remote["bosses"]:
                    if self.payload_matches(a, b, ("role", "state", "attack", "state_started_at", "origin", "target")):
                        self.check("boss_attack_timeline_and_locked_points_replicate", True, "")
                        self.report["boss_state_sample"] = dict(server=a, remote=b)
                        self.combat_observed.add("boss_state"); changed = True
                        break
        cue_keys = ("role", "type", "started_at", "origin", "target", "radius", "windup", "active", "detail_radius", "seed", "drops", "cadence")
        for a in server["cues"]:
            category = ("tank_cue" if a["type"] == str(u.MCNutCombatCue.CHARGE_TELL)
                        and a["role"] == str(u.MCNutBossRole.TANK) else
                        "mage_cue" if a["type"] == str(u.MCNutCombatCue.FIRE_CAST) else None)
            if category is None or category in self.combat_observed:
                continue
            for b in remote["cues"]:
                if self.payload_matches(a, b, cue_keys):
                    self.check(category + "_immutable_payload_replicates", True, "")
                    self.report[category + "_sample"] = dict(server=a, remote=b)
                    self.combat_observed.add(category); changed = True
                    break
        for a in server["projectiles"]:
            for b in remote["projectiles"]:
                if not self.payload_matches(a, b, ("started_at", "start", "target", "seconds", "radius")):
                    continue
                if "flight" not in self.combat_observed:
                    self.check("fireball_locked_flight_replicates", a["seconds"] > 0 and a["radius"] > 0,
                               "The authoritative fireball has invalid flight metadata")
                    self.check("remote_fireball_never_applies_damage", b["damage_applications"] == 0,
                               "The cosmetic remote fireball applied gameplay damage")
                    self.report["fireball_flight_sample"] = dict(server=a, remote=b)
                    self.combat_observed.add("flight"); changed = True
                if a["impacted"] and b["impacted"] and "impact" not in self.combat_observed and self.payload_matches(a, b, ("impact_point", "impact_normal", "impact_at")):
                    self.check("fireball_terminal_impact_payload_replicates", True, "")
                    self.check("one_server_fireball_damage_budget", a["damage_applications"] <= 1 and b["damage_applications"] == 0,
                               "A terminal fireball exceeded its authority-only contact budget")
                    self.report["fireball_impact_sample"] = dict(server=a, remote=b)
                    self.combat_observed.add("impact"); changed = True
        if changed:
            self.report["combat_observed"] = sorted(self.combat_observed)
            self.write()
        return {"boss_roles_settings", "boss_state", "tank_cue", "mage_cue", "flight", "impact"}.issubset(self.combat_observed)

    def local_controller(self, world):
        controllers = [pc for pc in u.GameplayStatics.get_all_actors_of_class(world, u.MCPlayerController)
                       if pc.is_local_controller()]
        self.require(len(controllers) == 1, "Each peer must own exactly one local gameplay controller")
        return controllers[0]

    def menu_has_three(self, controller):
        widget = prop(controller, "perk_choice_widget")
        if not (controller.is_reward_menu_open() and bool(widget) and widget.is_in_viewport()):
            return False
        return len(self.card_rows(widget)) == 1

    def card_rows(self, widget):
        # Native WidgetTree/Cards/Buttons are private to Python reflection.
        # Resolve this owner's live card row by UObject outer, then count its
        # attached children (old detached cards may still await GC).
        return [row for row in u.ObjectIterator(u.HorizontalBox)
                if row.get_typed_outer(u.MCPerkChoiceWidget) == widget
                and row.get_children_count() == 3
                and all(row.get_child_at(index).get_class().get_path_name()
                        == "/Script/MessControl.MCPerkCardButton" for index in range(3))]

    def choose_card_next_tick(self, controller, index):
        self.require(self.menu_has_three(controller), "The owner has no playable card row")
        widget = prop(controller, "perk_choice_widget")
        row = self.card_rows(widget)[0]
        button = row.get_child_at(index)
        # A direct Python ProcessEvent uses FEditorScriptExecutionGuard, which
        # forces Actor RPC callspace to Local. A world timer invokes native
        # Choose after that guard exits, taking the real client/server route.
        u.SystemLibrary.set_timer_for_next_tick(button, "Choose")
        return button

    def offers_ready(self, level):
        players = self.player_map(self.server_world)
        if set(players) != {self.host_id, self.remote_id}:
            return False
        if not all(self.offer(ps)["valid"] and self.offer(ps)["level"] == level for ps in players.values()):
            return False
        clients = self.player_map(self.client_world)
        if set(clients) != {self.host_id, self.remote_id}:
            return False
        # GameState and PlayerState travel on separate channels; wait for both payloads.
        return (self.offer(clients[self.remote_id]) == self.offer(players[self.remote_id])
                and self.progression_tuple(self.server_world) == self.progression_tuple(self.client_world))

    def verify_offers(self, key):
        server_players = self.player_map(self.server_world)
        client_players = self.player_map(self.client_world)
        offers = {pid: self.offer(ps) for pid, ps in server_players.items()}
        self.check(key + "_three_unique", all(len(v["cards"]) == len(set(v["cards"])) == 3 for v in offers.values()),
                   "Every personal offer must contain three distinct cards")
        self.check(key + "_personal_tokens", len({v["id"] for v in offers.values()}) == 2,
                   "Different owners must receive different offer tokens")
        # The card IDs may coincide by chance; token ownership and private replication distinguish offers.
        self.check(key + "_owner_replication", self.offer(client_players[self.remote_id]) == offers[self.remote_id],
                   "The remote client did not receive its own exact server offer")
        hidden = self.offer(client_players[self.host_id])
        self.check(key + "_other_offer_private", not hidden["cards"] and not hidden["valid"],
                   "The remote client received another player's private card payload")
        self.check(key + "_shared_progression", self.progression_tuple(self.server_world) == self.progression_tuple(self.client_world),
                   "Shared team level/XP differ between server and client")
        self.check(key + "_both_widgets", self.menu_has_three(self.host_pc) and self.menu_has_three(self.remote_pc),
                   "Both local owners must render exactly three playable cards")
        return offers

    def finish(self, failure=None):
        self.failure = failure
        self.report["status"] = "stopping"
        if failure:
            self.report["error"] = failure
        self.enter("stopping")
        # This helper refused an existing session and records the actual worlds it created.
        if self.levels.is_in_play_in_editor():
            worlds = u.EditorLevelLibrary.get_pie_worlds(True)
            if self.server_world is None or self.server_world in worlds:
                self.levels.editor_request_end_play()
            else:
                self.failure = self.failure or "Owned PIE was replaced; the replacement was left running"

    def restore(self):
        if self.callback is not None:
            u.unregister_slate_post_tick_callback(self.callback)
            self.callback = None
        errors = []
        try:
            if not u.ToolsetLibrary.set_object_properties(self.play_settings, json.dumps(self.previous_settings)):
                errors.append("The toolset serializer rejected restoring PIE settings")
            restored = json.loads(u.ToolsetLibrary.get_object_properties(self.play_settings, list(self.previous_settings)))
            self.report["restored_play_settings"] = restored
            if restored != self.previous_settings:
                errors.append("PIE settings differ after restoration: " + json.dumps(restored))
        except Exception:
            errors.append(traceback.format_exc())
        u.SystemLibrary.execute_console_command(
            u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world(),
            "t.IdleWhenNotForeground " + str(self.previous_idle))
        self.report["restored_idle_cvar"] = u.SystemLibrary.get_console_variable_int_value("t.IdleWhenNotForeground")
        if self.report["restored_idle_cvar"] != self.previous_idle:
            errors.append("Could not restore t.IdleWhenNotForeground")
        if errors:
            self.failure = self.failure or "Could not restore all PIE settings"
            self.report["restore_errors"] = errors
        self.report.update(status="failed" if self.failure else "passed", passed=not bool(self.failure),
                           restored_settings=not bool(errors), elapsed_seconds=time.monotonic()-self.started)
        if self.failure:
            self.report["error"] = self.failure
        self.write()
        u.log(("MC_SINGLE_DAY_NETWORK_FAIL " if self.failure else "MC_SINGLE_DAY_NETWORK_PASS ") + str(self.path))
        if getattr(builtins, KEY, None) is self:
            delattr(builtins, KEY)

    def tick(self, _delta):
        try:
            now = time.monotonic()
            if self.phase == "stopping":
                worlds = u.EditorLevelLibrary.get_pie_worlds(True)
                if not worlds or (self.server_world is not None and self.server_world not in worlds):
                    self.restore()
                elif now-self.phase_started > 15:
                    self.failure = self.failure or "Owned PIE did not stop within 15 seconds"
                    self.restore()
                return
            self.require(now-self.started < 120, "Network check timed out in phase " + self.phase)
            if self.server_world and self.client_world and self.director:
                self.observe_landing_shadows()
            worlds = u.EditorLevelLibrary.get_pie_worlds(False)
            if self.phase == "waiting_worlds":
                pairs = [(w, u.GameplayStatics.get_game_state(w)) for w in worlds]
                pairs = [(w, gs) for w, gs in pairs if isinstance(gs, u.MCGameState)]
                servers = [(w, gs) for w, gs in pairs if gs.has_authority()]
                clients = [(w, gs) for w, gs in pairs if not gs.has_authority()]
                if len(servers) != 1 or len(clients) != 1:
                    return
                self.server_world, self.gs = servers[0]
                self.client_world = clients[0][0]
                if len(self.player_map(self.server_world)) != 2 or len(self.player_map(self.client_world)) != 2:
                    return
                self.require(prop(self.gs, "single_day_loop", "bSingleDayLoop"), "PIE did not enable the authored single-day loop")
                self.host_pc = self.local_controller(self.server_world)
                self.remote_pc = self.local_controller(self.client_world)
                self.host_id = prop(prop(self.host_pc, "player_state"), "player_id")
                self.remote_id = prop(prop(self.remote_pc, "player_state"), "player_id")
                self.require(self.host_id != self.remote_id and not self.remote_pc.has_authority(), "The second controller is not a real remote peer")
                self.progression = prop(self.gs, "progression")
                self.director = prop(self.gs, "single_day_director")
                self.first_experience = self.progression.get_experience_to_next_level()
                self.team_tool_mask = prop(self.gs, "team_tool_upgrades")
                self.initial_stacks = {pid: self.stacks(ps) for pid, ps in self.player_map(self.server_world).items()}
                self.enter("complete_teaching")
            elif self.phase == "complete_teaching":
                tutorials = u.GameplayStatics.get_all_actors_of_class(self.server_world, u.MCTutorialDirector)
                if len(tutorials) != 1:
                    return
                tutorial = tutorials[0]
                learners = prop(tutorial, "players")
                if len(learners) != 2 or not all(prop(p, "loaded", "bLoaded") for p in learners):
                    return
                self.check("real_two_clients_loaded", True, "")
                # FinishTutorial is native, not reflected. Its real bound delegate is invoked by Tick.
                tutorial.set_editor_property("stage", u.MCTutorialStage.COMPLETE)
                self.enter("first_offers")
            elif self.phase == "first_offers":
                if not self.offers_ready(2) or not self.menu_has_three(self.host_pc) or not self.menu_has_three(self.remote_pc):
                    return
                self.original_offers = self.verify_offers("first")
                self.check("real_training_handoff", prop(self.director, "stage") == u.MCSingleDayStage.FIRST_PERK
                           and prop(self.progression, "team_level") == 2
                           and prop(self.progression, "total_experience") == self.first_experience,
                           "The native teaching handoff must award exactly the initial shared level")
                self.snapshot("first_three_cards_both_owners")
                self.selected[self.remote_id] = self.original_offers[self.remote_id]["cards"][1]
                self.remote_card = self.choose_card_next_tick(self.remote_pc, 1)
                self.enter("remote_choice")
            elif self.phase == "remote_choice":
                server = self.player_map(self.server_world)
                client = self.player_map(self.client_world)
                selected = self.selected[self.remote_id]
                if prop(server[self.remote_id], "pending_level_choices") != 0 or selected not in self.stacks(client[self.remote_id]):
                    return
                if self.remote_pc.is_reward_menu_open():
                    return
                self.check("remote_rpc_grants_personally", self.stacks(server[self.remote_id]).get(selected, 0)
                           == self.initial_stacks[self.remote_id].get(selected, 0)+1,
                           "The actual remote RPC did not grant exactly one personal perk")
                self.check("host_choice_independent", prop(server[self.host_id], "pending_level_choices") == 1
                           and self.stacks(server[self.host_id]) == self.initial_stacks[self.host_id]
                           and prop(self.director, "stage") == u.MCSingleDayStage.FIRST_PERK,
                           "One remote choice must not consume the host choice or release the initial gate")
                self.check("remote_menu_closed", not self.remote_pc.is_reward_menu_open(), "The accepted remote menu remained open")
                self.snapshot("remote_claim_host_still_pending")
                u.SystemLibrary.set_timer_for_next_tick(self.remote_card, "Choose")
                self.enter("duplicate_choice")
            elif self.phase == "duplicate_choice" and now-self.phase_started > .7:
                server = self.player_map(self.server_world)
                selected = self.selected[self.remote_id]
                self.check("closed_card_no_double_grant", self.stacks(server[self.remote_id]).get(selected, 0)
                           == self.initial_stacks[self.remote_id].get(selected, 0)+1,
                           "A closed card callback granted another perk")
                self.selected[self.host_id] = self.original_offers[self.host_id]["cards"][0]
                self.choose_card_next_tick(self.host_pc, 0)
                self.enter("host_choice")
            elif self.phase == "host_choice":
                server = self.player_map(self.server_world)
                client = self.player_map(self.client_world)
                if prop(self.director, "stage") != u.MCSingleDayStage.NUTS or any(prop(p, "pending_level_choices") for p in server.values()):
                    return
                if self.stacks(server[self.host_id]) != self.stacks(client[self.host_id]):
                    return
                if self.host_pc.is_reward_menu_open() or self.remote_pc.is_reward_menu_open():
                    return
                selected = self.selected[self.host_id]
                self.check("host_personal_grant", self.stacks(server[self.host_id]).get(selected, 0)
                           == self.initial_stacks[self.host_id].get(selected, 0)+1,
                           "The host did not receive exactly its chosen personal perk")
                self.check("both_accepted_release_nuts", not self.host_pc.is_reward_menu_open() and not self.remote_pc.is_reward_menu_open(),
                           "Both accepted personal choices must close UI and release the nut stage")
                self.check("personal_legendary_not_team_unlock", prop(self.gs, "team_tool_upgrades") == self.team_tool_mask,
                           "A personal level choice modified the whole-team tool mask")
                self.snapshot("both_claimed_nut_stage_released")
                self.nut_event = prop(self.director, "nut_event")
                self.nuts_before = prop(self.nut_event, "nuts_spawned")
                amount = self.progression.get_experience_to_next_level()
                self.progression.add_experience(amount)
                self.expected_experience = self.first_experience+amount
                self.enter("later_offers")
            elif self.phase == "later_offers":
                if not self.offers_ready(3) or not self.menu_has_three(self.host_pc) or not self.menu_has_three(self.remote_pc):
                    return
                self.second_offers = self.verify_offers("later")
                self.check("later_shared_level", prop(self.progression, "team_level") == 3
                           and prop(self.progression, "total_experience") == self.expected_experience,
                           "The authoritative XP award did not advance both peers to the same next level")
                self.snapshot("next_team_level_three_cards_while_rain_runs")
                self.enter("rain_with_pending_choices")
            elif self.phase == "rain_with_pending_choices" and now-self.phase_started > .8:
                self.require(prop(self.director, "stage") == u.MCSingleDayStage.NUTS,
                             "The nut stage ended before observing ongoing rain and replicated landing cues")
                # The new storm intentionally contains quiet gaps. Wait for its
                # next real drop and complete replication, without spawning,
                # delaying, or altering any gameplay actor for this observation.
                if prop(self.nut_event, "nuts_spawned") <= self.nuts_before or not self.landing_shadow_observed:
                    self.require(now-self.phase_started < 12,
                                 "No matching active landing shadow/continued nut drop reached the remote peer within 12 seconds")
                    return
                self.check("later_choices_dont_pause_rain", prop(self.director, "stage") == u.MCSingleDayStage.NUTS
                           and prop(self.nut_event, "nuts_spawned") > self.nuts_before,
                           "Pending later personal choices stopped the ongoing nut event")
                self.snapshot("pending_next_choices_and_rain_continues")
                self.choose_card_next_tick(self.host_pc, 0)
                self.choose_card_next_tick(self.remote_pc, 0)
                self.enter("accept_later_choices")
            elif self.phase == "accept_later_choices":
                if any(prop(p, "pending_level_choices") for p in self.player_map(self.server_world).values()):
                    return
                if self.host_pc.is_reward_menu_open() or self.remote_pc.is_reward_menu_open():
                    return
                self.check("later_actual_choices_close_before_combat", True, "")
                self.report["combat_scope"] = "Natural storm duration and boss attack schedule observed on both peers; no objective, actor pose, HP, cooldown or completion changes"
                self.enter("combat_replication")
            elif self.phase == "combat_replication":
                if self.observe_combat():
                    self.snapshot("natural_two_boss_combat_and_fireball_both_peers")
                    self.finish()
                else:
                    self.require(now-self.phase_started < 85, "Natural boss/cue/fireball replication was not fully observed within 85 seconds; observed=" + repr(sorted(self.combat_observed)))
        except Exception:
            if self.phase == "stopping":
                self.failure = self.failure or traceback.format_exc()
                self.restore()
            else:
                self.finish(traceback.format_exc())


existing = getattr(builtins, KEY, None)
if existing is not None:
    raise RuntimeError("This network validation is already running; inspect its report or use stop_single_day_network.py")
validation = SingleDayNetworkValidation()
setattr(builtins, KEY, validation)
try:
    validation.start()
except Exception:
    failure = traceback.format_exc()
    if validation.callback is None:
        validation.failure = failure
        validation.restore()
    else:
        # Begin-play can already be queued even if a later start step fails.
        validation.finish(failure)
    raise
