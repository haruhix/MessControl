"""Import only the explicitly reviewed replacement UV packet into the v08 map."""
import hashlib
import json
from pathlib import Path
import runpy
import unreal as u

root = Path(u.Paths.project_dir()).resolve()
request = json.loads((root / 'Tools/Unreal/Menu3D/Evidence/v08b_geometry_binding_request.json').read_text(encoding='utf8'))
assert request['revision'] == 'v08b_SeamUV'
assert request['candidate'] == 'Candidates/Final96_GumMinStretch_LocalFangRepair'
assert request['visual_uv_review'] == 'PASS'
assert request['geometry_unchanged'] is True
assert request['technical_uv_review'] in ('PASS', 'PASS_WITH_BOUNDED_NOTES')
assert request['notes'], 'Record the actual review scope, including hidden seam limits'
world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
assert world and world.get_path_name().split('.')[0] == '/Game/UI/MainMenu3D/L_MainMenu3D_RefFinish_v08'
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
folder = root / 'ArtSource/UI/MainMenu3D/20261015_refB_v1/Assembly/UnrealHandoff' / request['revision'] / request['candidate']
assert hashlib.sha256((folder / 'SK_DAY03.fbx').read_bytes()).hexdigest() == request['fbx_sha256']
runpy.run_path(str(root / 'Tools/Unreal/Menu3D/import_vampire_symmetric_v06.py'),
    init_globals={'MC_DAY03_IMPORT_REQUEST': {
        'revision': request['revision'], 'candidate': request['candidate'],
        'mesh_name': 'SK_DAY03_Reference_v08b', 'fbx_sha256': request['fbx_sha256'],
        'report_name': 'reference_v08b_import_report.json',
        'status': 'REVIEWED_UV_CANDIDATE_IMPORTED_PENDING_PBR_AND_NATIVE_MOTION_REVIEW',
        'validation': request['notes']}})
