"""Refresh the contact-plane parameters after a rigid menu layout change."""
import json
from pathlib import Path
import unreal as u
ROOT = Path('E:/DEVGAME/MessControl')
BASE = '/Game/UI/MenuShowcase/AnatomyV2'
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
hero = next(a for a in u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors() if 'MC_MenuV2_Hero' in [str(t) for t in a.tags])
sample = json.loads((ROOT / 'ArtSource/UI/MenuSceneV2_20261022/Blender/Exports/A_Menu_CleanTooth_Loop_V2_BrushWorld.json').read_text())['samples'][20]
loc, q = sample['location_m'], sample['quaternion_wxyz']
ht = hero.get_actor_transform()
pose = u.Transform()
pose.translation = ht.transform_location(u.Vector(loc[0] * 100, -loc[1] * 100, loc[2] * 100))
pose.rotation = ht.rotation * u.Quat(-q[1], q[2], -q[3], q[0])
pose.scale3d = ht.scale3d * (sample['scale'][0] / .01)
rotation = pose.rotation.rotator()
values = {'Coffee C': pose.transform_location(u.Vector(80, 0, -14)),
          'Coffee U': u.MathLibrary.get_right_vector(rotation),
          'Coffee V': u.MathLibrary.get_forward_vector(rotation),
          'Coffee W': u.MathLibrary.get_up_vector(rotation)}
material = u.load_asset(BASE + '/Materials/M_MenuCoffeeEnamel')
found = set()
for node in u.MaterialEditingLibrary.get_material_expressions(material):
    if isinstance(node, u.MaterialExpressionVectorParameter):
        name = str(node.get_editor_property('parameter_name'))
        if name in values:
            v = values[name]
            node.set_editor_property('default_value', u.LinearColor(v.x, v.y, v.z, 1))
            found.add(name)
assert found == set(values)
assert not list(u.MaterialEditingLibrary.recompile_material(material))
assert u.EditorAssetLibrary.save_loaded_asset(material)
u.log('MENU_COFFEE_PROJECTION_SYNCED')
