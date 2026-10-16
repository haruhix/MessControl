"""Read-only saved-file, profile coverage and editor-restoration audit."""
import unreal as u,json,hashlib
from pathlib import Path
root=Path(u.Paths.project_dir()).resolve();out=root/'Artifacts/ToothDamageV10_20261017'
protected=json.loads((out/'ProtectedFiles.json').read_text())
changed=[p for p,h in protected.items() if hashlib.sha256((root/p).read_bytes()).hexdigest()!=h]
before=json.loads((out/'Before.json').read_text())
profile=u.load_asset('/Game/Gameplay/Arena/Damage/DA_ToothDamage')
rows=[{k:r.get_editor_property(k).get_path_name() for k in ('Intact','Chipped','Broken')} for r in profile.get_editor_property('Teeth')]
sources=json.loads((root/'ContentSource/Meshes/ToothDamageV10/Sources.json').read_text())
variants=json.loads((root/'ContentSource/Meshes/ToothDamageV10/Variants.json').read_text())
ss=u.get_editor_subsystem(u.StaticMeshEditorSubsystem);mat=u.load_asset('/Game/Art/Materials/M_ArenaTooth')
mesh_checks=[]
for source in sources:
 matches=[r for r in rows if r['Intact']==source['mesh']];assert len(matches)==1,source['mesh']
 for stage in ('Chipped','Broken'):
  mesh=u.load_asset(matches[0][stage]);slots=mesh.get_editor_property('static_materials')
  index=next(i for i,s in enumerate(slots) if str(s.material_slot_name)=='MC_Damage_Enamel')
  assert mesh.get_material(index)==mat and mesh.get_editor_property('allow_cpu_access')
  assert ss.get_num_uv_channels(mesh,0)==3
  assert mesh.get_editor_property('body_setup').get_editor_property('collision_trace_flag')==u.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE
  mesh_checks.append(mesh.get_path_name())
dirty=[p.get_path_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
solo=json.loads((out/'SoloCheck.json').read_text())
checks={'all_protected_files_unchanged':not changed,'all_eight_old_profile_rows_retained':all(r in rows for r in before['profile']),
 'all_twelve_new_meshes_covered_once':len(mesh_checks)==24,'all_24_closed_volumes_and_three_UVs_authored':len(variants)==24 and all(v['closed_enamel_volume']>0 and len(v['uv_layers'])==3 for v in variants),
 'solo_damage_repair_and_knockout_passed':solo['passed'],'editor_not_in_PIE':not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor(),
 'preexisting_dirty_packages_preserved':set(dirty)==set(before['dirty'])}
report=dict(passed=all(checks.values()),checks=checks,protected_files=len(protected),unexpected_changes=changed,profile_rows=len(rows),verified_damage_meshes=mesh_checks,dirty=dirty)
(out/'FinalAudit.json').write_text(json.dumps(report,indent=2))
print('V10_DAMAGE_AUDIT',report['passed'],checks)
assert report['passed']
