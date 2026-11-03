"""Bounded Frontend delegate check in an existing solo New-UI MainMenu PIE.

Host-side script. Does not start/stop PIE, change UI preferences/settings,
search/join/host sessions, travel, or confirm Exit. Actions are real UMG delegate
broadcasts, not hardware input. Restores the source address draft and Main page.
"""
import json
import os
import shutil
import sys
import time
from pathlib import Path

PROJECT = Path("E:/DEVGAME/MessControl")
OUT = Path("E:/CodexWork/MessControl/Validation/UIHubReward_20261104")
TEMP = Path("E:/CodexTemp/MessControl-UIFoundation")
TEMP.mkdir(parents=True, exist_ok=True)
os.environ["TEMP"] = os.environ["TMP"] = str(TEMP)
os.environ["PYTHONDONTWRITEBYTECODE"] = "1"
sys.dont_write_bytecode = True
sys.path.insert(0, "E:/UE/UE_5.8/Engine/Plugins/Experimental/PythonScriptPlugin/Content/Python")
import remote_execution as remote

PROBE = PROJECT / "Tools/Unreal/UI/probe_unified_hub_reward.py"
STATE = OUT / "FrontendCurrentState.json"
MARKER = "ui-delegate-probe.invalid:7777"
OUT.mkdir(parents=True, exist_ok=True)

# Every command has a fresh namespace: retain paths/strings on the host, not
# UObject references in Editor Python globals that could hide a GC lifetime bug.
REMOTE_CONTEXT = r"""
import json
from pathlib import Path
import unreal as u
OUT = Path("E:/CodexWork/MessControl/Validation/UIHubReward_20261104")
worlds = u.EditorLevelLibrary.get_pie_worlds(True)
assert len(worlds) == 1, "Requires one existing solo PIE world"
world = worlds[0]
assert u.GameplayStatics.get_current_level_name(world, True) == "L_MainMenu"
assert u.MCGameplayUIFactory.get_presentation_version() == u.MCUIVersion.NEW
pc = u.GameplayStatics.get_player_controller(world, 0)
def owner(cls, viewport=False):
    matches = [x for x in u.ObjectIterator(cls)
               if x.get_world() == world and x.get_owning_player() == pc
               and (not viewport or x.is_in_viewport())]
    assert len(matches) == 1, (str(cls), len(matches))
    return matches[0]
source = owner(u.MCMainMenuWidget, True)
root = owner(u.MCUnifiedUIWidget, True)
view = owner(u.MCUnifiedFrontendWidget)
def tree(widget):
    for name in ("WidgetTree_0", "WidgetTree"):
        found = u.find_object(widget, name)
        if isinstance(found, u.WidgetTree) and found.get_outer() == widget:
            return found
    iterator = u.ObjectIterator(u.WidgetTree)
    try:
        for found in iterator:
            if found.get_outer() == widget:
                return found
    finally:
        del iterator
    raise AssertionError("Owned WidgetTree missing: " + widget.get_path_name())
def find(widget, name, required=True):
    # Native scoped loaded-object lookup, not a scan of editor/template widgets.
    owned_tree = tree(widget)
    found = u.find_object(owned_tree, name)
    if required:
        assert isinstance(found, u.Widget) and found.get_outer() == owned_tree, name
    return found
def text(widget):
    return str(widget.get_text()) if widget is not None else None
def state():
    source_page = source.get_active_menu_page_root()
    source_pages = source_page.get_parent()
    assert isinstance(source_pages, u.WidgetSwitcher)
    root_pages = find(root, "InterfaceScreens")
    view_pages = find(view, "FrontendPages")
    source_root = find(source, "MenuBackdrop")
    return {
        "world": world.get_path_name(), "controller": pc.get_path_name(),
        "source": source.get_path_name(), "root": root.get_path_name(),
        "frontend": view.get_path_name(),
        "sourcePage": source_page.get_name() if source_page else None,
        "sourcePageIndex": source_pages.get_active_widget_index(),
        "rootScreen": root_pages.get_active_widget_index(),
        "rootPage": root_pages.get_active_widget().get_name(),
        "frontendPage": view_pages.get_active_widget_index(),
        "sourceVisible": source.is_visible(), "rootVisible": root.is_visible(),
        "sourceRootVisibility": str(source_root.get_visibility()),
        "sourceAddress": text(find(source, "MenuJoin_AddressBox", False)),
        "newAddress": text(find(view, "FrontendJoin_Address", False)),
    }
def click_new(name):
    assert name in ("FrontendJoin_Back", "FrontendExit_Cancel")
    expected_page = "MenuJoin_Page" if name == "FrontendJoin_Back" else "MenuExit_Page"
    assert source.get_active_menu_page_root().get_name() == expected_page
    assert find(root, "InterfaceScreens").get_active_widget_index() == 10
    button = find(view, name)
    assert isinstance(button, u.MCUniversalUIWidget), name
    assert button.get_is_enabled(), name
    active_page = find(view, "FrontendPages").get_active_widget()
    current, within = button, False
    while current is not None:
        assert current.is_visible(), (name, current.get_name())
        if current == active_page:
            within = True
        current = current.get_parent()
    assert within and root.is_visible(), name
    # Use the saved editable button's actual bound universal surface delegate.
    surface = find(button, "UniversalSurface")
    assert surface.get_is_enabled(), name
    surface.on_clicked.broadcast()
def edit_new_address(value):
    box = find(view, "FrontendJoin_Address")
    assert box.get_is_enabled() and box.is_visible()
    assert source.get_active_menu_page_root().get_name() == "MenuJoin_Page"
    box.set_text(u.Text(value))
    # Explicit real OnTextChanged dispatch; no private presenter/source setter.
    box.on_text_changed.broadcast(u.Text(value))
"""
STATE_WRITE = '\n(OUT / "FrontendCurrentState.json").write_text(json.dumps(state(), ensure_ascii=False, indent=2), encoding="utf-8")\n'

session = remote.RemoteExecution()
report = {
    "passed": False,
    "input": "Real editable UMG UniversalSurface.on_clicked / OnTextChanged broadcasts; hardware input unverified",
    "scope": "Existing MainMenu solo PIE with New UI; no network, travel, confirmations, settings or preferences",
    "steps": [], "forcedGC": 0, "cleanup": {},
}
baseline = None
baseline_address = None
started_actions = False
connected = False
failure = None

def statement(code):
    scope = {"__name__": "__main__", "__file__": str(PROJECT / "Tools/Unreal/UI/check_unified_frontend_solo.py")}
    return session.run_command("exec(" + repr(code) + ", " + repr(scope) + ")",
        exec_mode=remote.MODE_EXEC_STATEMENT, unattended=True, raise_on_failure=True)

def current():
    statement(REMOTE_CONTEXT + STATE_WRITE)
    return json.loads(STATE.read_text(encoding="utf-8"))

def stable(data):
    if baseline is not None:
        for key in ("world", "controller", "source", "root", "frontend"):
            assert data[key] == baseline[key], ("Object identity changed", key, baseline[key], data[key])

def wait_page(source_page, root_screen, frontend_page=None, address=None, timeout=10):
    deadline, last = time.monotonic() + timeout, None
    while time.monotonic() < deadline:
        last = current()
        stable(last)
        if (last["sourcePage"] == source_page and last["rootScreen"] == root_screen
                and last["rootVisible"] and last["sourceVisible"]
                and (frontend_page is None or last["frontendPage"] == frontend_page)
                and (address is None or (last["sourceAddress"] == address and last["newAddress"] == address))):
            return last
        time.sleep(.15)
    raise RuntimeError("Frontend transition timeout: " + json.dumps(last, ensure_ascii=False))

def action(code):
    statement(REMOTE_CONTEXT + "\n" + code + "\n" + STATE_WRITE)

def source_action(name):
    assert name in ("JOIN_LOBBY", "EXIT", "BACK")
    action("source.handle_action(u.MCMainMenuAction." + name + ")")

def capture(name, data):
    # Fresh namespace prevents probe ObjectIterator caches surviving the command.
    probe_scope = {"__name__": "__main__", "__file__": str(PROBE)}
    statement("from pathlib import Path\np = " + repr(str(PROBE)) +
              "\nexec(compile(Path(p).read_text(encoding='utf-8'), p, 'exec'), " + repr(probe_scope) + ")")
    probe = json.loads((OUT / "LiveLatest.json").read_text(encoding="utf-8"))
    assert probe["screenshot"]["captured"], probe["screenshot"]
    for extension in ("json", "png"):
        shutil.copy2(OUT / ("LiveLatest." + extension), OUT / (name + "." + extension))
    report["steps"].append({"name": name, "state": data,
        "json": str(OUT / (name + ".json")), "png": str(OUT / (name + ".png"))})
    print(name + ": PASS", flush=True)

def force_gc():
    # Run only in a clean/restarted Editor; preceding author/render jobs must
    # also use fresh namespaces and release wrappers before this checkpoint.
    # Python cycles from this script's fresh command namespaces release first.
    statement("import gc\nimport unreal as u\ngc.collect()\nu.SystemLibrary.collect_garbage()")
    report["forcedGC"] += 1

try:
    session.start()
    nodes, deadline = [], time.monotonic() + 8
    while time.monotonic() < deadline:
        nodes = [n for n in session.remote_nodes if n.get("project_name") == "MessControl"]
        if len(nodes) == 1:
            break
        time.sleep(.2)
    assert len(nodes) == 1, "Expected exactly one connected MessControl Editor"
    session.open_command_connection(nodes[0]["node_id"])
    connected = True
    report["remoteNode"] = {"nodeId": nodes[0]["node_id"], "project": nodes[0].get("project_name")}
    baseline = current()
    assert baseline["sourcePage"] == "MenuMain_Page" and baseline["rootScreen"] == 0, baseline
    assert baseline["rootVisible"] and baseline["sourceVisible"], baseline
    report["baseline"] = baseline

    started_actions = True
    source_action("JOIN_LOBBY")
    joined = wait_page("MenuJoin_Page", 10, 0)
    baseline_address = joined["sourceAddress"]
    assert baseline_address is not None and joined["newAddress"] == baseline_address, joined
    report["baselineAddress"] = baseline_address
    capture("FrontendJoin", joined)
    action("click_new('FrontendJoin_Back')")
    capture("FrontendBack", wait_page("MenuMain_Page", 0))
    source_action("JOIN_LOBBY")
    wait_page("MenuJoin_Page", 10, 0, baseline_address)
    action("edit_new_address(" + repr(MARKER) + ")")
    wait_page("MenuJoin_Page", 10, 0, MARKER)
    force_gc()
    capture("FrontendJoinAfterGC", wait_page("MenuJoin_Page", 10, 0, MARKER))

    action("click_new('FrontendJoin_Back')")
    wait_page("MenuMain_Page", 0)
    source_action("JOIN_LOBBY")
    capture("FrontendAddressRetained", wait_page("MenuJoin_Page", 10, 0, MARKER))

    action("edit_new_address(" + repr(baseline_address) + ")")
    wait_page("MenuJoin_Page", 10, 0, baseline_address)
    action("click_new('FrontendJoin_Back')")
    wait_page("MenuMain_Page", 0)

    source_action("EXIT")
    capture("FrontendExit", wait_page("MenuExit_Page", 10, 4))
    action("click_new('FrontendExit_Cancel')")
    wait_page("MenuMain_Page", 0)
    report["passed"] = True
except Exception as exc:
    failure = exc
    report["error"] = str(exc)
finally:
    if connected and started_actions:
        try:
            before_cleanup = current()
            stable(before_cleanup)
            if baseline_address is not None:
                # Only the UI text draft is restored. No Join/commit/preferences.
                action("find(source, 'MenuJoin_AddressBox').set_text(u.Text(" + repr(baseline_address) + "))")
            data = current()
            if data["sourcePage"] in ("MenuJoin_Page", "MenuExit_Page"):
                source_action("BACK")
            else:
                assert data["sourcePage"] == "MenuMain_Page", ("Unexpected page; no unrelated action", data)
            restored = wait_page("MenuMain_Page", 0)
            assert baseline_address is None or restored["sourceAddress"] == baseline_address, restored
            report["cleanup"] = {"mainRestored": True, "addressRestored": baseline_address is not None,
                                 "state": restored}
            capture("FrontendFinalRestored", restored)
        except Exception as exc:
            report["cleanup"] = {"error": str(exc), "mainRestored": False}
            report["passed"] = False
            if failure is None:
                failure = exc
    session.stop()
    (OUT / "FrontendSoloReport.json").write_text(
        json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
print(json.dumps({"passed": report["passed"], "steps": len(report["steps"]),
                  "forcedGC": report["forcedGC"], "cleanup": report["cleanup"],
                  "report": str(OUT / "FrontendSoloReport.json")}, ensure_ascii=False))
if failure is not None:
    raise failure
