"""Connect the supplied knight clips, preserving existing walk and ball bindings."""
import unreal as u,json,hashlib
from pathlib import Path
ROOT=Path(u.Paths.project_dir());OUT=ROOT/'Artifacts/NutKnightAnimations_20261016'
def main():
 assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
 assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages()
 assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages()
 baseline=json.loads((OUT/'Baseline.json').read_text())
 profile=u.load_asset('/Game/Gameplay/CoreLoop/DA_NutRain');settings=profile.get_editor_property('settings');boss=settings.get_editor_property('boss')
 bindings={'idle':'Idle','unshielded_idle':'Idle','melee':'Attack1','melee2':'Attack2','combo':'Combo','block':'Block','hit':'Hit','shield_break':'Stun','stun_hit':'StunHit','jump':'Jump','death':'Death'}
 skeleton=u.load_asset('/Game/Gameplay/CoreLoop/NutTank/SK_NutTank').get_editor_property('skeleton')
 for field,label in bindings.items():
  clip=u.load_asset('/Game/Gameplay/CoreLoop/NutTank/Animations/KnightUpdate/A_NutKnight_'+label)
  assert isinstance(clip,u.AnimSequence) and clip.get_editor_property('skeleton')==skeleton
  boss.set_editor_property('tank_'+field+'_animation',clip)
 tuning={'tank_melee_impact_fraction':15/38,'tank_melee2_impact_fraction':15/38,'tank_melee_animation_seconds':38/30,
         'tank_combo_animation_seconds':92/30,'tank_combo_contact_fractions':u.Vector(15/92,29/92,49/92),
         'tank_jump_takeoff_fraction':8/97,'tank_jump_impact_fraction':37/97,
         'tank_stun_seconds':3.,'tank_death_seconds':155/30}
 for name,value in tuning.items():boss.set_editor_property(name,value)
 preserved={}
 for name in ['tank_walk_animation','tank_walk_left_animation','tank_walk_right_animation','tank_unshielded_walk_animation','tank_transform_animation','tank_charge_tell_animation','tank_charge_loop_animation','tank_charge_recovery_animation','tank_skeletal_mesh','tank_unshielded_skeletal_mesh']:
  value=boss.get_editor_property(name);path=value.get_path_name() if value else str(value)
  assert path==baseline['bindings'][name],(name,path,baseline['bindings'][name]);preserved[name]=path
 settings.set_editor_property('boss',boss);profile.set_editor_property('settings',settings)
 assert u.EditorAssetLibrary.save_loaded_asset(profile)
 for path,sha in baseline['protected_asset_hashes'].items():assert hashlib.sha256((ROOT/path).read_bytes()).hexdigest()==sha,path
 (OUT/'ConnectionReport.json').write_text(json.dumps({'passed':True,'bindings':bindings,'tuning':{k:str(v) for k,v in tuning.items()},'preserved':preserved},indent=2))
 print('NUT_KNIGHT_CONNECT_PASS',len(bindings))
if __name__=='__main__':main()
