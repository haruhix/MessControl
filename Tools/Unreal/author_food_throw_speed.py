"""Save the actual player Blueprint's requested faster throw, preserving other edits."""
import unreal as u,json
from pathlib import Path
root=Path(u.Paths.project_dir());out=root/'Artifacts/FoodThrow_20261017'
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
bp=u.load_asset('/Game/Blueprints/BP_PlayerCharacter');cdo=u.get_default_object(bp.generated_class())
component=cdo.get_editor_property('food_collection');keys=['throw_interval','max_pieces','collection_reach','spill_contact_impulse']
before={k:component.get_editor_property(k) for k in keys}
with u.ScopedEditorTransaction('Faster arcing food throw'):
 bp.modify();cdo.modify();component.modify();component.set_editor_property('throw_speed',1800.)
 assert u.BlueprintEditorLibrary.compile_blueprint(bp)
 actual=u.get_default_object(bp.generated_class()).get_editor_property('food_collection')
 assert abs(actual.get_editor_property('throw_speed')-1800.)<.001
 assert before=={k:actual.get_editor_property(k) for k in keys}
 assert u.EditorAssetLibrary.save_loaded_asset(bp,False)
(out/'Blueprint.json').write_text(json.dumps(dict(passed=True,throw_speed=actual.get_editor_property('throw_speed'),preserved=before),indent=2));print('FOOD_THROW_SPEED_SAVED',1800)
