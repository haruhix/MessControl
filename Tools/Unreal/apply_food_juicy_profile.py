"""Assign the reviewed saved food VFX profile through VFX Composer's writer.

Only the existing scoped mouth-library Food.AutoMouth binding is saved. Prior
profiles/systems remain intact and can be reassigned through the same writer.
"""
import hashlib,json,shutil
from pathlib import Path
import unreal as u

OUT=Path('E:/CodexWork/MessControl/Validation/FoodJuicyVFX_20261109')
ROOT=Path('E:/DEVGAME/MessControl')
NEW='/Game/Gameplay/Food/VFX/Juicy/DA_FoodJuicy_Pulp'
NS='/Game/Gameplay/Food/VFX/Juicy/NS_FoodJuicy_Pulp.NS_FoodJuicy_Pulp'
LIB='/Game/Gameplay/VFX/Profiles/DA_MouthPresentation'
L=u.MCPerkPreviewEditorLibrary

def values(asset,name):return {str(k):v.export_text() for k,v in asset.get_editor_property(name).items()}
def disk(asset):return ROOT/('Content/'+asset.get_path_name().split('.')[0].removeprefix('/Game/')+'.uasset')
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def run():
    assert not u.EditorLevelLibrary.get_pie_worlds(True)
    assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages()
    assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages()
    assert not json.loads(L.get_perk_preview_snapshot_json()).get('running')
    proof=OUT/'Composer/20261109T121740Z/Report.json'
    assert json.loads(proof.read_text(encoding='utf8'))['passed'],'Real Composer scenarios must pass first'
    library=u.load_asset(LIB);profile=u.load_asset(NEW)
    assert library and profile
    bindings=dict(library.get_editor_property('profiles'));old=bindings[u.Name('Food.AutoMouth')]
    assert old and old!=profile,'Preserve the first integration evidence'
    old_slots=values(old,'slots');old_sfx=values(old,'sfx_slots');old_groups=values(old,'event_groups')
    assert values(profile,'sfx_slots')==old_sfx and values(profile,'event_groups')==old_groups
    slots=dict(profile.get_editor_property('slots'))
    assert {str(k) for k in slots}=={'Consume','Discard'}
    assert all(v.get_editor_property('system').get_path_name()==NS for v in slots.values())
    backups=[]
    for asset in (library,old):
        source=disk(asset);target=OUT/'BeforeIntegration'/source.relative_to(ROOT)
        assert not target.exists(),'Preserve prior backup'
        target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,target)
        assert sha(source)==sha(target)
        backups.append(dict(source=str(source),backup=str(target),sha256=sha(source)))
    message=L.assign_saved_profile_to_source(u.Name('Food.AutoMouth'),profile)
    after=dict(library.get_editor_property('profiles'))
    assert after[u.Name('Food.AutoMouth')]==profile,message
    assert set(after)==set(bindings)
    assert all(after[k]==v for k,v in bindings.items() if str(k)!='Food.AutoMouth')
    assert values(old,'slots')==old_slots and values(old,'sfx_slots')==old_sfx and values(old,'event_groups')==old_groups
    assert sha(disk(old))==backups[1]['sha256']
    assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages()
    report=dict(passed=True,new_profile=profile.get_path_name(),previous_profile=old.get_path_name(),
        message=str(message),source='Food.AutoMouth',backups=backups,
        other_bindings_preserved=True,prior_artwork_preserved=True,sfx_and_groups_preserved=True,
        runtime_root='Existing scoped DA_MouthPresentation -> saved food profile -> NS -> mesh/material',
        rollback='Assign previous_profile through MCPerkPreviewEditorLibrary.assign_saved_profile_to_source',
        main_gameplay_check_pending=True)
    (OUT/'Integration.json').write_text(json.dumps(report,indent=2),encoding='utf8')
    print('FOOD_JUICY_PROFILE_ASSIGNED '+profile.get_path_name())

run()
