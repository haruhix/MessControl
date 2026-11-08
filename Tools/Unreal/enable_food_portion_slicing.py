"""Enable CPU slicing only for oversized ordinary menu meshes found by the audit.

Run in an idle project Editor, after audit_food_portion_cpu_meshes.py. Saves only those meshes;
never changes the menu, geometry, materials, proportions or collision settings.
"""
import hashlib
import json
from pathlib import Path
import unreal as u

OUT=Path('E:/CodexWork/MessControl/Validation/FoodPortions_20261107')
ROOT=Path('E:/DEVGAME/MessControl')
assert not u.EditorLevelLibrary.get_pie_worlds(True), 'End owned PIE before saving meshes'
audit=json.loads((OUT/'CpuMeshCoverage.json').read_text(encoding='utf8'))
assert audit['complete'] and audit['table_payload_unchanged']
paths=sorted({v['path'] for key in ('unsupported_oversized_whole_uses','unsupported_oversized_fragment_uses') for v in audit[key]})
dirty={p.get_path_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
assert all(p.split('.')[0] not in dirty for p in paths), 'Preserve unsaved mesh edits'
report=dict(changed=[],complete=False,menu_unchanged=False)
table=u.load_asset('/Game/Data/DT_BreakfastMenu')
before=u.DataTableFunctionLibrary.export_data_table_to_json_string(table)
(OUT/'CpuMeshCoverage.Before.json').write_text(json.dumps(audit,indent=2)+'\n',encoding='utf8')
try:
    for path in paths:
        mesh=u.load_asset(path)
        assert mesh and not mesh.get_editor_property('allow_cpu_access'), path
        package=path.split('.')[0]
        filename=ROOT/'Content'/(package.removeprefix('/Game/')+'.uasset')
        old_hash=hashlib.sha256(filename.read_bytes()).hexdigest()
        mesh.set_editor_property('allow_cpu_access',True)
        assert mesh.get_editor_property('allow_cpu_access'), path
        assert u.EditorAssetLibrary.save_loaded_asset(mesh,only_if_is_dirty=True), path
        new_hash=hashlib.sha256(filename.read_bytes()).hexdigest()
        assert new_hash!=old_hash, 'Saved package did not change: '+path
        report['changed'].append(dict(path=path,before_sha256=old_hash,after_sha256=new_hash,
            setting='bAllowCPUAccess',value=True))
    report['menu_unchanged']=before==u.DataTableFunctionLibrary.export_data_table_to_json_string(table)
    assert report['menu_unchanged']
    report['complete']=True
finally:
    (OUT/'EnableCpuSlicing.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')
print('MC_FOOD_PORTION_CPU_ENABLED',len(report['changed']),report['complete'])
