"""Inspect three in-process PIE players on a listen server and both clients.

Run after configuring PlayNumberOfClients=3, PIE_ListenServer, RunUnderOneProcess.
Run twice to also check that the colors stay stable. Writes only a Saved report.
"""
import json
from pathlib import Path
import unreal as u

hero_class = u.load_class(None, '/Script/MessControl.MCToothCharacter')
bag = u.load_asset('/Game/Gameplay/CharacterCurrent/Materials/MI_Bag')
path = Path(u.Paths.project_saved_dir()) / 'BagColorVerification.json'
previous = json.loads(path.read_text()) if path.exists() else None
worlds = u.EditorLevelLibrary.get_pie_worlds(False)
assert len(worlds) == 3, [w.get_path_name() for w in worlds]

def rgba(color):
    return [color.r, color.g, color.b, color.a]

report = {'session': str(worlds[0]), 'worlds': [], 'passed': False}
for world in worlds:
    players = []
    for actor in u.GameplayStatics.get_all_actors_of_class(world, hero_class):
        state = actor.get_editor_property('player_state')
        if not state:
            continue
        mesh = actor.get_component_by_class(u.SkeletalMeshComponent)
        material = mesh.get_material(1)
        assert isinstance(material, u.MaterialInstanceDynamic), str(material)
        assert material.get_editor_property('parent') == bag
        color = rgba(actor.get_editor_property('bag_color'))
        applied = rgba(material.get_vector_parameter_value('Albedo Color'))
        assert all(abs(a-b) < 1e-6 for a,b in zip(color,applied)), (color,applied)
        assert material.get_scalar_parameter_value('Saturate') == 1
        assert min(color[:3]) < .5 and max(color[:3]) >= .99, color
        assert rgba(mesh.get_material(0).get_vector_parameter_value('Albedo Color')) == [1,1,1,1]
        players.append({'id': state.get_editor_property('player_id'), 'color': color, 'material': material.get_path_name()})
    players.sort(key=lambda row: row['id'])
    assert len(players) == 3, players
    assert len({p['material'] for p in players}) == 3
    report['worlds'].append({'world': world.get_path_name(), 'players': players})
expected = [(p['id'],p['color']) for p in report['worlds'][0]['players']]
for world in report['worlds'][1:]:
    assert [(p['id'],p['color']) for p in world['players']] == expected, report
if previous and previous.get('session') == report['session']:
    assert previous['worlds'] == report['worlds'], 'Colors changed during play'
    report['stable_on_repeat'] = True
report['passed'] = True
path.write_text(json.dumps(report,indent=2), encoding='utf8')
u.log('BAG_COLOR_PASS ' + json.dumps(report))
