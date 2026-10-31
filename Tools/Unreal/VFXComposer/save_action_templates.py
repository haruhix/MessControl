"""Run through remote_python.py after author_action_templates.py; save owned starter NS/recipes."""
import json
from pathlib import Path
import unreal as u

OUT=Path('E:/CodexWork/MessControl/Validation/ComposerActionWorkspace_20261031/Templates')
lib=u.MCPerkPreviewEditorLibrary
assert not u.EditorLevelLibrary.get_pie_worlds(True)
# The independent ingredient editor remains the master recipe authoring API.
if hasattr(lib,'open_vfx_elements'):lib.open_vfx_elements()
else:lib.open_vfx_composer()
rows=[]
for kind in ('Flash','Smoke','Trail','Glow'):
    path='/Game/Tests/VFXPractice/Effects/NS_Template_'+kind
    system=u.load_asset(path)
    assert system,path
    u.EditorAssetLibrary.set_metadata_tag(system,'MC.ActionTemplate','20261031')
    assert u.EditorAssetLibrary.save_loaded_asset(system,only_if_is_dirty=False)
    recipe_path='/Game/Tests/VFXPractice/Templates/DA_Template_'+kind
    if u.EditorAssetLibrary.does_asset_exist(recipe_path):
        saved=u.load_asset(recipe_path)
        assert len(saved.get_editor_property('layers'))==1
        props=json.loads(u.ToolsetLibrary.get_object_properties(saved,['Layers']))
        assert props['Layers'][0]['system']['refPath']==system.get_path_name(),props
    else:
        assert lib.composer_load_recipe(u.new_object(u.MCVFXComposition))
        assert lib.composer_add_system(system)
        result=lib.save_working_vfx_recipe_as(recipe_path+'.'+recipe_path.rsplit('/',1)[-1])
        saved,error=result if isinstance(result,tuple) else (result,'')
        assert saved,error
    result=u.MCVFXComposerBuilderLibrary.validate_recipe(saved)
    if isinstance(result,tuple):valid,error=result
    elif isinstance(result,str):error=result;valid=not error
    else:valid=bool(result);error=''
    rows.append(dict(kind=kind,system=system.get_path_name(),recipe=saved.get_path_name(),valid=valid,error=error))
    assert valid,(kind,error)
(OUT/'SavedRecipes.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2),encoding='utf8')
print(json.dumps(rows,ensure_ascii=False))
