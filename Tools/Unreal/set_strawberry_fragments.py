"""Set only the authored strawberry fragment count to five.

Run with Unreal's PythonScript commandlet. This reads the current menu, keeps
all other rows/fields intact, and saves only DT_BreakfastMenu. Set
MC_STRAWBERRY_AUDIT_ONLY=1 for a read-only verification. Evidence/backup files
go to MC_STRAWBERRY_REPORT_DIR (an absolute E: path).
"""
import copy
import hashlib
import json
import os
from pathlib import Path

import unreal as u


def write_json(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding="utf-8")


def export_rows(table):
    text = u.DataTableFunctionLibrary.export_data_table_to_json_string(table)
    if isinstance(text, tuple):
        text = next((item for item in text if isinstance(item, str)), None)
    if not text:
        raise RuntimeError("Could not export the current breakfast menu")
    return json.loads(text)


report_dir = Path(os.environ.get(
    "MC_STRAWBERRY_REPORT_DIR",
    "E:/CodexWork/MessControlFeedback_20261019/Strawberry"))
if report_dir.drive.upper() != "E:":
    raise RuntimeError("Strawberry evidence must be stored on E:")
report_dir.mkdir(parents=True, exist_ok=True)
audit_only = os.environ.get("MC_STRAWBERRY_AUDIT_ONLY") == "1"
assert not u.EditorLevelLibrary.get_pie_worlds(False), "Stop PIE before editing the menu"
table = u.EditorAssetLibrary.load_asset("/Game/Data/DT_BreakfastMenu")
assert table, "The saved breakfast menu is missing"
before = export_rows(table)
matches = [row for row in before if "strawberr" in str(row["Name"]).lower()]
if not matches:
    matches = [row for row in before if any(
        "strawberr" in str(mesh).lower() for mesh in row.get("WholeMeshes", []))]
assert len(matches) == 1, "Expected one strawberry row, found: " + str([
    row["Name"] for row in matches])
name = matches[0]["Name"]
old_count = matches[0]["Fragments"]
report = dict(asset=table.get_path_name(), row=name, before=old_count,
              requested=5, audit_only=audit_only, changed=False)
if audit_only:
    report["rows"] = before
else:
    expected = copy.deepcopy(before)
    next(row for row in expected if row["Name"] == name)["Fragments"] = 5
    backup_file = report_dir / "MenuBefore.json"
    if not backup_file.exists():
        write_json(backup_file, before)
    if old_count != 5:
        try:
            assert u.DataTableFunctionLibrary.fill_data_table_from_json_string(
                table, json.dumps(expected, ensure_ascii=False)), "Table import failed"
            after = export_rows(table)
            assert after == expected, "An unrelated menu value changed; refusing to save"
            assert u.EditorAssetLibrary.save_loaded_asset(table), "Menu save failed"
            report["changed"] = True
        except Exception:
            u.DataTableFunctionLibrary.fill_data_table_from_json_string(
                table, json.dumps(before, ensure_ascii=False))
            raise
    else:
        after = before
    report["after"] = next(row for row in after if row["Name"] == name)["Fragments"]
    report["unrelated_values_preserved"] = after == expected
    assert report["after"] == 5
    write_json(report_dir / "MenuAfter.json", after)
package = Path(u.Paths.project_content_dir()) / "Data/DT_BreakfastMenu.uasset"
report["package_sha256"] = hashlib.sha256(package.read_bytes()).hexdigest()
write_json(report_dir / ("Audit.json" if audit_only else "Result.json"), report)
u.log("MC_STRAWBERRY_FRAGMENTS " + json.dumps({
    key: value for key, value in report.items() if key != "rows"}, ensure_ascii=False))
