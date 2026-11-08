"""Guarded existing-clothing commerce migration. Default is a dry plan.

Offline (no Unreal import): plan_saved(). In an idle current-schema Editor: run().
Explicit application: run(apply=True). Root owns all Editor/build leases.
Only DT_CharacterCosmetics is imported/saved. DT_HubCatalog is read, hash-checked
and backed up, never changed. IDs, fit/mesh/material/enable/unlocked fields remain
verbatim. Canonical captions retain Hub native key/source while explicitly adopting
the target package localization namespace before import, avoiding save-time random keys.
All working data/backups/reports stay E:. No actors/profile/input/PIE/build actions.
"""
from __future__ import annotations
import copy
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import sys
import traceback
import uuid

sys.dont_write_bytecode = True
os.environ['TEMP'] = os.environ['TMP'] = 'E:/CodexTemp/MessControl-Clothing'
PROJECT = Path('E:/DEVGAME/MessControl')
SNAPSHOTS = Path('E:/CodexWork/MessControl/Validation/Clothing_20261104')
OUTPUT = SNAPSHOTS / 'Migration'
COSMETICS = '/Game/Data/DT_CharacterCosmetics'
HUB = '/Game/Gameplay/Hub/Economy/DT_HubCatalog'
WARDROBE_CLASS = '/Game/Gameplay/UI/V2/WBP_UIWardrobe.WBP_UIWardrobe_C'
ROW_STRUCT = '/Script/MessControl.MCCosmeticCatalogRow'
HUB_STRUCT = '/Script/MessControl.MCHubCatalogRow'
OLD_FIELDS = ('Name','Slot','DisplayName','Description','Mesh','AttachBone','bMeshSpaceFit','LocalTransform','Materials','bEnabled','bDefaultUnlocked')
PROTECTED = ('Name','Slot','Mesh','AttachBone','bMeshSpaceFit','LocalTransform','Materials','bEnabled','bDefaultUnlocked')
NEW_DEFAULTS = {'bUseCanonicalCommerce':False,'ShopItemId':'None','DisplayNameKey':'None','DescriptionKey':'None','Icon':'None',
    'BasePrice':15,'DiscountPercent':0,'Rarity':'Common','bInShop':True,'ShopAppearanceChance':1.0,'SortOrder':0}
NEW_FIELDS = tuple(NEW_DEFAULTS)
RARITIES = {'Common','Rare','Epic','Legendary'}
NONE = ('','None','none','null','NULL')


class MigrationError(RuntimeError):
    pass


def need(condition, message):
    if not condition:
        raise MigrationError(message)


def on_e(value):
    result = Path(value).resolve()
    need(result.drive.upper() == 'E:', 'Working/output path must stay E: ' + str(result))
    return result


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def canonical(value):
    return json.dumps(value,ensure_ascii=False,sort_keys=True,separators=(',',':'))


def write(path, value):
    path=on_e(path);path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')


def read(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def indexed(rows, label):
    need(isinstance(rows,list), label + ' is not a row array')
    result={}
    for row in rows:
        need(isinstance(row,dict) and isinstance(row.get('Name'),str) and row['Name'] not in NONE, label+' missing row name')
        key=row['Name'].casefold()
        need(key not in result, label+' case-insensitive/FName ID collision: '+row['Name'])
        result[key]=row
    return result


def object_path(value):
    need(isinstance(value,str), 'Asset reference is not a native exported string: '+repr(value))
    if value in NONE:return None
    if value.startswith('<Object '):
        match=re.match(r"<Object '([^']+)' .* Class 'Texture2D'>$",value)
        need(match is not None,'Unsupported/foreign PSD icon object: '+value);value=match.group(1)
    elif not value.startswith('/Game/'):
        match=re.fullmatch(r"[^']+'(/Game/[^']+)'",value)
        need(match is not None,'Unsupported asset reference: '+value);value=match.group(1)
    need(value.startswith('/Game/') and ':' not in value and '..' not in value.split('/'), 'Expected standalone /Game asset: '+value)
    need('.' in value and not value.endswith('.'), 'Asset lacks object name: '+value)
    return value


def package_file(asset):
    path=object_path(asset) if '.' in str(asset) else str(asset)
    need(path is not None and path.startswith('/Game/'), 'Not a /Game package: '+str(asset))
    package=path.split('.',1)[0]
    candidate=on_e(PROJECT/'Content'/package.removeprefix('/Game/')).with_suffix('.uasset')
    need(candidate.is_relative_to((PROJECT/'Content').resolve()), 'Asset path escaped project Content')
    return candidate


def normalized_overrides(meta):
    result={}
    for ident,value in meta.get('psdIconOverrides',{}).items():
        key=str(ident).casefold();need(key not in result,'Duplicate PSD override ID: '+str(ident))
        result[key]=object_path(value)
    return result


def native_text_content(value):
    if not isinstance(value,str):return ''
    if value.startswith(('NSLOCTEXT','LOCTEXT','INVTEXT')):
        parts=re.findall(r'"((?:\\.|[^"\\])*)"',value)
        need(bool(parts),'Malformed native FText export')
        return json.loads('"'+parts[-1].replace(chr(92)+"'","'")+'"')
    return value


def native_text_identity(value):
    """Parse only a native NSLOCTEXT export; keep the source/key tokens untouched."""
    need(isinstance(value,str),'Native FText export is not a string')
    match=re.fullmatch(r'NSLOCTEXT\(\s*("(?:\\.|[^"\\])*")\s*,\s*("(?:\\.|[^"\\])*")\s*,\s*("(?:\\.|[^"\\])*")\s*\)',value)
    need(match is not None,'Expected explicit native NSLOCTEXT, no raw-string reconstruction: '+value)
    return match,json.loads(match.group(1)),json.loads(match.group(2))


def target_package_namespace(cosmetics):
    observed=set()
    for row in cosmetics:
        for field in ('DisplayName','Description'):
            _,namespace,_=native_text_identity(row[field])
            suffix=re.search(r'\[([0-9A-Fa-f]{32})\]$',namespace)
            need(suffix is not None,'Original target FText lacks an unambiguous native package namespace: '+row['Name']+'.'+field)
            observed.add(suffix.group(1))
    need(len(observed)==1,'Original target native package namespace is ambiguous: '+str(sorted(observed)))
    return next(iter(observed))


def target_owned_ftext(value,package_namespace):
    # Native TextNamespaceUtil::BuildFullNamespace replaces text between the last
    # '[' and trailing ']'. Pre-owning it prevents TextHistory_Base::Serialize
    # assigning a new random key on persistent package save. Never alter key/source.
    match,namespace,key=native_text_identity(value)
    suffix=re.search(r'\[([0-9A-Fa-f]{32})\]$',namespace)
    need(suffix is not None,'Hub FText lacks explicit native package namespace')
    owned=namespace[:suffix.start(1)]+package_namespace+namespace[suffix.end(1):]
    result=value[:match.start(1)]+json.dumps(owned,ensure_ascii=False)+value[match.end(1):]
    check,owned_namespace,owned_key=native_text_identity(result)
    need(owned_key==key and check.group(3)==match.group(3),'FText key/source unexpectedly changed while adopting target namespace')
    need(owned_namespace==owned,'Target namespace conversion failed')
    return result


def fit_source_valid(row):
    need(row.get('Slot') not in NONE and row.get('AttachBone') not in NONE,'Missing existing slot/bone: '+row['Name'])
    mesh=object_path(row.get('Mesh','None'));need(mesh and package_file(mesh).is_file(),'Missing real existing mesh: '+row['Name'])
    for material in row.get('Materials',[]):
        ref=object_path(material);need(ref and package_file(ref).is_file(),'Missing existing material: '+row['Name'])
    transform=row['LocalTransform'];q=transform.get('Rotation',{})
    for key,axes in (('Rotation',('X','Y','Z','W')),('Translation',('X','Y','Z')),('Scale3D',('X','Y','Z'))):
        need(all(type(transform.get(key,{}).get(axis)) in (int,float) and math.isfinite(transform[key][axis]) for axis in axes),'Invalid native transform: '+row['Name']+'.'+key)
    need(abs(sum(q[k]*q[k] for k in ('X','Y','Z','W'))-1.)<.01,'Unnormalized native rotation: '+row['Name'])


def snapshot_inputs():
    meta=read(SNAPSHOTS/'SavedTablesBefore.json')
    cosmetics=read(SNAPSHOTS/'DT_CharacterCosmetics.Before.json')
    hub=read(SNAPSHOTS/'DT_HubCatalog.Before.json')
    need(len(cosmetics)==10 and len(hub)==55,'Expected provided 10/55 native saved snapshot; refresh scope explicitly if changed')
    return cosmetics,hub,meta


def planned_rows(cosmetics, hub, meta, *, current_rows=None):
    old=indexed(cosmetics,'saved cosmetics');offers=indexed(hub,'saved Hub catalog');overrides=normalized_overrides(meta)
    target_namespace=target_package_namespace(cosmetics)
    need(set(overrides)<=set(old),'PSD override points at a missing cosmetic ID: '+str(sorted(set(overrides)-set(old))))
    result=[];links=[];used={}
    for ident,source in old.items():
        need(set(OLD_FIELDS)<=set(source),'Missing saved geometry/identity field for '+source['Name'])
        linked=[row for row in hub if str(row.get('CosmeticID','')).casefold()==ident]
        need(len(linked)==1,'Cosmetic '+source['Name']+' needs exactly one Hub link; found '+str([r['Name'] for r in linked]))
        offer=linked[0]
        fit_source_valid(source)
        need(offer.get('Kind')=='Cosmetic','Wrong Hub kind for '+source['Name']+': '+str(offer.get('Kind')))
        need(type(source['bEnabled']) is bool and type(offer.get('bEnabled')) is bool and type(offer.get('bInShop')) is bool,'Native enabled/shop flags must be booleans: '+source['Name'])
        need(source['bEnabled']==offer.get('bEnabled'),'Enabled conflict cannot preserve source and effective offer: '+source['Name'])
        public=offer['Name'];public_key=public.casefold()
        need(public_key not in used,'ShopItemId collision: '+public+' already used by '+used.get(public_key,''))
        need(offers.get(public_key) is offer,'Public Hub ID collision: '+public)
        used[public_key]=source['Name']
        price=offer.get('Price');need(type(price) is int and 0<=price<=2147483647,'Invalid existing price for '+public)
        need(offer.get('Rarity') in RARITIES,'Invalid rarity for '+public)
        for text_field,key_field in (('DisplayName','DisplayNameKey'),('Description','DescriptionKey')):
            need(bool(native_text_content(offer.get(text_field,'')).strip()),'Empty real offer '+text_field+': '+public)
            need(isinstance(offer.get(key_field),str) and offer[key_field] not in NONE,'Missing stable '+key_field+': '+public)
        icon=overrides.get(ident) or object_path(offer.get('Icon','None'))
        need(icon is not None,'Real offer icon required, no fallback invented: '+public)
        icon_file=package_file(icon);need(icon_file.is_file(),'Required real icon package missing: '+str(icon_file))
        target=copy.deepcopy(source)
        target.update(bUseCanonicalCommerce=True,ShopItemId=public,DisplayName=target_owned_ftext(offer['DisplayName'],target_namespace),Description=target_owned_ftext(offer['Description'],target_namespace),
            DisplayNameKey=offer['DisplayNameKey'],DescriptionKey=offer['DescriptionKey'],Icon=icon,BasePrice=price,DiscountPercent=0,
            Rarity=offer['Rarity'],bInShop=bool(offer['bInShop']),ShopAppearanceChance=1.0,SortOrder=offer['SortOrder'])
        need(all(target[k]==source[k] for k in PROTECTED),'Protected field changed in planner: '+source['Name'])
        result.append(target)
        links.append({'cosmeticId':source['Name'],'shopItemId':public,'basePrice':price,'effectivePrice':price,'discountPercent':0,
            'rarity':offer['Rarity'],'chance':1.0,'icon':icon,'iconSource':'saved_PSD_override' if ident in overrides else 'existing_Hub_offer',
            'iconPackageSHA256':sha(icon_file),'replacedFallbackFields':[k for k in ('DisplayName','Description') if source[k]!=target[k]],
            'nativeHubFallbackSource':{k:offer[k] for k in ('DisplayName','Description')},
            'targetOwnedFallbacks':{k:target[k] for k in ('DisplayName','Description')},
            'targetPackageNamespace':target_namespace,
            'replacedFallbackIdentities':{k:{'before':source[k],'hubSource':offer[k],'after':target[k]} for k in ('DisplayName','Description') if source[k]!=target[k]}})
    expected=indexed(result,'planned canonical rows')
    if current_rows is not None:
        current=indexed(current_rows,'live cosmetics')
        need(set(current)==set(old),'Live cosmetic row IDs added/removed; refusing implicit migration scope change')
        merged=[]
        for row in current_rows:
            ident=row['Name'].casefold();seed=old[ident];target=expected[ident]
            for field in PROTECTED:
                need(row.get(field)==seed[field],'Protected live '+row['Name']+'.'+field+' changed since snapshot; refresh/review before migration')
            canonical_row=bool(row.get('bUseCanonicalCommerce',False))
            if canonical_row:
                for field in ('DisplayName','Description')+NEW_FIELDS:
                    need(row.get(field)==target[field],'Already canonical '+row['Name']+'.'+field+' differs; refusing to overwrite authored post-migration changes')
            else:
                for field in ('DisplayName','Description'):
                    need(row.get(field)==seed[field],'Unmigrated native FText changed since snapshot: '+row['Name']+'.'+field)
                for field,default in NEW_DEFAULTS.items():
                    need(field not in row or row[field]==default,'Unmigrated authored commerce would be overwritten: '+row['Name']+'.'+field)
            extra=set(row)-set(OLD_FIELDS)-set(NEW_FIELDS)
            need(not extra,'Unknown live row fields require schema review: '+str(sorted(extra)))
            merged.append(copy.deepcopy(target))
        result=merged
    return result,{'links':links,'idsUnchanged':True,'geometryFieldsVerbatim':list(PROTECTED),'hubNeverImported':True,
        'oldClothingChancePreservedByOne':True,'rarityDoesNotInventChance':True,'noncosmeticPayloadSHA256':hashlib.sha256(canonical([r for r in hub if r['Kind']!='Cosmetic']).encode()).hexdigest()}


def new_output(output_dir):
    output=on_e(output_dir)/(datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')+'_'+uuid.uuid4().hex[:8])
    output.mkdir(parents=True,exist_ok=False);return output


def plan_saved(*, output_dir=OUTPUT):
    output=new_output(output_dir);old,hub,meta=snapshot_inputs()
    planned,report=planned_rows(old,hub,meta)
    report.update(status='OFFLINE_DRY_PLAN',apply=False,editorExecuted=False,output=str(output),
        snapshots={str(p):sha(p) for p in (SNAPSHOTS/'SavedTablesBefore.json',SNAPSHOTS/'DT_CharacterCosmetics.Before.json',SNAPSHOTS/'DT_HubCatalog.Before.json')})
    report['savedPackageGuards']=[{'asset':x['asset'],'expected':x['savedSHA256'],'current':sha(package_file(x['asset'])),'matches':sha(package_file(x['asset']))==x['savedSHA256']} for x in meta['tables']]
    need(all(x['matches'] for x in report['savedPackageGuards']),'Saved target/Hub package changed since authoritative snapshot')
    write(output/'PlannedCosmeticRows.json',planned);write(output/'Report.json',report);return report


def dirty_packages(u):
    return {p.get_path_name() for p in list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages())+list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())}


def idle(u):
    need(not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor(),'PIE/SIE is preserved; migration requires idle Editor')


def native_prices(u, planned):
    need(callable(getattr(getattr(u,'MCCosmeticCatalog',None),'effective_price',None)),'Fresh native EffectivePrice API unavailable')
    checks=[]
    for base,discount,expected in [(0,0,0),(15,33,10),(1,99,0),(2147483647,0,2147483647),(2147483647,100,0),(-1,0,-1),(1,101,-1)]+[(r['BasePrice'],r['DiscountPercent'],r['BasePrice']) for r in planned]:
        fixture=u.MCCosmeticCatalogRow();fixture.set_editor_property('base_price',base);fixture.set_editor_property('discount_percent',discount)
        actual=int(u.MCCosmeticCatalog.effective_price(fixture));need(actual==expected,'Native price contract differs: '+str((base,discount,actual,expected)))
        checks.append({'base':base,'discount':discount,'expected':expected,'actual':actual})
    return checks


def roundtrip(expected, actual):
    left=indexed(expected,'expected roundtrip');right=indexed(actual,'actual roundtrip')
    differences=[]
    for ident in sorted(set(left)|set(right)):
        if ident not in left or ident not in right:
            differences.append(ident+'.<row membership>');continue
        for field in sorted(set(left[ident])|set(right[ident])):
            if left[ident].get(field)!=right[ident].get(field):differences.append(left[ident]['Name']+'.'+field)
    need(not differences,'Native roundtrip changed payload/FText/geometry at '+str(differences[:40]))


def backups(output, meta):
    records=[]
    for entry in meta['tables']:
        source=package_file(entry['asset'])
        for suffix in ('.uasset','.uexp','.ubulk'):
            item=source.with_suffix(suffix)
            if not item.is_file():continue
            dest=on_e(output/'BeforePackages'/item.relative_to(PROJECT));dest.parent.mkdir(parents=True,exist_ok=True)
            need(not dest.exists(),'Backup already exists: '+str(dest));shutil.copy2(item,dest)
            need(sha(item)==sha(dest),'Backup verification failed: '+str(item))
            records.append({'source':str(item),'backup':str(dest),'sha256':sha(dest)})
    need(sum(x['source'].endswith('.uasset') for x in records)==2,'Both saved tables require verified E backups')
    return records


def expected_original_with_schema_defaults(old):
    result=[]
    for row in old:
        need(set(row)==set(OLD_FIELDS),'Original snapshot has unexpected fields; recovery schema needs review')
        result.append(dict(copy.deepcopy(row),**copy.deepcopy(NEW_DEFAULTS)))
    return result


def verify_recovery_baseline(path,current_rows,current_hash,hub_hash,old,hub,meta):
    path=on_e(path);data=read(path)
    need(data.get('kind')=='ClothingLogicalRollbackBaselineV1','Not an explicit reviewed logical-rollback baseline')
    need(data.get('target')==COSMETICS and data.get('hub')==HUB,'Recovery baseline table scope differs')
    expected_hash={x['asset']:x['savedSHA256'] for x in meta['tables']}
    need(data.get('originalTargetSHA256')==expected_hash[COSMETICS] and data.get('hubSHA256')==expected_hash[HUB]==hub_hash,'Recovery original/Hub bytes are not pinned to saved evidence')
    need(data.get('recoveredTargetSHA256')==current_hash,'Recovered target bytes changed since explicit baseline capture')
    expected=expected_original_with_schema_defaults(old)
    roundtrip(expected,current_rows)
    need(data.get('rows')==expected,'Recovery row evidence must match original native identities/fit and only new schema defaults')
    failure_path=on_e(data['failureReport']);failure=read(failure_path)
    need(sha(failure_path)==data.get('failureReportSHA256'),'Recovery failure report changed')
    need(failure.get('status')=='FAILED' and failure.get('apply') is True and failure.get('logicalRollbackVerified') is True and failure.get('logicalRollbackSaved') is True,'Recovery is not backed by verified saved logical rollback')
    original=on_e(data['originalBinaryBackup'])
    need(original.is_file() and sha(original)==expected_hash[COSMETICS],'Verified exact original binary backup missing or changed')
    need(any(x.get('backup')==str(original) and x.get('sha256')==expected_hash[COSMETICS] for x in failure.get('packageBackups',[])),'Recovery original backup is not the failed scoped operation backup')
    return {'file':str(path),'sha256':sha(path),'recoveredTargetSHA256':current_hash,'originalTargetSHA256':expected_hash[COSMETICS],'allOriginalRowsAndNewDefaultsExact':True}


def capture_verified_rollback_baseline(*, failure_report_file, output_dir=OUTPUT):
    """Explicit read-only Editor capture. Never imports/saves assets or invokes gameplay."""
    import unreal as u
    idle(u);need(on_e(u.Paths.project_dir())==PROJECT.resolve(),'Wrong project checkout')
    old,hub,meta=snapshot_inputs();dirty_before=dirty_packages(u)
    need(not {COSMETICS,HUB}&dirty_before,'Recovery requires both tables clean; preserve unsaved edits')
    failure_path=on_e(failure_report_file);failure=read(failure_path)
    need(failure.get('status')=='FAILED' and failure.get('apply') is True and failure.get('logicalRollbackVerified') is True and failure.get('logicalRollbackSaved') is True,'No verified saved logical rollback report')
    target=u.load_asset(COSMETICS);hub_asset=u.load_asset(HUB)
    need(isinstance(target,u.DataTable) and isinstance(hub_asset,u.DataTable),'Recovery requires actual existing tables')
    schema=u.DataTableFunctionLibrary.get_data_table_row_struct(target)
    need(schema and schema.get_path_name()==ROW_STRUCT,'Recovery row schema differs')
    before=json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(target))
    roundtrip(expected_original_with_schema_defaults(old),before)
    roundtrip(hub,json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(hub_asset)))
    expected_hash={x['asset']:x['savedSHA256'] for x in meta['tables']}
    need(sha(package_file(HUB))==expected_hash[HUB],'Hub bytes changed during failed migration')
    original=next((x for x in failure.get('packageBackups',[]) if x.get('source')==str(package_file(COSMETICS)) and x.get('sha256')==expected_hash[COSMETICS]),None)
    need(original is not None,'Original binary backup provenance missing')
    backup=on_e(original['backup']);need(backup.is_file() and sha(backup)==expected_hash[COSMETICS],'Original binary backup is not intact')
    data={'kind':'ClothingLogicalRollbackBaselineV1','target':COSMETICS,'hub':HUB,'rows':expected_original_with_schema_defaults(old),
        'originalTargetSHA256':expected_hash[COSMETICS],'recoveredTargetSHA256':sha(package_file(COSMETICS)),
        'hubSHA256':expected_hash[HUB],'originalBinaryBackup':str(backup),'failureReport':str(failure_path),'failureReportSHA256':sha(failure_path),
        'nativeEditorReadOnlyCapture':True,'assetImportSaveGameplayInvoked':False}
    output=new_output(output_dir);dest=output/'VerifiedRollbackBaseline.json'
    need(dirty_packages(u)==dirty_before,'Editor dirty state changed during recovery capture')
    write(dest,data)
    return str(dest)


def run(*, apply=False, output_dir=OUTPUT, recovery_baseline_file=None):
    need(type(apply) is bool,'Explicit apply must be a boolean; default remains dry-plan')
    import unreal as u
    output=new_output(output_dir);old,hub,meta=snapshot_inputs()
    report={'status':'PREFLIGHT','apply':bool(apply),'output':str(output),'changedAssets':[],'packageBackups':[],'stateMutations':[],
        'gameplayProfileTouched':False,'sourceBuildPIEStarted':False,'nativeCommerceValidationDirect':False}
    target=before=None;mutated=saved=False
    try:
        idle(u);need(on_e(u.Paths.project_dir())==PROJECT.resolve(),'Wrong project checkout')
        dirty_before=dirty_packages(u);report['dirtyBefore']=sorted(dirty_before)
        need(not {COSMETICS,HUB}&dirty_before,'Source/target tables have unsaved edits; preserve them before migration')
        target=u.load_asset(COSMETICS);hub_asset=u.load_asset(HUB)
        need(isinstance(target,u.DataTable) and isinstance(hub_asset,u.DataTable),'Expected two existing DataTables')
        schema=u.DataTableFunctionLibrary.get_data_table_row_struct(target)
        need(schema and schema.get_path_name()==ROW_STRUCT,'Unexpected cosmetic row struct')
        hs=u.DataTableFunctionLibrary.get_data_table_row_struct(hub_asset);need(hs and hs.get_path_name()==HUB_STRUCT,'Unexpected Hub row struct')
        before=json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(target))
        live_hub=json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(hub_asset))
        need(indexed(live_hub,'live Hub')==indexed(hub,'saved Hub'),'Hub row payload changed since native snapshot; refresh plan, do not guess')
        planned,merge=planned_rows(old,hub,meta,current_rows=before);report['merge']=merge
        columns={str(x) for x in u.DataTableFunctionLibrary.get_data_table_column_names(target)}
        schema_ready=set(NEW_FIELDS)<=columns and callable(getattr(getattr(u,'MCCosmeticCatalog',None),'effective_price',None))
        report['currentSchemaReady']=schema_ready;report['columns']=sorted(columns)
        current_hash=sha(package_file(COSMETICS));hub_hash=sha(package_file(HUB));expected_hash={x['asset']:x['savedSHA256'] for x in meta['tables']}
        need(hub_hash==expected_hash[HUB],'Saved Hub bytes changed since snapshot')
        already=indexed(before,'live before')==indexed(planned,'canonical plan')
        recovery=None
        if recovery_baseline_file is not None and not already:
            recovery=verify_recovery_baseline(recovery_baseline_file,before,current_hash,hub_hash,old,hub,meta)
            report['explicitRecoveredBaseline']=recovery
        need(current_hash==expected_hash[COSMETICS] or already or recovery is not None,'Saved cosmetics changed outside this exact already-applied migration; explicit verified logical-rollback baseline required')
        report['idempotentAlreadyApplied']=already
        # Verify actual saved wardrobe overrides, not a private replacement icon list.
        for icon in normalized_overrides(meta).values():need(isinstance(u.load_asset(icon),u.Texture2D),'PSD icon is not a real Texture2D: '+icon)
        cls=u.load_class(None,WARDROBE_CLASS);need(cls is not None,'Actual saved WBP_UIWardrobe class missing')
        actual={str(k).casefold():v.get_path_name() for k,v in u.get_default_object(cls).get_editor_property('cosmetic_icon_art').items() if v is not None}
        need(actual==normalized_overrides(meta),'Actual wardrobe PSD icon map changed since native snapshot')
        for link in merge['links']:need(isinstance(u.load_asset(link['icon']),u.Texture2D),'Required offer icon is not a real Texture2D: '+link['icon'])
        write(output/'BeforeRows.json',before);write(output/'HubBeforeRows.json',live_hub);write(output/'PlannedCosmeticRows.json',planned)
        if not apply:
            report['status']='LIVE_DRY_PLAN';return report
        need(schema_ready,'Build/load current canonical clothing schema before explicit apply')
        helper=getattr(getattr(u,'MCClothingFitEditorLibrary',None),'inspect_open_preview',None)
        need(callable(helper),'Native idle-row-editor guard unavailable; no unsafe Python editor-pointer guess')
        editor_state=json.loads(helper(target.get_path_name(),u.Name('None')));report['targetEditorState']=editor_state
        need(editor_state.get('tableLoaded') is True and editor_state.get('tableEditorOpen') is False,'Close target DataTable editor before whole-row import; preserve other editors. Native guard must report tableLoaded/tableEditorOpen.')
        report['nativePriceChecks']=native_prices(u,planned)
        report['packageBackups']=backups(output,meta);write(output/'Report.json',report)
        idle(u);need(dirty_packages(u)==dirty_before,'Editor dirty state changed during preflight; no mutation performed')
        if not already:
            with u.ScopedEditorTransaction('Migrate existing clothing commerce to its canonical table'):
                target.modify();mutated=True
                need(u.DataTableFunctionLibrary.fill_data_table_from_json_string(target,json.dumps(planned,ensure_ascii=False),schema),'Canonical DataTable import failed')
                after=json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(target));write(output/'AfterImportRows.json',after);roundtrip(planned,after)
                need(indexed(planned_rows(old,hub,meta,current_rows=after)[0],'second plan')==indexed(after,'imported canonical rows'),'Migration is not idempotent')
                need(dirty_packages(u)<=dirty_before|{COSMETICS},'Unrelated package became dirty; preserving it')
                idle(u);need(sha(package_file(HUB))==hub_hash,'Hub package changed before target save')
                need(u.EditorAssetLibrary.save_loaded_asset(target,only_if_is_dirty=False),'Target DataTable save failed');saved=True;report['changedAssets']=[COSMETICS]
        after=json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(target));write(output/'AfterSaveRows.json',after);roundtrip(planned,after)
        need(indexed(json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(hub_asset)),'Hub after')==indexed(live_hub,'Hub before'),'Hub payload changed')
        need(sha(package_file(HUB))==hub_hash,'Hub saved bytes changed')
        write(output/'AfterRows.json',after);report['savedTargetSHA256']=sha(package_file(COSMETICS));report['hubSHA256Unchanged']=hub_hash
        report['status']='IDEMPOTENT_NO_SAVE' if already else 'APPLIED_VERIFIED';return report
    except Exception:
        report.update(status='FAILED',error=traceback.format_exc())
        if mutated and target is not None and before is not None:
            try:
                need(u.DataTableFunctionLibrary.fill_data_table_from_json_string(target,json.dumps(before,ensure_ascii=False),schema),'Target logical rollback import failed')
                roundtrip(before,json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(target)))
                disk_changed = 'current_hash' in locals() and sha(package_file(COSMETICS))!=current_hash
                if saved or disk_changed:need(u.EditorAssetLibrary.save_loaded_asset(target,only_if_is_dirty=False),'Logical rollback save failed')
                report['logicalRollbackSaved']=bool(saved or disk_changed)
                report['restoredInMemoryOnly']=not bool(saved or disk_changed)
                report['logicalRollbackVerified']=True
                report['exactBinaryRecovery']='Original bytes remain in verified E backups. Do not replace files behind the loaded Editor; close/unload safely for exact offline recovery if needed.'
            except Exception:
                report['rollbackError']=traceback.format_exc();report['manualRecoveryRequired']=True
        raise
    finally:
        report['dirtyAfter']=sorted(dirty_packages(u));write(output/'Report.json',report)


if __name__=='__main__':
    # Editor ExecutePythonScript defaults to a dry plan; the offline flag never imports Unreal.
    options=globals().get('MC_CLOTHING_MIGRATION_OPTIONS',{})
    need(not options.get('offline',False) or not options.get('apply',False),'Offline mode never applies assets')
    result=plan_saved(**{k:v for k,v in options.items() if k not in ('offline','apply')}) if options.get('offline',False) else run(**options)
