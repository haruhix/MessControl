"""Fit existing DAY03 cavity/tongue widths to the new arch; preserve source assets."""
import json
from pathlib import Path
import unreal as u

levels=u.get_editor_subsystem(u.LevelEditorSubsystem);assert not levels.is_in_play_in_editor()
world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
assert world.get_path_name().startswith('/Game/UI/MainMenu3D/L_MainMenu3D')
changes=[]
for suffix in ('Interior','Tongue'):
    actors=u.GameplayStatics.get_all_actors_with_tag(world,'Menu3D_DAY03_'+suffix);assert len(actors)==1
    actor=actors[0];component=actor.get_editor_property('root_component')
    old=component.get_editor_property('relative_scale3d')
    old_values=[old.x,old.y,old.z]
    assert any(abs(old.x-v)<1e-6 for v in (1.,.94,.90))
    component.set_editor_property('relative_scale3d',u.Vector(.90,old.y,old.z))
    changes.append({'actor':actor.get_path_name(),'old':old_values,'new':str(component.get_editor_property('relative_scale3d'))})
assert levels.save_current_level()
report={'status':'SAVED_PENDING_NATIVE_REVIEW','relative_x':.90,'changes':changes,
    'crown_meshes_and_character_unchanged':True,
    'widget_changes':'DAY03 heading/species/Play/Back shifted up18designpx; two DAY03 interior/tongue mask denominators2→2.66666666667 (75% card height).',
    'intent':'Keep old cheek/tongue volumes inside the remodeled gum silhouette and innerBack clear of lower enamel.'}
(Path(u.Paths.project_dir())/'Tools/Unreal/Menu3D/Evidence/v07_cavity_controls_fit_r02.json').write_text(json.dumps(report,indent=2),encoding='utf8')
u.log('MC_V07_CAVITY_FIT '+json.dumps(report))
