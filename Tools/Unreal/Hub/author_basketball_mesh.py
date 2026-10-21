"""Use the centered basketball art with the native ball's 100 cm visual basis.

Run in the open Hub editor. The native BeginPlay scales a 50 cm-radius mesh
to the physics radius, so bake normalization into a dedicated mesh asset.
"""
import json
import shutil
from pathlib import Path
import unreal as u

SOURCE = '/Game/FromBlender11/SM_stylized_basketball_game_ready_001'
MATERIAL = '/Game/Fab/Stylized_Basketball_-_Game_Ready/stylized_basketball_game_ready/Materials/Basketball'
TARGET = '/Game/Hub/Meshes/SM_HubBasketball'
TAG = 'HubBasketballMeshSource'

def xyz(v):
    return [v.x, v.y, v.z]

def run():
    assert not u.EditorLevelLibrary.get_pie_worlds(False), 'Stop PIE before changing the ball'
    world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
    assert world.get_path_name() == '/Game/Maps/Hub.Hub'
    actors = u.get_editor_subsystem(u.EditorActorSubsystem)
    balls = [a for a in actors.get_all_level_actors() if isinstance(a, u.MCHubBasketball)]
    assert len(balls) == 1, 'Expected the existing Hub basketball'
    ball = balls[0]
    source = u.load_asset(SOURCE)
    material = u.load_asset(MATERIAL)
    assert source and material
    box = source.get_bounding_box()
    assert (box.min + box.max).length() < .01, 'Source mesh must be centered'
    root = Path(u.Paths.project_dir())
    out = root / 'Saved/HubBasketballMesh_20261021'
    out.mkdir(parents=True, exist_ok=True)
    backup = out / 'Before/Content/Maps/Hub.umap'
    backup.parent.mkdir(parents=True, exist_ok=True)
    if not backup.exists():
        shutil.copy2(root / 'Content/Maps/Hub.umap', backup)
    report = {
        'source': SOURCE, 'material': MATERIAL, 'target': TARGET,
        'ball': ball.get_path_name(), 'radius': ball.radius,
        'location': xyz(ball.get_actor_location()),
        'previous_mesh': ball.visual.static_mesh.get_path_name(),
        'hub_had_unsaved_edits': any(p.get_name() == '/Game/Maps/Hub' for p in u.EditorLoadingAndSavingUtils.get_dirty_map_packages()),
    }
    mesh = u.load_asset(TARGET) if u.EditorAssetLibrary.does_asset_exist(TARGET) else None
    if mesh:
        assert u.EditorAssetLibrary.get_metadata_tag(mesh, TAG) == SOURCE, 'Preserve unrelated existing asset'
    else:
        mesh = u.EditorAssetLibrary.duplicate_asset(SOURCE, TARGET)
        assert mesh
        u.EditorAssetLibrary.set_metadata_tag(mesh, TAG, SOURCE)
    dynamic = u.new_object(u.DynamicMesh)
    dynamic, outcome = u.GeometryScript_AssetUtils.copy_mesh_from_static_mesh(
        source, dynamic, u.GeometryScriptCopyMeshFromAssetOptions(), u.GeometryScriptMeshReadLOD())
    assert outcome == u.GeometryScriptOutcomePins.SUCCESS
    extent = (box.max - box.min) * .5
    factor = 50. / max(extent.x, extent.y, extent.z)
    u.GeometryScript_MeshTransforms.scale_mesh(dynamic, u.Vector(factor, factor, factor))
    options = u.GeometryScriptCopyMeshToAssetOptions()
    options.set_editor_property('use_build_scale', False)
    options.set_editor_property('replace_materials', True)
    options.set_editor_property('new_materials', [material])
    options.set_editor_property('new_material_slot_names', ['Basketball'])
    options.set_editor_property('generate_lightmap_u_vs', u.GeometryScriptGenerateLightmapUVOptions.DO_NOT_GENERATE_LIGHTMAP_U_VS)
    _, outcome = u.GeometryScript_AssetUtils.copy_mesh_to_static_mesh(
        dynamic, mesh, options, u.GeometryScriptMeshWriteLOD(), False)
    assert outcome == u.GeometryScriptOutcomePins.SUCCESS
    assert u.EditorAssetLibrary.save_loaded_asset(mesh, only_if_is_dirty=False)
    bounds = mesh.get_bounding_box()
    report['mesh_min'] = xyz(bounds.min)
    report['mesh_max'] = xyz(bounds.max)
    extent = (bounds.max - bounds.min) * .5
    assert abs(max(extent.x, extent.y, extent.z) - 50.) < .01, report
    with u.ScopedEditorTransaction('Replace Hub ball with basketball mesh'):
        ball.modify()
        ball.visual.modify()
        assert ball.visual.set_static_mesh(mesh)
        ball.visual.set_editor_property('override_materials', [])
        ball.visual.set_relative_location(u.Vector(), False, False)
        ball.visual.set_relative_scale3d(u.Vector(ball.radius / 50., ball.radius / 50., ball.radius / 50.))
    assert ball.visual.get_material(0) == material
    assert u.EditorLoadingAndSavingUtils.save_map(world, '/Game/Maps/Hub')
    report['saved'] = True
    (out / 'authored.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print('BASKETBALL_MESH_AUTHORED', json.dumps(report))
    return report

if __name__ == '__main__':
    run()
