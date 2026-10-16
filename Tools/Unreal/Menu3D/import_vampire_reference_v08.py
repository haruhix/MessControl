"""Import the frozen reference-shape candidate, retaining native animation/UI."""
import hashlib
import json
from pathlib import Path
import runpy
import unreal as u

root = Path(u.Paths.project_dir()).resolve()
levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
assert not levels.is_in_play_in_editor()
world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
assert world and world.get_path_name().split('.')[0] == '/Game/UI/MainMenu3D/L_MainMenu3D_RefFinish_v08'
handoff = root / 'ArtSource/UI/MainMenu3D/20261015_refB_v1/Assembly/UnrealHandoff/v08_ReferenceFinish'
receipt = json.loads((handoff / 'producer_handoff_receipt.json').read_text(encoding='utf8'))
expected = '0ae690f36ee0a43683336f916253ee3ab46b9873bf62a85228772e87eb2a3a1b'
assert receipt['files']['SK_DAY03.fbx']['sha256'] == expected
assert hashlib.sha256((handoff / 'SK_DAY03.fbx').read_bytes()).hexdigest() == expected
runpy.run_path(str(root / 'Tools/Unreal/Menu3D/import_vampire_symmetric_v06.py'),
    init_globals={'MC_DAY03_IMPORT_REQUEST': {
        'revision': 'v08_ReferenceFinish',
        'mesh_name': 'SK_DAY03_Reference_v08',
        'fbx_sha256': expected,
        'report_name': 'reference_v08_import_report.json',
        'status': 'DRAFT_SHAPE_IMPORTED_UV_REJECTED_PENDING_V08B_REPLACEMENT',
        'validation': 'Source symmetry and shape proof checked. This fragmented UV candidate was rejected by the user; fresh-FBX technical checks and replacement UV are pending. No PBR or final native acceptance.'}})
