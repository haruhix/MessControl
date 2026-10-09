import json
from pathlib import Path
import unreal as u

mat = u.load_asset('/Game/Gameplay/Cold/M_PlayerIceCoating')
u.get_editor_subsystem(u.AssetEditorSubsystem).close_all_editors_for_asset(mat)
assert mat.get_blend_mode() == u.BlendMode.BLEND_MASKED
assert not mat.get_editor_property('two_sided')
errors = list(u.MaterialEditingLibrary.recompile_material(mat))
assert not errors, errors
assert u.EditorAssetLibrary.save_loaded_asset(mat, only_if_is_dirty=False)
out = Path(u.Paths.project_dir())/'Artifacts/PlayerFrostMask'
task = u.AssetExportTask()
task.set_editor_properties({'object': mat, 'filename': (out/'MaterialGraph.copy').as_posix(),
    'automated': True, 'prompt': False, 'replace_identical': True, 'exporter': u.ObjectExporterT3D()})
assert u.Exporter.run_asset_export_task(task)
root_line = next(line.strip() for line in (out/'MaterialGraph.copy').read_text(encoding='utf-8-sig').splitlines() if line.strip().startswith('OpacityMask='))
assert 'Expression=' in root_line and 'UseConstant=True' not in root_line, root_line
report = json.loads((out/'MaterialBuild.json').read_text(encoding='utf-8'))
report.update(compile_errors=errors, effective_blend_mode='Masked', opacity_mask_inline_constant_disabled=True, opacity_mask_saved_input=root_line)
(out/'MaterialBuild.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
print('FROST_FINAL_MASK_VERIFIED', json.dumps(report))
