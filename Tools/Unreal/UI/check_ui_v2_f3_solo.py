"""Exercise F3 through native Slate MCP in an already running standalone PIE.

Run only with game-window focus and F3 closed. Does not start/stop PIE or change
gameplay data. Both clicks restore the original UI choice. Output must be on E:.
"""
import argparse
import base64
import json
import re
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from native_mcp_client import NativeMCP

parser = argparse.ArgumentParser()
parser.add_argument('--case', required=True)
parser.add_argument('--game-only', action='store_true', help='Gameplay viewport focus is verified by reopening F3 after close.')
parser.add_argument('--output', default='E:/CodexWork/MessControl/Validation/UIFoundation_20261103')
args = parser.parse_args()
assert re.fullmatch(r'[A-Za-z0-9_]+', args.case)
out = Path(args.output).resolve()
assert out.drive.upper() == 'E:'
out.mkdir(parents=True, exist_ok=True)
client = NativeMCP()
SLATE = 'SlateInspectorToolset.SlateInspectorToolset'

def call(group, name, arguments=None):
    result = client.tool('call_tool', {'toolset_name': group, 'tool_name': name, 'arguments': arguments or {}})
    if result.get('isError'):
        raise RuntimeError(result)
    return json.loads(result['content'][0]['text'])['returnValue']

assert call('EditorToolset.EditorAppToolset', 'IsPIERunning')
observer = call(SLATE, 'Observe', {'ref': '', 'maxDepth': 40})
roots = call(SLATE, 'Snapshot', {'ref': '', 'maxDepth': 40, 'bIncludeSourceLocations': False})
windows = re.findall(r'^window .*NetMode: Standalone[^\n]*\[ref=(w\d+)\]', roots, re.M)
assert len(windows) == 1, 'Requires exactly one standalone game window'
window = windows[0]
copies = json.loads(Path(__file__).with_name('ui_v2_texts.json').read_text(encoding='utf-8'))
labels = {row['Name']: {row[key] for key in ('RU', 'EN', 'DE', 'FR', 'ES', 'PT_BR', 'ZH_HANS', 'ZH_HANT', 'JA', 'KO')} for row in copies}
to_new = labels['UI.V2.Switch.ToNew']
to_current = labels['UI.V2.Switch.ToCurrent']

def snapshot():
    return call(SLATE, 'Snapshot', {'ref': window, 'maxDepth': 40, 'bIncludeSourceLocations': False})

def selector(text):
    matches = [(label, ref) for label, ref in re.findall(r'button "([^"\n]+)"[^\n]*\[ref=(b\d+)\]', text)
               if label in to_new or label in to_current]
    assert len(matches) <= 1, 'Multiple UI selectors are visible'
    return matches[0] if matches else None

def wait_for(check):
    deadline = time.monotonic() + 6
    while time.monotonic() < deadline:
        text = snapshot()
        if check(text):
            return text
        time.sleep(.2)
    raise AssertionError('UI did not reach the expected state')

report = {'case': args.case, 'passed': False, 'input': 'Native Slate MCP ProcessKeyDown/Up and pointer click route; no hardware input claim'}
original = None
try:
    before = snapshot()
    assert not selector(before), 'Close F3 before running this check'
    assert args.game_only or '[focused]' in before, 'Focus the game window first'
    assert call(SLATE, 'PressKey', {'key': 'F3'})
    opened = wait_for(lambda text: selector(text) is not None)
    original, button = selector(opened)
    (out / (args.case + '_Current.txt')).write_text(opened, encoding='utf-8')
    assert call(SLATE, 'Click', {'ref': button})
    changed = wait_for(lambda text: selector(text) is not None and selector(text)[0] != original)
    (out / (args.case + '_Changed.txt')).write_text(changed, encoding='utf-8')
    screenshot = call(SLATE, 'Screenshot', {'ref': window})
    file = out / (args.case + '_Changed.png')
    file.write_bytes(base64.b64decode(screenshot['data']))
    assert call(SLATE, 'Click', {'ref': selector(changed)[1]})
    restored = wait_for(lambda text: selector(text) is not None and selector(text)[0] == original)
    assert call(SLATE, 'PressKey', {'key': 'Escape'})
    closed = wait_for(lambda text: selector(text) is None)
    (out / (args.case + '_Closed.txt')).write_text(closed, encoding='utf-8')
    if args.game_only:
        # SViewport is omitted from some accessibility snapshots. A second F3
        # can pass our input preprocessor only after viewport focus is restored.
        assert call(SLATE, 'PressKey', {'key': 'F3'})
        wait_for(lambda text: selector(text) is not None)
        assert call(SLATE, 'PressKey', {'key': 'F3'})
        wait_for(lambda text: selector(text) is None)
    else:
        assert '[focused]' in closed, 'Underlying screen did not regain focus'
    report.update(passed=True, original_label=original, changed_label=selector(changed)[0],
                  restored_label=selector(restored)[0], focus_restored=True, screenshot=str(file))
finally:
    # On failure, make a bounded attempt to restore only this test's selection.
    if not report['passed'] and original:
        try:
            active = selector(snapshot())
            if active and active[0] != original:
                call(SLATE, 'Click', {'ref': active[1]})
            if selector(snapshot()):
                call(SLATE, 'PressKey', {'key': 'F3'})
        except Exception as error:
            report['cleanup_error'] = str(error)
    call(SLATE, 'Unobserve', {'identifier': observer})
    (out / (args.case + '_Report.json')).write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps(report, ensure_ascii=False))
