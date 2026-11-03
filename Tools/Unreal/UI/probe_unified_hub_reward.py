"""One read-only snapshot of an already-running solo PIE; no gameplay actions.

Execute in the connected Editor's Python context. The only writes are LiveLatest
PNG/JSON on E:. No PIE start/stop, actors, input, console commands, UI refresh,
offers, readiness ACKs, profile changes, sleeps, or follow-up captures.
"""
import json
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path
import unreal as u

OUT = Path("E:/CodexWork/MessControl/Validation/UIHubReward_20261104")
MAX_OWNERS_PER_CLASS, MAX_CONTROLS, MAX_BINDING_SAMPLES = 4, 32, 5
CLASS_NAMES = (
    "MCUnifiedUIWidget", "MCHubWidget", "MCUnifiedHubWidget",
    "MCPerkChoiceWidget", "MCUnifiedRewardWidget",
    "MCMainMenuWidget", "MCUnifiedFrontendWidget",
    "MCGameplayMenuWidget", "MCUnifiedGameplayMenuWidget",
    "MCFishingRewardWidget", "MCScoreboardWidget", "MCUnifiedTeamWidget",
)
EXPECTED = {
    "MCUnifiedUIWidget": ("InterfaceScreens", "Screen_Hub", "Screen_Reward",
                          "Screen_Frontend", "Screen_GameplayMenu"),
    "MCHubWidget": ("HubReadinessPanel", "HubBottomBar", "HubFooterBackdrop",
                     "HubSelectionFooter", "HubMouthPresentation",
                     "HubFishIcon", "HubFishCard"),
    "MCUnifiedHubWidget": ("HubV2Readiness", "HubV2ReadinessCount",
        "HubV2OpenSelection", "HubV2ProfileName", "HubV2WalletCount",
        "HubV2FishIcon", "HubV2FishCount", "HubV2WalletCard",
        "HubV2PartyHint", "HubV2SelectionFooter", "HubV2Launch", "HubV2Return"),
    "MCUnifiedRewardWidget": ("RewardV2TitlePlate", "RewardV2Countdown",
        "RewardV2SourceTitle", "RewardV2Subtitle", "RewardV2Cards",
        "RewardV2Card0", "RewardV2Card1", "RewardV2Card2", "RewardV2Card3",
        "RewardV2Action0", "RewardV2Hint"),
    "MCUnifiedFrontendWidget": ("FrontendPages", "FrontendStatus",
        "FrontendJoin_FindRooms", "FrontendJoin_RoomPicker",
        "FrontendJoin_JoinAddress", "FrontendConnection_CreateLobby",
        "FrontendLobby_Start", "FrontendLobby_Invite",
        "FrontendLeave_Confirm", "FrontendExit_Confirm"),
    "MCUnifiedGameplayMenuWidget": ("GameplayMenuV2Pages",
        "GameplayMenuV2Heading", "GameplayMenuV2_MenuResume",
        "GameplayMenuV2_MenuSettings", "GameplayMenuV2_MenuLeave",
        "GameplayMenuV2_MenuExit", "GameplayMenuV2Status", "GameplayMenuV2Hint"),
    "MCGameplayMenuWidget": ("GameplayMenuPages", "GameplayMenuHeading",
        "MenuResume", "MenuSettings", "MenuLeave", "MenuExit"),
}
PREFIXES = ("HubV2", "HubReadiness", "HubWallet", "HubBottomFish",
    "HubProfile", "HubParty", "HubSelection", "HubLaunch", "HubReturn",
    "HubOpenSelection", "RewardV2", "Frontend", "GameplayMenuV2",
    "MenuJoin_", "MenuConnection_", "MenuLobby_", "MenuLeave_", "MenuExit_")
SOURCE_PROPS = ("title", "subtitle", "hint", "heading", "status",
    "status_text", "title_text", "hint_text", "lobby_players", "pages",
    "stage_countdown_text", "stage_countdown_border", "room_picker",
    "address_box", "buttons", "card_widgets", "current_choice_ids")

def prop(obj, name):
    try:
        return obj.get_editor_property(name) if obj is not None else None
    except Exception:
        return None

def path(obj):
    return obj.get_path_name() if isinstance(obj, u.Object) else None

def value(obj):
    if obj is None or isinstance(obj, (bool, int, float)):
        return obj
    if isinstance(obj, u.Object):
        return path(obj)
    return str(obj)[:600]

def readonly_get(obj, method, *args):
    # Only named, source-reviewed, non-mutating getters are passed here.
    fn = getattr(obj, method, None) if obj is not None else None
    if not callable(fn):
        return {"available": False}
    try:
        return {"available": True, "value": value(fn(*args))}
    except Exception as exc:
        return {"available": True, "error": str(exc)[:180]}

def widget_tree(owner):
    # Known live instance/template names; avoid protected Python property access.
    for name in ("WidgetTree_0", "WidgetTree"):
        found = u.find_object(owner, name)
        if isinstance(found, u.WidgetTree) and found.get_outer() == owner:
            return found
    iterator = u.ObjectIterator(u.WidgetTree)
    try:
        for found in iterator:
            if found.get_outer() == owner:
                return found
    finally:
        del iterator
    raise AssertionError("Owned WidgetTree missing: " + path(owner))

def root_node(owner):
    tree = widget_tree(owner)
    # Named source-owned roots/anchors avoid protected RootWidget reflection.
    for name in ("InterfaceSafeArea","UniversalRoot","MenuBackdrop","HubViewportCanvas","HubCanvas","RewardBackdrop","RewardV2Size","FrontendDesignBounds","GameplayMenuV2Size","GameplayMenuBackdrop","HubV2Root","TeamRoot","FishingRewardCanvas","ScoreFrame"):
        node = u.find_object(tree, name)
        if isinstance(node, u.Widget):
            while node.get_parent() is not None and node.get_parent().get_outer() == tree:
                node = node.get_parent()
            return node
    return None

def nodes(owner):
    owned_tree = widget_tree(owner)
    pending, result, visited = [root_node(owner)], [], set()
    while pending and len(visited) < 2048:
        node = pending.pop()
        if node is None or path(node) in visited:
            continue
        visited.add(path(node))
        if node.get_outer() == owned_tree:
            result.append(node)
        if isinstance(node, u.PanelWidget):
            pending.extend(node.get_all_children())
        if isinstance(node, u.UserWidget):
            # Follow actual named-slot content; protected binding arrays may
            # be unavailable, so traverse only this universal shape's live root.
            if isinstance(node, u.MCUniversalUIWidget):
                pending.append(root_node(node))
            for binding in list(prop(node, "named_slot_bindings") or [])[:8]:
                content = prop(binding, "content")
                if content is not None:
                    pending.append(content)
    return result

def hidden(node):
    visibility = str(node.get_visibility()).upper()
    return "COLLAPSED" in visibility or "HIDDEN" in visibility

def effective(node):
    # Cross WidgetTree roots into nested UserWidgets and respect switcher slots.
    visited = set()
    while node is not None and path(node) not in visited:
        visited.add(path(node))
        if hidden(node):
            return False
        parent = node.get_parent()
        if isinstance(parent, u.WidgetSwitcher) and parent.get_active_widget() != node:
            return False
        if parent is not None:
            node = parent
        else:
            outer = node.get_outer()
            node = (outer.get_outer() if isinstance(outer, u.WidgetTree)
                    and root_node(outer.get_outer()) == node else None)
    return True

def widget_state(node):
    if node is None:
        return None
    data = {"name": node.get_name(), "path": path(node),
            "visibility": str(node.get_visibility()),
            "visible": node.is_visible(), "effectiveVisible": effective(node),
            "enabled": node.get_is_enabled()}
    if isinstance(node, (u.TextBlock, u.EditableTextBox)):
        data["text"] = str(node.get_text())[:600]
    universal = getattr(u, "MCUniversalUIWidget", None)
    if universal and isinstance(node, universal):
        data["label"] = str(prop(node, "label"))[:600]
        data["labelKey"] = value(prop(node, "label_key"))
    if isinstance(node, u.Button):
        pending, labels = [node], []
        for _ in range(16):
            if not pending:
                break
            child = pending.pop(0)
            if isinstance(child, u.TextBlock):
                labels.append(str(child.get_text())[:600])
            elif isinstance(child, u.PanelWidget):
                pending.extend(list(child.get_all_children())[:8])
        data["text"] = " ".join(labels[:3])
    if isinstance(node, u.ComboBoxString):
        data["selected"] = node.get_selected_option()
        data["optionCount"] = node.get_option_count()
    return data

def switcher_state(switcher):
    children = list(switcher.get_all_children())
    return {"name": switcher.get_name(),
            "activeIndex": switcher.get_active_widget_index(),
            "activeWidget": path(switcher.get_active_widget()),
            "pageCount": len(children),
            "pages": [{"name": w.get_name(), "visibility": str(w.get_visibility()),
                       "effectiveVisible": effective(w)}
                      for w in children[:20]]}

def binding_evidence(owner):
    bindings = prop(owner, "bindings")
    if bindings is None:
        controls = prop(owner, "controls")
        bindings = [prop(c, "binding") for c in controls] if controls is not None else []
    bindings = [b for b in bindings if b is not None]
    sources, samples = set(), []
    live_count = 0
    for binding in bindings[:160]:
        source = next((prop(binding, key) for key in
                       ("source_button", "source_control", "source")
                       if prop(binding, key) is not None), None)
        if source is not None:
            live_count += 1
            outer = source.get_outer()
            if isinstance(outer, u.WidgetTree):
                sources.add(path(outer.get_outer()))
        if len(samples) < MAX_BINDING_SAMPLES:
            samples.append({"source": path(source),
                            "view": path(prop(binding, "view")),
                            "viewName": value(prop(binding, "view_name")),
                            "page": value(prop(binding, "page_name") or prop(binding, "page"))})
    return {"count": len(bindings), "liveSourceControls": live_count,
            "sourceOwners": sorted(sources), "samples": samples,
            "inspectionCap": 160,
            "note": "Source-control links are evidence; nulls do not prove no backend."}

def source_snapshot(owner):
    result = {}
    for name in SOURCE_PROPS:
        item = prop(owner, name)
        if item is None:
            continue
        if isinstance(item, u.WidgetSwitcher):
            result[name] = switcher_state(item)
        elif isinstance(item, u.Widget):
            result[name] = widget_state(item)
        elif name in ("buttons", "card_widgets", "current_choice_ids"):
            result[name] = {"count": len(item),
                "items": [widget_state(x) if isinstance(x, u.Widget) else value(x)
                          for x in list(item)[:8]]}
    offer = prop(owner, "current_rogue_offer")
    if offer is not None:
        result["currentRogueOffer"] = {
            key: value(prop(offer, key)) for key in ("offer_id", "kind", "step", "curse", "reward")}
        for key in ("i_ds", "ids", "reward_i_ds", "reward_ids"):
            items = prop(offer, key)
            if items is not None:
                result["currentRogueOffer"][key] = [value(x) for x in list(items)[:8]]
    return result

def main():
    try:
        worlds = u.EditorLevelLibrary.get_pie_worlds(True)
        assert len(worlds) == 1, "Probe requires one already-running solo PIE world."
        world = worlds[0]
        pc = u.GameplayStatics.get_player_controller(world, 0)
        assert pc is not None, "Current PIE has no local player controller."
        report = {
            "schema": 2, "ownedTreeTraversalCap": 2048, "utc": datetime.now(timezone.utc).isoformat(),
            "world": path(world), "controller": path(pc),
            "version": readonly_get(getattr(u, "MCGameplayUIFactory", None), "get_presentation_version"),
            "readOnly": True, "objects": {}, "classCounts": {}, "missingPythonClasses": [],
            "limitations": [
                "Visibility is sampled; no navigation, action dispatch, or future state is tested.",
                "Non-reflected C++ backend pointers/snapshots are unavailable to Python.",
                "GetPresentationSnapshot is intentionally not called: reward snapshots refresh deadline UI.",
                "Expected controls are structural evidence, not an assertion of artistic approval.",
            ],
        }
        for method in ("is_wardrobe_open", "is_hub_shop_open"):
            report[method] = readonly_get(pc, method)

        interesting_types = tuple(cls for cls in (getattr(u, name, None) for name in
            ("TextBlock", "MCUniversalUIWidget", "ComboBoxString", "EditableTextBox", "Button")) if cls)
        for class_name in CLASS_NAMES:
            cls = getattr(u, class_name, None)
            if cls is None:
                report["missingPythonClasses"].append(class_name)
                continue
            owners = [w for w in u.ObjectIterator(cls)
                      if w.get_world() == world and w.get_owning_player() == pc]
            owners.sort(key=lambda w: (not w.is_in_viewport(), not w.is_visible(), path(w)))
            report["classCounts"][class_name] = {"total": len(owners),
                                                "sampled": min(len(owners), MAX_OWNERS_PER_CLASS)}
            summaries = []
            for owner in owners[:MAX_OWNERS_PER_CLASS]:
                owned = nodes(owner)
                assert owned, "No scoped widgets resolved: " + path(owner)
                named = {w.get_name(): w for w in owned}
                expected = EXPECTED.get(class_name, ())
                selected = [w for w in owned if isinstance(w, interesting_types)
                            and (w.get_name().startswith(PREFIXES) or w.get_name() in expected)]
                active_controls = sorted((w for w in selected if effective(w)), key=lambda w: w.get_name())
                data = widget_state(owner)
                data.update({"inViewport": owner.is_in_viewport(),
                    "root": widget_state(root_node(owner)),
                    "widgetCount": len(owned),
                    "widgetTypes": dict(Counter(w.get_class().get_name() for w in owned)),
                    "expected": {"present": [n for n in expected if n in named],
                                 "missing": [n for n in expected if n not in named],
                                 "states": [{"name": n, "visibility": str(named[n].get_visibility()),
                                             "effectiveVisible": effective(named[n]),
                                             "enabled": named[n].get_is_enabled()}
                                            for n in expected if n in named]},
                    "controls": [widget_state(w) for w in active_controls[:MAX_CONTROLS]],
                    "controlCount": len(selected), "effectiveControlCount": len(active_controls),
                    "controlsTruncated": len(active_controls) > MAX_CONTROLS,
                    "switchers": [switcher_state(w) for w in owned
                                  if isinstance(w, u.WidgetSwitcher)][:4]})
                if class_name.startswith("MCUnified") and class_name != "MCUnifiedUIWidget":
                    data["backendGetter"] = readonly_get(owner, "has_bound_backend")
                    data["backendEvidence"] = binding_evidence(owner)
                if class_name in ("MCHubWidget", "MCMainMenuWidget", "MCPerkChoiceWidget", "MCGameplayMenuWidget"):
                    data["sourceSnapshot"] = source_snapshot(owner)
                if class_name == "MCHubWidget":
                    data["menuInput"] = readonly_get(owner, "requires_menu_input")
                    data["sceneReady"] = readonly_get(owner, "is_scene_ready")
                if class_name == "MCMainMenuWidget":
                    data["activeSourcePage"] = readonly_get(owner, "get_active_menu_page_root")
                if class_name == "MCUnifiedUIWidget":
                    screens = named.get("InterfaceScreens")
                    if isinstance(screens, u.WidgetSwitcher) and owner.is_in_viewport():
                        report["activeScreen"] = switcher_state(screens)
                        report["rootVisible"] = owner.is_visible()
                if class_name == "MCFishingRewardWidget":
                    data["animating"] = readonly_get(owner, "is_animating")
                    data["animationAge"] = readonly_get(owner, "get_animation_age")
                    data["counterTargets"] = {}
                    for key, getter in (("icon", "get_counter_icon_target"), ("card", "get_counter_card_target")):
                        state = readonly_get(owner, getter)
                        if state.get("available") and "error" not in state:
                            target = getattr(owner, getter)()
                            state["widget"] = widget_state(target)
                            outer = target.get_outer() if target is not None else None
                            state["treeOwner"] = path(outer.get_outer()) if isinstance(outer, u.WidgetTree) else None
                            hub_cls = getattr(u, "MCUnifiedHubWidget", None)
                            state["targetsUnifiedHub"] = bool(hub_cls and isinstance(
                                outer.get_outer() if isinstance(outer, u.WidgetTree) else None, hub_cls))
                        data["counterTargets"][key] = state
                summaries.append(data)
            report["objects"][class_name] = summaries

        gs = u.GameplayStatics.get_game_state(world)
        hub_cls = getattr(u, "MCHubGameState", None)
        if hub_cls and isinstance(gs, hub_cls):
            hub = {"path": path(gs)}
            for name in ("phase", "scene_generation", "selected_day", "notice"):
                hub[name] = value(prop(gs, name))
            for method in ("get_ready_count", "get_participant_count", "get_loaded_count",
                           "are_all_ready", "are_all_loaded", "has_host"):
                hub[method] = readonly_get(gs, method)
            hub["is_local_host"] = readonly_get(gs, "is_host", pc)
            players = prop(gs, "players")
            hub["players"] = [{"player": path(prop(p, "player")), "ready": prop(p, "ready"),
                              "loaded": prop(p, "loaded")} for p in list(players or [])[:4]]
            report["hubState"] = hub

        OUT.mkdir(parents=True, exist_ok=True)
        report["screenshot"] = {"path": str(OUT / "LiveLatest.png"), "captured": False}
        capture = getattr(getattr(u, "MCGameplayUIEditorLibrary", None), "capture_pie_game_window", None)
        if callable(capture):
            try:
                report["screenshot"]["captured"] = bool(capture(world, str(OUT / "LiveLatest.png")))
            except Exception as exc:
                report["screenshot"]["error"] = str(exc)[:240]
        else:
            report["screenshot"]["error"] = "Capture API unavailable in current Editor binary."
        (OUT / "LiveLatest.json").write_text(
            json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
        print(json.dumps({"activeScreen": report.get("activeScreen", {}).get("activeWidget"),
            "rootVisible": report.get("rootVisible"), "classes": report["classCounts"],
            "json": str(OUT / "LiveLatest.json"), "screenshot": report["screenshot"]}, ensure_ascii=False))
    finally:
        # No UObject, reflected container, or owner cache escapes the command.
        # Do not force UE/Python GC here: this probe only reads game state.
        worlds = world = pc = gs = owners = owner = owned = named = None
        selected = active_controls = screens = players = target = outer = None
        data = hub = summaries = report = None


if __name__ == "__main__":
    main()
