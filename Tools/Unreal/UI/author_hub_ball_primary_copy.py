"""Save basketball mouse controls and the round coin summary, preserving other rows.

Run in an idle editor or PythonScript commandlet. Evidence/package backups stay
on E:. Set MC_BALL_COPY_AUDIT_ONLY=1 for saved-data verification without editing.
"""
import copy
import hashlib
import json
import os
from pathlib import Path

import unreal as u

ROOT = Path("E:/DEVGAME/MessControl")
OUT = Path(os.environ.get("MC_BALL_COPY_REPORT_DIR",
    "E:/CodexWork/MessControl/Validation/GameplayFixes_20261114/HubBallCopy"))
ASSET = "/Game/Gameplay/UI/Data/DT_UI_Localization"
KEYS = ("hub.basketball.controls", "hub.basketball.pickup", "UI.HUD.RunCoinSummary")
FIELDS = ("SourceText", "Context", "RU", "EN", "DE", "FR", "ES",
          "ZH_HANS", "ZH_HANT", "JA", "KO", "PT_BR")


def export_rows(table):
    raw = u.DataTableFunctionLibrary.export_data_table_to_json_string(table)
    if isinstance(raw, tuple):
        raw = next(value for value in raw if isinstance(value, str))
    return json.loads(raw)


def write_json(name, value):
    (OUT / name).write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding="utf-8")


def main():
    assert OUT.drive.upper() == "E:", "All basketball evidence must stay on E:"
    OUT.mkdir(parents=True, exist_ok=True)
    audit = os.environ.get("MC_BALL_COPY_AUDIT_ONLY") == "1"
    if "-run=pythonscript" not in u.SystemLibrary.get_command_line().lower():
        editor = u.get_editor_subsystem(u.LevelEditorSubsystem)
        assert not editor or not editor.is_in_play_in_editor(), "Preserve the running play session"
        dirty = u.EditorLoadingAndSavingUtils.get_dirty_content_packages()
        assert not any(package.get_name() == ASSET for package in dirty), "Preserve unsaved localization edits"
    table = u.load_asset(ASSET)
    assert table, ASSET
    before = export_rows(table)
    source = json.loads((ROOT / "Tools/Unreal/UI/gameplay_ui_texts.json").read_text(encoding="utf-8"))
    canonical = {row["Name"]: row for row in source if row["Name"] in KEYS}
    assert len(canonical) == len(KEYS), "Missing canonical basketball strings"
    planned = copy.deepcopy(before)
    index = {row["Name"]: row for row in planned}
    for key in KEYS:
        if key not in index:
            assert key == "UI.HUD.RunCoinSummary", "Missing saved basketball row: " + key
            added = copy.deepcopy(canonical[key])
            planned.append(added)
            index[key] = added
        for field in FIELDS:
            index[key][field] = canonical[key][field]
    import sys
    sys.path.insert(0, str(ROOT / "Tools/Unreal/UI"))
    from localization_catalog import text_source
    if audit:
        saved = {row["Name"]: row for row in before}
        for key in KEYS:
            assert key in saved, "Missing saved row: " + key
            for field in FIELDS:
                assert text_source(saved[key][field]) == canonical[key][field], key + "." + field
        write_json("Audit.json", {"passed": True, "keys": KEYS, "validation": "Saved DataTable copy only; no gameplay result claimed."})
        return
    package = ROOT / "Content/Gameplay/UI/Data/DT_UI_Localization.uasset"
    backup = OUT / "DT_UI_Localization.Before.uasset"
    before_json = OUT / "BeforeRows.json"
    if not backup.exists():
        backup.write_bytes(package.read_bytes())
        assert hashlib.sha256(backup.read_bytes()).digest() == hashlib.sha256(package.read_bytes()).digest()
    if not before_json.exists():
        write_json("BeforeRows.json", before)
    try:
        assert u.DataTableFunctionLibrary.fill_data_table_from_json_string(table, json.dumps(planned, ensure_ascii=False))
        after = export_rows(table)
        after_index = {row["Name"]: row for row in after}
        assert len(after) == len(planned), "Unexpected localization row count"
        for row in before:
            for field, value in row.items():
                if row["Name"] in KEYS and field in FIELDS:
                    continue
                assert after_index[row["Name"]].get(field) == value, row["Name"] + "." + field
        # FText export may retain namespace/history syntax; compare their source.
        for key in KEYS:
            for field in FIELDS:
                assert text_source(after_index[key][field]) == canonical[key][field], key + "." + field
        assert u.EditorAssetLibrary.save_loaded_asset(table, only_if_is_dirty=False), ASSET
    except Exception:
        assert u.DataTableFunctionLibrary.fill_data_table_from_json_string(table, json.dumps(before, ensure_ascii=False)), "Rollback import failed"
        assert u.EditorAssetLibrary.save_loaded_asset(table, only_if_is_dirty=False), "Rollback save failed"
        raise
    write_json("AfterRows.json", after)
    write_json("Authoring.json", {"passed": True, "keys": KEYS, "package": ASSET,
        "backup": str(backup), "validation": "Two localized control rows and the round coin summary saved; unrelated rows and fields preserved. Gameplay validation remains separate."})
    u.log("MC_BALL_PRIMARY_COPY_COMPLETE " + str(OUT))


main()
