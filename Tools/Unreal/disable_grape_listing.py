"""Deactivate grape ingredients in the saved breakfast menu without deleting art.

Run with Unreal's PythonScript commandlet while the editor is closed, or in an
idle editor. The saved menu is authoritative: current grapes keep their legacy
row names (including Bacon). Only SelectionWeight is set to zero. Run again
after a menu migration to retain this listing policy. Set MC_GRAPE_AUDIT_ONLY=1
for read-only, fresh-process verification against the retained BeforeRows.json.
All evidence and the original package backup stay under MC_GRAPE_REPORT_DIR
on E:. No meshes, materials, collision data or other menu fields are changed.
"""
import copy
import hashlib
import json
import os
from pathlib import Path

import unreal as u


ASSET = "/Game/Data/DT_BreakfastMenu"


def write_json(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding="utf-8")


def export_rows(table):
    value = u.DataTableFunctionLibrary.export_data_table_to_json_string(table)
    if isinstance(value, tuple):
        value = next((item for item in value if isinstance(item, str)), None)
    assert value, "Could not export the saved breakfast menu"
    return json.loads(value)


def grape_rows(rows):
    return [row for row in rows if any(
        "grape" in str(mesh).lower() for mesh in row.get("WholeMeshes", []))]


def planned_rows(rows):
    result = copy.deepcopy(rows)
    matches = grape_rows(result)
    assert matches, "No actual grape mesh in the current menu; review its identity before authoring"
    for row in matches:
        row["SelectionWeight"] = 0.0
    assert any(row.get("Kind") == "Food" and row.get("SelectionWeight", 0) > 0
               for row in result), "Preserve an available ordinary food pool"
    return result


def main():
    out = Path(os.environ.get(
        "MC_GRAPE_REPORT_DIR",
        "E:/CodexWork/MessControl/Validation/GameplayFixes_20261114/GrapeListing"))
    assert out.drive.upper() == "E:", "Grape evidence must stay on E:"
    out.mkdir(parents=True, exist_ok=True)
    audit = os.environ.get("MC_GRAPE_AUDIT_ONLY") == "1"
    commandlet = "-run=pythonscript" in u.SystemLibrary.get_command_line().lower()
    if not commandlet:
        level = u.get_editor_subsystem(u.LevelEditorSubsystem)
        assert not level or not level.is_in_play_in_editor(), "Preserve the running play session"
        assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages(), "Preserve unsaved content"
        assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages(), "Preserve unsaved maps"

    table = u.load_asset(ASSET)
    assert table, "The saved breakfast menu is missing"
    before = export_rows(table)
    expected = planned_rows(before)
    package = Path(u.Paths.project_content_dir()) / "Data/DT_BreakfastMenu.uasset"
    backup = out / "BeforeRows.json"
    report = dict(asset=ASSET, audit_only=audit, changed=False,
                  rows=[row["Name"] for row in grape_rows(before)],
                  original_weights={row["Name"]: row["SelectionWeight"] for row in grape_rows(before)})
    if audit:
        assert before == expected, "Grape listing is still active in the freshly loaded package"
        if backup.exists():
            original = json.loads(backup.read_text(encoding="utf-8"))
            assert before == planned_rows(original), "An unrelated saved menu value changed"
        after = before
    else:
        if not backup.exists():
            write_json(backup, before)
            (out / "BeforePackage.uasset").write_bytes(package.read_bytes())
        if before != expected:
            try:
                assert u.DataTableFunctionLibrary.fill_data_table_from_json_string(
                    table, json.dumps(expected, ensure_ascii=False)), "Menu import failed"
                after = export_rows(table)
                assert after == expected, "An unrelated field changed; refusing to save"
                assert u.EditorAssetLibrary.save_loaded_asset(table), "Menu save failed"
                report["changed"] = True
            except Exception:
                u.DataTableFunctionLibrary.fill_data_table_from_json_string(
                    table, json.dumps(before, ensure_ascii=False))
                raise
        else:
            after = before
    assert after == expected, "Only grape SelectionWeight may change"
    report.update(unrelated_values_preserved=True,
                  selection_weights={row["Name"]: row["SelectionWeight"] for row in grape_rows(after)},
                  package_sha256=hashlib.sha256(package.read_bytes()).hexdigest(),
                  row_count=len(after), resources_preserved=True,
                  validation=("Fresh-process saved-table audit against original rows; no gameplay/build assertion"
                              if audit else "DataTable import/readback/save; fresh-process audit pending"))
    write_json(out / ("AuditRows.json" if audit else "AfterRows.json"), after)
    write_json(out / ("Audit.json" if audit else "Authoring.json"), report)
    u.log("MC_GRAPE_LISTING " + json.dumps(report, ensure_ascii=False))


if __name__ == "__main__":
    main()
