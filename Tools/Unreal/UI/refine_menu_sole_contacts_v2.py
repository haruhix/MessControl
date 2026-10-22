"""Two stable low-sole contact decals for the existing rounded tooth roots."""
import unreal as u
import json
from pathlib import Path
BASE = '/Game/UI/MenuShowcase/AnatomyV2'
es = u.get_editor_subsystem(u.EditorActorSubsystem)
ls = u.get_editor_subsystem(u.LevelEditorSubsystem)
assert not ls.is_in_play_in_editor()
assert u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world().get_path_name().startswith(BASE + '/L_MenuAnatomyV2.')
actors = es.get_all_level_actors()
m = u.load_asset(BASE + '/Materials/M_MenuFootContact')
node = next(n for n in u.MaterialEditingLibrary.get_material_expressions(m) if isinstance(n, u.MaterialExpressionCustom))
node.set_editor_property('code', 'float2 p=(UV-.5)*2.0; float r=length(p); return .64*exp(-3.8*dot(p,p))*(1.0-smoothstep(.70,1.0,r));')
assert not list(u.MaterialEditingLibrary.recompile_material(m))
assert u.EditorAssetLibrary.save_loaded_asset(m)
for a in actors:
    if a.get_actor_label() == 'MENU V2 | Soft sole contact':
        a.set_actor_hidden_in_game(True)
        a.set_is_temporarily_hidden_in_editor(True)
hero = next(a for a in actors if 'MC_MenuV2_Hero' in [str(t) for t in a.tags])
report = json.loads(Path('E:/DEVGAME/MessControl/ArtSource/UI/MenuSceneV2_20261022/Blender/Exports/A_Menu_CleanTooth_Loop_V2_BakeReport.json').read_text())
contacts = []
for side, samples in report['footprints']['sole_samples_world_cm'].items():
    low = min(samples[0], key=lambda p: p[2])
    point = hero.get_actor_transform().transform_location(u.Vector(low[0], -low[1], low[2]))
    contacts.append((side.upper(), point.x, point.y))
assert len(contacts) == 2
for side, x, y in contacts:
    label = 'MENU V2 | Sole contact ' + side
    a = next((a for a in actors if a.get_actor_label() == label), None)
    if not a:
        a = es.spawn_actor_from_class(u.DecalActor, u.Vector(x, y, 20))
        a.set_actor_label(label)
        a.tags = ['MC_MenuV2_Owned']
    a.set_actor_location(u.Vector(x, y, 20), False, True)
    a.set_actor_rotation(u.Rotator(pitch=-90), False)
    c = a.get_component_by_class(u.DecalComponent)
    c.set_decal_material(m)
    c.set_editor_property('decal_size', u.Vector(25, 12, 13))
    c.set_editor_property('fade_screen_size', .0001)
assert ls.save_current_level()
u.log('MENU_TWO_SOLE_CONTACTS_SAVED')
