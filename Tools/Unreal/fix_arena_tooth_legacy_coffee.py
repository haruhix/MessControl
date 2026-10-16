"""Remove the obsolete flat Coffee pattern; native projected plaque owns dirt."""
import unreal as u,json,hashlib,shutil
from pathlib import Path

root=Path(u.Paths.project_dir());out=root/'Artifacts/ArenaToothSpots_20261017';out.mkdir(exist_ok=True)
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
lib=u.EditorAssetLibrary;e=u.MaterialEditingLibrary
material=lib.load_asset('/Game/Art/Materials/M_ArenaTooth');assert material
old_blend='float3 base = lerp(Enamel,Stain*(.8+.2*sin(P.z*.3)),patch);'
new_blend='float3 base = Enamel; // Native projected grime supplies the visible plaque.'
nodes=[n for n in e.get_material_expressions(material) if isinstance(n,u.MaterialExpressionCustom) and old_blend in str(n.get_editor_property('code'))]
assert nodes,'Inspect an already modified material before reapplying'
protected=[]
for folder in ['Content/Gameplay/Arena/V10','Content/FromBlender10']:
 protected.extend((root/folder).rglob('*.uasset'))
protected.extend(root/p for p in ['Content/Art/Materials/Arena/MI_Gum_v2.uasset','Content/Data/DA_ArenaTooth.uasset','Content/Data/DA_RunRules.uasset','Content/Data/DA_Tongue.uasset','Content/Gameplay/Care/M_ArenaToothRelief.uasset'])
hashes={str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in protected}
report=dict(material=material.get_path_name(),nodes=[],protected_hashes=hashes,dirty_before=[p.get_path_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()])
backup=out/'M_ArenaTooth_Before.uasset'
if not backup.exists():shutil.copy2(root/'Content/Art/Materials/M_ArenaTooth.uasset',backup)
with u.ScopedEditorTransaction('Remove duplicate legacy coffee spots from arena enamel'):
 for n in nodes:
  old=str(n.get_editor_property('code'));new=old.replace(old_blend,new_blend)
  report['nodes'].append(dict(name=n.get_name(),before=old,after=new))
  n.set_editor_property('code',new)
 errors=list(e.recompile_material(material));report['compile_errors']=errors
 if errors:
  for n,row in zip(nodes,report['nodes']):n.set_editor_property('code',row['before'])
  e.recompile_material(material)
  raise RuntimeError(errors)
 assert lib.save_loaded_asset(material,only_if_is_dirty=False)
report['protected_differences']=[p for p,h in hashes.items() if hashlib.sha256((root/p).read_bytes()).hexdigest()!=h]
report['dirty_after']=[p.get_path_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
report['passed']=not report['protected_differences'] and not errors
(out/'MaterialFix.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print('ARENA_LEGACY_COFFEE_FIXED',len(nodes),'nodes','errors',errors,'protected differences',report['protected_differences'])
assert report['passed']
