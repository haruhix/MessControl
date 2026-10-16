"""Read-only audit of branching cracks, unchanged crowns and editor state."""
import unreal as u,json,hashlib
from pathlib import Path
root=Path(u.Paths.project_dir()).resolve();out=root/'Artifacts/ToothBranchCracks_20261017'
protected=json.loads((out/'ProtectedFiles.json').read_text())
changed=[p for p,h in protected.items() if hashlib.sha256((root/p).read_bytes()).hexdigest()!=h]
before=json.loads((out/'Before.json').read_text());old_variants={r['variant']:r for r in json.loads((out/'VariantsBefore.json').read_text())}
variants=json.loads((root/'ContentSource/Meshes/ToothDamageV10/Variants.json').read_text())
geometry_same=all(abs(r['closed_enamel_volume']-old_variants[r['variant']]['closed_enamel_volume'])<1e-8 and r['fracture_faces']==old_variants[r['variant']]['fracture_faces'] and r['uv_layers']==old_variants[r['variant']]['uv_layers'] for r in variants)
branches=all(r['branch_stats']['forks']>20 and r['crack_faces']>400 for r in variants)
mat=u.load_asset('/Game/Art/Materials/M_ArenaTooth');e=u.MaterialEditingLibrary
custom=[str(n.get_editor_property('code')) for n in e.get_material_expressions(mat) if isinstance(n,u.MaterialExpressionCustom)]
shader_ok=len(custom)==2 and all('sin(' not in c and 'Flash*.55' in c and 'float3 base = Enamel' in c for c in custom)
ss=u.get_editor_subsystem(u.StaticMeshEditorSubsystem)
for r in variants:
 mesh=u.load_asset('/Game/Gameplay/Arena/V10/Damage/'+r['variant'])
 assert mesh and mesh.get_editor_property('allow_cpu_access') and ss.get_num_uv_channels(mesh,0)==3
 index=next(i for i,s in enumerate(mesh.get_editor_property('static_materials')) if str(s.material_slot_name)=='MC_Damage_Enamel')
 assert mesh.get_material(index)==mat
dirty=[p.get_path_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
solo=json.loads((out/'SoloCheck.json').read_text());compile_report=json.loads((out/'MaterialFix.json').read_text())
checks=dict(protected_assets_unchanged=not changed,all_24_crown_volumes_caps_and_uv_channels_preserved=len(variants)==24 and geometry_same,
 all_24_variants_have_branched_networks=branches,legacy_wave_overlay_removed_flash_retained=shader_ok,material_compiled=not compile_report['compile_errors'],
 solo_damage_stages_spray_repair_and_loss_passed=solo['passed'],PIE_stopped=not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor(),
 preexisting_unsaved_packages_preserved=set(dirty)==set(before['dirty']))
report=dict(passed=all(checks.values()),checks=checks,protected_files=len(protected),unexpected_changes=changed,dirty=dirty,
 crack_quads_min=min(r['crack_faces'] for r in variants),crack_quads_max=max(r['crack_faces'] for r in variants))
(out/'FinalAudit.json').write_text(json.dumps(report,indent=2))
print('TOOTH_BRANCH_CRACKS_AUDIT',report['passed'],checks);assert report['passed']
