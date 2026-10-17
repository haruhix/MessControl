"""Save the agreed five-second Knight stun and existing shield-free mesh binding.

Writes only DA_NutRain, after read-only mesh inspection. All other profile fields
and packages are preserved. Package bytes and the complete settings are backed up.
"""
import datetime
import hashlib
import json
import shutil
from pathlib import Path
import unreal as u

ROOT = Path(u.Paths.project_dir()).resolve()
PROFILE = '/Game/Gameplay/CoreLoop/DA_NutRain'
MESH = '/Game/Gameplay/CoreLoop/NutTank/SK_NutTank'
BARE = '/Game/Gameplay/CoreLoop/NutTank/SK_NutTank_NoShield'
OUT = ROOT / 'Saved/ManualIntake/ShieldInspection'


def path(obj):
    return obj.get_path_name().split('.')[0] if obj else None


def dirty():
    return set(p.get_name() for p in list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages())
               + list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages()))


def main():
    assert not u.EditorLevelLibrary.get_pie_worlds(False), 'Stop PIE before profile authoring'
    before_dirty = dirty()
    assert PROFILE not in before_dirty, 'DA_NutRain has unsaved edits'
    inspection = json.loads((OUT / 'SavedAssets.json').read_text(encoding='utf-8'))
    assert inspection['same_skeleton'] and not inspection['variant_is_same_object'], 'Mesh diagnosis must pass first'
    profile = u.load_asset(PROFILE)
    mesh, bare = u.load_asset(MESH), u.load_asset(BARE)
    assert isinstance(profile, u.MCNutRainProfile)
    assert isinstance(mesh, u.SkeletalMesh) and isinstance(bare, u.SkeletalMesh)
    assert mesh != bare and mesh.get_editor_property('skeleton') == bare.get_editor_property('skeleton')
    original = profile.get_editor_property('settings').copy()
    settings = original.copy()
    boss = settings.get_editor_property('boss').copy()
    old_stun = float(boss.get_editor_property('tank_stun_seconds'))
    old_bare = boss.get_editor_property('tank_unshielded_skeletal_mesh')
    assert path(boss.get_editor_property('tank_skeletal_mesh')) == MESH, 'Custom Knight mesh requires separate review'
    assert path(old_bare) in (None, MESH, BARE), 'Preserve custom shield-free mesh binding'
    assert abs(old_stun - 3.) < .001 or abs(old_stun - 5.) < .001, 'Preserve custom stun tuning'
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    backup = OUT / ('ProfileBackup_' + stamp)
    backup.mkdir(parents=True, exist_ok=False)
    package = ROOT / 'Content/Gameplay/CoreLoop/DA_NutRain.uasset'
    shutil.copy2(package, backup / package.name)
    before_sha = hashlib.sha256(package.read_bytes()).hexdigest()
    (backup / 'Before.json').write_text(json.dumps(dict(profile=PROFILE, sha256=before_sha,
        settings=original.export_text(), stun_seconds=old_stun, unshielded_mesh=path(old_bare)), indent=2), encoding='utf-8')
    boss.set_editor_property('tank_stun_seconds', 5.)
    boss.set_editor_property('tank_unshielded_skeletal_mesh', bare)
    settings.set_editor_property('boss', boss)
    preserved = settings.copy()
    unchanged = boss.copy()
    unchanged.set_editor_property('tank_stun_seconds', old_stun)
    unchanged.set_editor_property('tank_unshielded_skeletal_mesh', old_bare)
    preserved.set_editor_property('boss', unchanged)
    assert preserved.export_text() == original.export_text(), 'Unrelated profile tuning changed'
    if settings.export_text() != original.export_text():
        profile.modify()
        profile.set_editor_property('settings', settings)
        assert u.EditorAssetLibrary.save_loaded_asset(profile, only_if_is_dirty=False)
    assert dirty() == before_dirty, 'Unexpected dirty packages after profile save'
    actual = profile.get_editor_property('settings').get_editor_property('boss')
    assert abs(actual.get_editor_property('tank_stun_seconds') - 5.) < .001
    assert path(actual.get_editor_property('tank_unshielded_skeletal_mesh')) == BARE
    report = dict(passed=True, profile=PROFILE, backup=str(backup), before_sha256=before_sha,
                  after_sha256=hashlib.sha256(package.read_bytes()).hexdigest(),
                  stun_seconds=5., unshielded_mesh=BARE, unrelated_settings_preserved=True,
                  validation='Saved profile and same-skeleton binding only; solo gameplay validation is separate.')
    (OUT / 'ProfileAuthoring.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print('KNIGHT_SHIELD_PROFILE_READY', json.dumps(report))


if __name__ == '__main__':
    main()
