"""Run one native isolated authority/data validation in an idle Editor.
No PIE, live profile/player, source row edits, table saves, build or Git.
The native fixture itself creates/destroys an unplayed transient preview world.
"""
from pathlib import Path
from datetime import datetime, timezone
import hashlib, json, uuid
import unreal as u

ROOT=Path('E:/DEVGAME/MessControl')
OUT=Path(globals().get('MC_CLOTHING_COMMERCE_OUTPUT',
    'E:/CodexWork/MessControl/Validation/Clothing_20261104/NativeCommerce/'+datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')+'_'+uuid.uuid4().hex[:8])).resolve()
assert OUT.drive.upper()=='E:' and not OUT.exists(), 'Use a fresh E: output directory'
assert not u.EditorLevelLibrary.get_pie_worlds(True), 'Idle Editor, no PIE'
assert hasattr(u,'MCClothingCommerceValidationLibrary'), 'Compile the new Editor library first'
OUT.mkdir(parents=True)
paths={
 '/Game/Data/DT_CharacterCosmetics':ROOT/'Content/Data/DT_CharacterCosmetics.uasset',
 '/Game/Gameplay/Hub/Economy/DT_HubCatalog':ROOT/'Content/Gameplay/Hub/Economy/DT_HubCatalog.uasset'}
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def dirty():return sorted(x.get_path_name() for x in list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages())+list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages()))
report={'passed':False,'scope':'Native isolated authority/data contracts; not PIE, controller RPC/proximity, fitted visuals or real UI input.'}
try:
 for asset in paths:
  table=u.load_asset(asset)
  assert table, 'Missing saved canonical source: '+asset
 before_sha={asset:sha(path) for asset,path in paths.items()};before_dirty=dirty()
 report['native']=json.loads(u.MCClothingCommerceValidationLibrary.validate_transient_commerce())
 report['sourcePackagesBefore']=before_sha
 report['sourcePackagesAfter']={asset:sha(path) for asset,path in paths.items()}
 report['dirtyPackagesBefore']=before_dirty;report['dirtyPackagesAfter']=dirty()
 assert report['native'].get('passed'), report['native']
 assert report['sourcePackagesAfter']==before_sha and report['dirtyPackagesAfter']==before_dirty, 'Read-only validation changed source disk/dirty state'
 report['passed']=True
finally:
 (OUT/'Report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
u.log('MC_CLOTHING_NATIVE_COMMERCE '+str(report['passed'])+' '+str(OUT/'Report.json'))
