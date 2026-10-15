"""Read-only saved-package dependency audit for the native menu commit."""
import json
from pathlib import Path
import unreal as u

root=Path(u.Paths.project_dir()).resolve()
assert str(root).replace('\\','/').lower()=='e:/devgame/messcontrol'
base='/Game/UI/MainMenu3D/'
registry=u.AssetRegistryHelpers.get_asset_registry()
options=u.AssetRegistryDependencyOptions()
for name in ('include_hard_package_references','include_soft_package_references',
             'include_searchable_names','include_hard_management_references','include_soft_management_references'):
    options.set_editor_property(name,True)
queue=[base+x for x in ('L_MainMenu3D','WBP_MainMenu3D','BP_Menu3DPlayerController','BP_Menu3DGameMode')]
seen=set();edges={}
while queue:
    asset=queue.pop(0)
    if asset in seen:continue
    seen.add(asset)
    dependencies=sorted(str(x) for x in registry.get_dependencies(asset,options))
    edges[asset]=dependencies
    queue.extend(x for x in dependencies if x.startswith(base) and x not in seen)
external=sorted(set(x for deps in edges.values() for x in deps if x.startswith('/Game/') and not x.startswith(base)))
report={'project':str(root),'source':'Live asset registry saved-package dependencies; no package edits or refresh/build',
    'task_assets':sorted(seen),'external_game_dependencies':external,'edges':edges}
out=root/'Tools/Unreal/Menu3D/Evidence/saved_dependency_closure.json'
out.write_text(json.dumps(report,indent=2),encoding='utf8')
u.log('MC_MENU_PUSH_DEPENDENCIES '+json.dumps(report))
