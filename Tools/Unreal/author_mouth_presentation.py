"""Author the first location-owned non-perk VFX binding, after an Editor rebuild.

Run explicitly in an idle rebuilt Editor. This recipe preserves ordinary nut
death's current appearance: canonical Death stays UseDefault. The real dust NS
is only an optional, separately saved replacement proof; it is NOT assigned to
the canonical library. Existing artist assignments and unrelated entries remain.

MC_PRESENTATION_AUTHOR_PROOF=True also creates an unbound runtime-class proof
profile under Tests/VFXPractice/Profiles for a bounded override scenario.
MC_PRESENTATION_AUTHOR_OUTPUT redirects the JSON report, and must be on E:.
MC_PRESENTATION_AUTHOR_KNIGHT_SHIELD=True also adds the native knight's Hit/Break
profile, preserving the complete current shield cue through UseDefault.
MC_PRESENTATION_AUTHOR_MAGE_FIREBALL=True adds Charge/Release/Flight/Impact,
preserving native presentation until the author explicitly applies an override.
"""
import hashlib
import json
from pathlib import Path

import unreal as u

ROOT = Path(u.Paths.project_dir()).resolve()
OUT = Path(globals().get(
    'MC_PRESENTATION_AUTHOR_OUTPUT',
    'E:/CodexWork/MessControl/Validation/WorldVFXPresentation_20261031/Authoring')).resolve()
assert ROOT.as_posix().lower() == 'e:/devgame/messcontrol', 'Wrong Unreal project'
assert OUT.drive.upper() == 'E:', 'Agent outputs must be on E:'
assert not u.EditorLevelLibrary.get_pie_worlds(True), 'Preserve the current Play session'
assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages(), 'Preserve unsaved maps'
assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages(), 'Preserve unsaved assets'

SOURCE_ID = 'EditorOnly.NutEnemyDeath'  # Stable runtime ID; not an editor-only asset/class.
FOLDER = '/Game/Gameplay/VFX/Profiles'
LIBRARY = FOLDER + '/DA_MouthPresentation'
PROFILE = FOLDER + '/DA_NutEnemyDeath'
DUST = '/Game/Gameplay/VFX/NutCombat/NS_NutDustImpact'
PROOF = '/Game/Tests/VFXPractice/Profiles/DA_NutEnemyDeath_DustProof'
CREATE_PROOF = bool(globals().get('MC_PRESENTATION_AUTHOR_PROOF', False))
OUT.mkdir(parents=True, exist_ok=True)

protected = [
    'Content/Blueprints/BP_PlayerCharacter.uasset',
    'Content/Gameplay/Roguelike/DT_Perks.uasset',
    'Content/Gameplay/Hub/Economy/DT_HubCatalog.uasset',
    'Content/Gameplay/VFX/NutCombat/NS_NutDustImpact.uasset',
]


def hashes():
    return {p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest()
            for p in protected if (ROOT / p).is_file()}


report = dict(source_id=SOURCE_ID, initial_canonical_mode='UseDefault',
              created=[], saved=[],
              existing_artist_assignments_preserved=[], protected_before=hashes(),
              proof_assigned_to_game=False)
tools = u.AssetToolsHelpers.get_asset_tools()


def load_or_create(path, cls):
    existing = u.load_asset(path) if u.EditorAssetLibrary.does_asset_exist(path) else None
    if existing:
        assert existing.get_class() == cls.static_class(), 'Unexpected source class: ' + path
        return existing, False
    factory = u.DataAssetFactory()
    factory.set_editor_property('data_asset_class', cls)
    name = path.rsplit('/', 1)[-1]
    result = tools.create_asset(name, path.rsplit('/', 1)[0], cls, factory)
    assert result, 'Failed to create ' + path
    report['created'].append(path)
    return result, True


def save(asset, path):
    assert u.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False), 'Failed to save ' + path
    report['saved'].append(path)


try:
    library, new_library = load_or_create(LIBRARY, u.MCVFXPresentationLibrary)
    profile, new_profile = load_or_create(PROFILE, u.MCPerkVFXProfile)
    if new_profile:
        slot = u.MCPerkVFXSlot()
        slot.set_editor_property('mode', u.MCPerkVFXSlotMode.USE_DEFAULT)
        profile.set_editor_property('slots', {u.Name('Death'): slot})
        save(profile, PROFILE)
    else:
        # Never reset an existing artist profile to the recipe's initial defaults.
        report['existing_artist_assignments_preserved'].append(PROFILE)

    authored_slots = dict(profile.get_editor_property('slots'))
    assert set(str(k) for k in authored_slots) <= {'Death'}, 'Existing profile has events outside ordinary nut death'
    report['named_profile_modes'] = {str(k): str(v.get_editor_property('mode')) for k, v in authored_slots.items()}

    profiles = dict(library.get_editor_property('profiles'))
    if u.Name(SOURCE_ID) not in profiles:
        profiles[u.Name(SOURCE_ID)] = profile
        library.set_editor_property('profiles', profiles)
        save(library, LIBRARY)  # Save the canonical owner last.
    else:
        # An existing value, including explicit None, belongs to the author.
        report['existing_artist_assignments_preserved'].append(LIBRARY + ':' + SOURCE_ID)
        if new_library:
            raise AssertionError('A newly created library unexpectedly already contains the source')

    if bool(globals().get('MC_PRESENTATION_AUTHOR_KNIGHT_SHIELD', False)):
        shield_path = FOLDER + '/DA_NutKnightShield'
        shield, new_shield = load_or_create(shield_path, u.MCPerkVFXProfile)
        if new_shield:
            shield.set_editor_property('slots', {
                u.Name('Hit'): u.MCPerkVFXSlot(), u.Name('Break'): u.MCPerkVFXSlot()})
            save(shield, shield_path)
        else:
            report['existing_artist_assignments_preserved'].append(shield_path)
        assert set(map(str, shield.get_editor_property('slots'))) <= {'Hit', 'Break'}
        profiles = dict(library.get_editor_property('profiles'))
        shield_key = u.Name('Boss.NutKnight.Shield')
        if shield_key not in profiles:
            profiles[shield_key] = shield
            library.set_editor_property('profiles', profiles)
            save(library, LIBRARY)
        else:
            report['existing_artist_assignments_preserved'].append(LIBRARY + ':' + str(shield_key))
        report['knight_shield_profile'] = shield_path

    if bool(globals().get('MC_PRESENTATION_AUTHOR_MAGE_FIREBALL', False)):
        mage_path = FOLDER + '/DA_NutMageFireball'
        mage, new_mage = load_or_create(mage_path, u.MCPerkVFXProfile)
        mage_events = {'Charge', 'Release', 'Flight', 'Impact'}
        if new_mage:
            mage.set_editor_property('slots', {
                u.Name(event): u.MCPerkVFXSlot() for event in sorted(mage_events)})
            save(mage, mage_path)
        else:
            report['existing_artist_assignments_preserved'].append(mage_path)
        assert set(map(str, mage.get_editor_property('slots'))) <= mage_events
        profiles = dict(library.get_editor_property('profiles'))
        mage_key = u.Name('Boss.NutMage.Fireball')
        if mage_key not in profiles:
            profiles[mage_key] = mage
            library.set_editor_property('profiles', profiles)
            save(library, LIBRARY)
        else:
            report['existing_artist_assignments_preserved'].append(LIBRARY + ':' + str(mage_key))
        report['mage_fireball_profile'] = mage_path

    if CREATE_PROOF:
        dust = u.load_asset(DUST)
        assert dust and isinstance(dust, u.NiagaraSystem), 'Missing native dust source'
        proof, new_proof = load_or_create(PROOF, u.MCPerkVFXProfile)
        if new_proof:
            replacement = u.MCPerkVFXSlot()
            replacement.set_editor_property('mode', u.MCPerkVFXSlotMode.REPLACE)
            replacement.set_editor_property('system', dust)
            proof.set_editor_property('slots', {u.Name('Death'): replacement})
            save(proof, PROOF)
        else:
            report['existing_artist_assignments_preserved'].append(PROOF)
        report['proof_profile'] = PROOF
        report['proof_limit'] = 'Dust-only replacement; native body shrink remains. Not full legacy cue/art parity.'

    report['protected_after'] = hashes()
    assert report['protected_after'] == report['protected_before'], 'Protected source changed'
    report['ok'] = True
except Exception as error:
    report['ok'] = False
    report['error'] = str(error)
    report['protected_after'] = hashes()
    raise
finally:
    (OUT / 'AuthorMouthPresentation.json').write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    u.log(json.dumps(report, ensure_ascii=False))
