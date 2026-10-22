"""Make the owned menu coffee respond to the exported bristle trajectory.

The native material track drives the reveal in sequence time, including paused
review frames. This is a menu presentation; gameplay cleaning is untouched.
"""
import json
from pathlib import Path
import unreal as u

ROOT = Path('E:/DEVGAME/MessControl')
BASE = '/Game/UI/MenuShowcase/AnatomyV2'
es = u.get_editor_subsystem(u.EditorActorSubsystem)
ls = u.get_editor_subsystem(u.LevelEditorSubsystem)
assert not ls.is_in_play_in_editor()
assert u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world().get_path_name().startswith(BASE + '/L_MenuAnatomyV2.')
edit, lib = u.MaterialEditingLibrary, u.EditorAssetLibrary
material = lib.load_asset(BASE + '/Materials/M_MenuCoffeeEnamel')
nodes = list(edit.get_material_expressions(material))
params = {str(n.get_editor_property('parameter_name')): n for n in nodes if isinstance(n, u.MaterialExpressionVectorParameter)}
vectors = {name: params['Coffee ' + name].get_editor_property('default_value') for name in ['C', 'U', 'V']}
C, U, V = [u.Vector(vectors[n].r, vectors[n].g, vectors[n].b) for n in ['C', 'U', 'V']]
actors = es.get_all_level_actors()
hero = next(a for a in actors if 'MC_MenuV2_Hero' in [str(t) for t in a.tags])
tooth = next(a for a in actors if '| ARENA | Tooth 04 |' in a.get_actor_label())
samples = json.loads((ROOT / 'ArtSource/UI/MenuSceneV2_20261022/Blender/Exports/A_Menu_CleanTooth_Loop_V2_BrushWorld.json').read_text())['samples']
points = []
for sample in samples[:193:12]:
    loc, q = sample['location_m'], sample['quaternion_wxyz']
    ht = hero.get_actor_transform()
    pose = u.Transform()
    pose.translation = ht.transform_location(u.Vector(loc[0] * 100, -loc[1] * 100, loc[2] * 100))
    pose.rotation = ht.rotation * u.Quat(-q[1], q[2], -q[3], q[0])
    pose.scale3d = ht.scale3d * (sample['scale'][0] / .01)
    d = pose.transform_location(u.Vector(80, 0, -14)) - C
    points.append((u.MathLibrary.dot_vector_vector(d, U), u.MathLibrary.dot_vector_vector(d, V)))
mask = next(n for n in nodes if isinstance(n, u.MaterialExpressionCustom) and str(n.get_editor_property('description')) == 'One connected feathered coffee film at bristle contact')
progress = next((n for n in nodes if isinstance(n, u.MaterialExpressionScalarParameter) and str(n.get_editor_property('parameter_name')) == 'Menu Clean Progress'), None)
if not progress:
    progress = edit.create_material_expression(material, u.MaterialExpressionScalarParameter)
    progress.set_editor_property('parameter_name', 'Menu Clean Progress')
progress.set_editor_property('default_value', 0.)
inputs = list(mask.get_editor_property('inputs'))
if 'Progress' not in [str(i.get_editor_property('input_name')) for i in inputs]:
    entry = u.CustomInput()
    entry.set_editor_property('input_name', 'Progress')
    inputs.append(entry)
    mask.set_editor_property('inputs', inputs)
assert edit.connect_material_expressions(progress, '', mask, 'Progress')
array = ','.join('float2(%.6f,%.6f)' % xy for xy in points)
base_code = str(mask.get_editor_property('code')).split('// MENU_NATIVE_CLEAN_TRAIL')[0]
base_code = base_code.replace('return saturate(boundary*slab*facing);', '')
trail = '''
// MENU_NATIVE_CLEAN_TRAIL
float2 path[COUNT] = {POINTS};
float2 here=float2(dot(d,U),dot(d,V));
float t=saturate(Progress)*(COUNT-1);
float cleared=0.0;
[unroll] for(int i=0;i<COUNT-1;i++) {
    float phase=saturate(t-i);
    float2 a0=path[i],b0=lerp(a0,path[i+1],phase);
    float2 ab=b0-a0;
    float along=saturate(dot(here-a0,ab)/max(dot(ab,ab),.0001));
    float distanceToStroke=length(here-(a0+along*ab));
    cleared=max(cleared,(1.0-smoothstep(4.5,7.0,distanceToStroke))*step(.0001,phase));
}
return saturate(boundary*slab*facing)*(1.0-cleared*.94);
'''.replace('COUNT', str(len(points))).replace('POINTS', array)
mask.set_editor_property('code', base_code + trail)
# Restore richer thin-film colour while retaining the filtered density texture.
for n in nodes:
    if not isinstance(n, u.MaterialExpressionCustom):
        continue
    label = str(n.get_editor_property('description'))
    if label in ['Espresso concentrate and translucent warm edges', 'Thin wet coffee film retains enamel underneath']:
        code = str(n.get_editor_property('code')).replace('(.16+.82*smoothstep(.07,.87,Density))', '(.42+.55*smoothstep(.07,.87,Density))')
        n.set_editor_property('code', code)
assert not list(edit.recompile_material(material))
assert lib.save_loaded_asset(material)
seq = lib.load_asset(BASE + '/LS_MenuCleaningV2')
for b in list(seq.get_bindings()):
    if b.get_name() in ['MenuCoffeeSurfaceV2', 'MenuCoffeeMaterialV2']:
        b.remove()
actor_binding = seq.add_possessable(tooth)
actor_binding.set_name('MenuCoffeeSurfaceV2')
component_binding = seq.add_possessable(tooth.static_mesh_component)
component_binding.set_name('MenuCoffeeMaterialV2')
component_binding.set_parent(actor_binding)
track = component_binding.add_track(u.MovieSceneComponentMaterialTrack)
info = u.ComponentMaterialInfo()
info.material_slot_index = 0
info.material_type = u.ComponentMaterialType.INDEXED_MATERIAL
track.set_material_info(info)
section = track.add_section()
section.set_range(0, 288)
parameter = u.MaterialParameterInfo()
parameter.name = 'Menu Clean Progress'
rate = seq.get_tick_resolution()
ratio = rate.numerator / rate.denominator / 24.
for frame, value in [(0, 0.), (192, 1.), (264, 1.), (288, 0.)]:
    section.add_scalar_parameter_key(parameter, u.FrameNumber(round(frame * ratio)), value, '', '', u.MovieSceneKeyInterpolation.LINEAR)
assert lib.save_loaded_asset(seq)
assert ls.save_current_level()
(ROOT / 'Artifacts/MenuSceneV2_20261022/Environment/NativeCoffeeCleanTrail.json').write_text(json.dumps({'points_cm': points, 'reveal_frames': [0, 192], 'hold_until': 264, 'loop_reset_frames': [264, 288], 'status': 'compiled_pending_native_visual_review'}, indent=2))
u.log('MENU_NATIVE_COFFEE_REVEAL_AUTHORED')
