"""Bind artist wizard clips and their full server timings; preserve walking."""
import unreal as u,json,hashlib,importlib.util
from pathlib import Path
root=Path(u.Paths.project_dir()).resolve();out=root/'Artifacts/NutWizardUpdate_20261017'
folder='/Game/Gameplay/CoreLoop/NutWizard/ArtistUpdate_20261017'
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
before=json.loads((out/'EditorBefore.json').read_text());report=json.loads((out/'ImportReport.json').read_text());assert report['passed']
profile=u.load_asset('/Game/Gameplay/CoreLoop/DA_NutRain')
settings=profile.get_editor_property('settings').copy();boss=settings.get_editor_property('boss').copy()
assert boss.get_editor_property('MageWalkAnimation').get_path_name()==before['bindings']['mage_walk_animation']
mapping={'Idle':'MageIdleAnimation','Hit':'MageHitAnimation','Death':'MageDeathAnimation','Cast':'MageCastAnimation','Summon':'MageSummonAnimation','Rain':'MageRainAnimation','Teleport':'MageTeleportAnimation'}
lengths={}
for label,field in mapping.items():
 clip=u.load_asset(folder+'/Animations/A_NutWizard_'+label);assert clip
 boss.set_editor_property(field,clip);lengths[label]=clip.get_play_length()
boss.set_editor_property('MageSkeletalMesh',u.load_asset(report['derived_mesh']))
boss.set_editor_property('AnimationBlendSeconds',.25)
for label,field in {'Cast':'MageCastAnimationSeconds','Summon':'MageSummonAnimationSeconds','Rain':'MageRainAnimationSeconds','Teleport':'MageTeleportAnimationSeconds','Hit':'MageHitAnimationSeconds'}.items():boss.set_editor_property(field,lengths[label])
release=boss.get_editor_property('MageCastReleaseFraction')
spec=importlib.util.spec_from_file_location('mc_wizard_cast_binding',Path(__file__).with_name('connect_nut_bosses.py'))
cast_helper=importlib.util.module_from_spec(spec);spec.loader.exec_module(cast_helper)
boss.set_editor_property('MageCastOffset',cast_helper.measure_cast_offset(boss.get_editor_property('MageSkeletalMesh'),boss.get_editor_property('MageCastAnimation'),boss.get_editor_property('MageHeight'),release,boss.get_editor_property('MageModelYaw')))
boss.set_editor_properties(dict(DeathSeconds=lengths['Death']+.25,FireballWindup=lengths['Cast']*release,RainWindup=lengths['Rain']*release,MageTeleportWindup=lengths['Teleport']*release,MageTurnDegreesPerSecond=180.))
settings.set_editor_property('boss',boss);profile.set_editor_property('settings',settings)
assert u.EditorAssetLibrary.save_loaded_asset(profile)
unchanged=[p for p,h in before['protected_hashes'].items() if hashlib.sha256((root/p).read_bytes()).hexdigest()!=h]
assert not unchanged,unchanged
data=dict(passed=True,bindings={field:boss.get_editor_property(field).get_path_name() for field in mapping.values()},walking_preserved=boss.get_editor_property('MageWalkAnimation').get_path_name(),lengths=lengths,blend=.25,profile=boss.export_text(),protected_assets_unchanged=True)
(out/'Bindings.json').write_text(json.dumps(data,indent=2));print('WIZARD_UPDATE_BOUND',lengths,'blend .25, walking preserved')
