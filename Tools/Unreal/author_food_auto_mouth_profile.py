"""Bind the Food.AutoMouth Consume effect in the existing scoped mouth library.

Run in an idle Editor through remote_python.py. Preserve all existing profiles,
and save only the new profile and the one mouth-library map entry.
"""
import json,hashlib
from pathlib import Path
import unreal as u

OUT=Path('E:/CodexWork/MessControl/Validation/FoodAutoMouth_20261108')
ROOT='/Game/Gameplay/Food/VFX'
PROFILE=ROOT+'/DA_Food_AutoMouth';SYSTEM=ROOT+'/NS_Food_AutoMouth'
LIBRARY='/Game/Gameplay/VFX/Profiles/DA_MouthPresentation'
assert not u.EditorLevelLibrary.get_pie_worlds(True)
assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages()
assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages()
lib=u.EditorAssetLibrary;tools=u.AssetToolsHelpers.get_asset_tools()
library=u.load_asset(LIBRARY);system=u.load_asset(SYSTEM);assert library and system
before=dict(library.get_editor_property('profiles'))
source=Path('E:/DEVGAME/MessControl/Content/Gameplay/VFX/Profiles/DA_MouthPresentation.uasset')
backup=OUT/'BeforePackages/DA_MouthPresentation.uasset'
backup.parent.mkdir(parents=True,exist_ok=True)
if not backup.exists():backup.write_bytes(source.read_bytes())
profile=u.load_asset(PROFILE) if lib.does_asset_exist(PROFILE) else None
if not profile:
 factory=u.DataAssetFactory();factory.set_editor_property('data_asset_class',u.MCPerkVFXProfile)
 profile=tools.create_asset('DA_Food_AutoMouth',ROOT,u.MCPerkVFXProfile,factory);assert profile
 slot=u.MCPerkVFXSlot();slot.set_editor_properties(dict(mode=u.MCPerkVFXSlotMode.REPLACE,system=system))
 profile.set_editor_property('slots',{u.Name('Consume'):slot})
 assert lib.save_loaded_asset(profile,only_if_is_dirty=False)
slots=dict(profile.get_editor_property('slots'));assert set(map(str,slots))=={'Consume'}
assert slots[u.Name('Consume')].get_editor_property('mode')==u.MCPerkVFXSlotMode.REPLACE
profiles=dict(before)
if u.Name('Food.AutoMouth') not in profiles:
 profiles[u.Name('Food.AutoMouth')]=profile;library.set_editor_property('profiles',profiles)
 assert lib.save_loaded_asset(library,only_if_is_dirty=False)
after=dict(library.get_editor_property('profiles'))
assert all(after[k]==v for k,v in before.items()),'Unrelated profile binding changed'
assert u.Name('Food.AutoMouth') in after
assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages()
report=dict(profile=PROFILE,system=SYSTEM,event='Consume',library=LIBRARY,source_id='Food.AutoMouth',
 before_profiles=len(before),after_profiles=len(after),unrelated_bindings_preserved=True,
 backup=str(backup),saved=True,source_registration='Root integrates native contract and scoped preload; no global catalog preload')
(OUT/'Profile.json').write_text(json.dumps(report,indent=2),encoding='utf8')
u.log('FOOD_AUTOMOUTH_PROFILE '+json.dumps(report))
