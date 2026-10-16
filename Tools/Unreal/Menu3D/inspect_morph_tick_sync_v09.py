"""Read the actual menu mesh tick/evaluation policy; no authoring."""
import json
from pathlib import Path
import unreal as u

world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
rows=[]
for day in (1,2,3):
    actor=u.GameplayStatics.get_all_actors_with_tag(world,f'Menu3D_DAY{day:02d}')[0]
    mesh=actor.get_component_by_class(u.SkeletalMeshComponent)
    values={}
    for name in ('animation_mode','anim_class','visibility_based_anim_tick_option',
                 'enable_update_rate_optimizations','primary_component_tick','pause_anims',
                 'no_skeleton_update','b_update_animation_in_editor'):
        try:values[name]=str(mesh.get_editor_property(name))
        except Exception as error:values[name]='UNAVAILABLE: '+str(error)
    rows.append({'day':day,'mesh':mesh.get_path_name(),'properties':values})
out=Path(u.Paths.project_dir())/'Tools/Unreal/Menu3D/Evidence/v09_morph_tick_policy_before.json'
assert not out.exists()
out.write_text(json.dumps(rows,indent=2),encoding='utf8')
u.log('MC_MORPH_TICK_POLICY '+json.dumps(rows))
