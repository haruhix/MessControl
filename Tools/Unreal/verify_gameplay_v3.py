"""Audit the saved playable map after the final editor restart, without UI input."""
import json
from pathlib import Path
import unreal as u
lib=u.EditorAssetLibrary
actors=u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors()
throats=[a for a in actors if isinstance(a,u.MCThroat)]
assert len(throats)==1
t=throats[0]
assert t.get_editor_property('uvula').static_mesh.get_name()=='SM_Uvula'
mouth=t.get_editor_property('authored_mouth')
assert mouth.skeletal_mesh_asset.get_name()=='SK_Exit'
assert t.get_editor_property('reverse_authored_open')
t.call_method('RebuildAppearance')
assert mouth.get_morph_target('Open')>.95
table=lib.load_asset('/Game/Data/DT_BreakfastMenu')
row=next(r for r in json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(table)) if r['Name']=='SpicyPepper')
assert row['Kind']=='Spicy' and row['FuseSeconds']==8 and row['FirstPulseRadius']==180
assignments=[]
for a in actors:
    if isinstance(a,u.StaticMeshActor) and a.static_mesh_component.static_mesh and a.static_mesh_component.static_mesh.get_name() in ('SM_Gum','SM_Roof_Wall'):
        path=a.static_mesh_component.get_material(0).get_path_name()
        assert path.startswith('/Game/Gameplay/MouthV3/')
        assignments.append(dict(actor=a.get_actor_label(),material=path))
assert len(assignments)>=2
assert any('MCCameraBounds' in [str(tag) for tag in a.tags] for a in actors)
assert len([a for a in actors if isinstance(a,u.MCArenaToothSocket) and 'ArtistArenaIntegrationV2' in [str(tag) for tag in a.tags]])==8
assert u.get_editor_subsystem(u.LevelEditorSubsystem).save_current_level()
report=dict(uvula=t.get_editor_property('uvula').static_mesh.get_path_name(),mouth=mouth.skeletal_mesh_asset.get_path_name(),open_at_rest=mouth.get_morph_target('Open'),materials=assignments,pepper=row)
Path(u.Paths.project_saved_dir(),'GameplayV3Audit.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
u.log('MC_GAMEPLAY_V3_AUDIT_PASS')
