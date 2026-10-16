"""Back up changed assets and remove only the repeating sine crack overlay."""
import unreal as u,json,hashlib,shutil
from pathlib import Path
root=Path(u.Paths.project_dir()).resolve();out=root/'Artifacts/ToothBranchCracks_20261017';out.mkdir(parents=True,exist_ok=True)
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
backup=out/'BeforeAssets';backup.mkdir(exist_ok=True)
changed=['Content/Art/Materials/M_ArenaTooth.uasset']+[p.relative_to(root).as_posix() for p in (root/'Content/Gameplay/Arena/V10/Damage').glob('*.uasset')]
for rel in changed:
 dest=backup/Path(rel).name
 if not dest.exists():shutil.copy2(root/rel,dest)
shutil.copy2(root/'ContentSource/Meshes/ToothDamageV10/Variants.json',out/'VariantsBefore.json')
protected={p.relative_to(root).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for folder in ['Content/Gameplay/Arena','Content/FromBlender10','Content/Art/Materials','Content/Data'] for p in (root/folder).rglob('*.uasset') if p.relative_to(root).as_posix() not in changed}
protected['Content/Maps/L_Mouth.umap']=hashlib.sha256((root/'Content/Maps/L_Mouth.umap').read_bytes()).hexdigest()
(out/'ProtectedFiles.json').write_text(json.dumps(protected,indent=2))
mat=u.load_asset('/Game/Art/Materials/M_ArenaTooth');e=u.MaterialEditingLibrary
needle='float crack = 1-smoothstep(.025,.075,abs(sin(P.z*.13+P.y*.085+sin(P.y*.16)*.8)));'
nodes=[n for n in e.get_material_expressions(mat) if isinstance(n,u.MaterialExpressionCustom) and needle in str(n.get_editor_property('code'))]
assert len(nodes)==2
report=[]
with u.ScopedEditorTransaction('Replace repeating tooth crack overlay with branching geometry'):
 for n in nodes:
  code=str(n.get_editor_property('code'))
  new='\nfloat3 base = Enamel; // Native plaque and authored branching cracks supply surface detail.\nbase = lerp(base,float3(1,.3,.08),Flash*.55);\nreturn base;\n'
  report.append(dict(node=n.get_name(),before=code,after=new));n.set_editor_property('code',new)
 errors=list(e.recompile_material(mat))
 if errors:
  for n,r in zip(nodes,report):n.set_editor_property('code',r['before'])
  e.recompile_material(mat);raise RuntimeError(errors)
 assert u.EditorAssetLibrary.save_loaded_asset(mat)
(out/'MaterialFix.json').write_text(json.dumps(dict(nodes=report,compile_errors=errors),indent=2))
print('REPEATING_TOOTH_CRACK_OVERLAY_REMOVED',len(nodes),'nodes')
