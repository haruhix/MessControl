"""Read-only API/editor guard for task-owned menu import, no package mutation."""
import json
from pathlib import Path
import unreal as u

report = {'dirty_maps':[p.get_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_map_packages()],
          'dirty_content':[p.get_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()]}
for name in ['FbxImportUI','FbxSkeletalMeshImportData','FbxStaticMeshImportData','PostProcessSettings','CameraComponent','SkeletalMesh','MaterialEditingLibrary']:
    cls = getattr(u,name)
    report[name] = {'methods':[x for x in dir(cls) if any(s in x for s in ('morph','material','import','shading','tone_curve','exposure','ortho','bounds'))]}
    try:
        ob = cls()
        report[name]['defaults'] = str(ob)
    except Exception:
        pass
char = u.load_asset('/Game/Gameplay/CharacterCurrent/SK_CharacterGameplay')
report['character_bounds'] = str(char.get_bounds())
report['character_morphs'] = [str(x) for x in char.get_all_morph_target_names()]
out = Path(u.Paths.project_dir())/'ArtSource/UI/MainMenu3D/20261015_refB_v1/Assembly/UnrealHandoff'
out.mkdir(parents=True,exist_ok=True)
(out/'native_api_inspection.json').write_text(json.dumps(report,indent=2),encoding='utf8')
u.log('MC_MENU3D_NATIVE_API '+json.dumps(report))
