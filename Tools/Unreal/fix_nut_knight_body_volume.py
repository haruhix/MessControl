"""Use dual-quaternion skinning for the knight's Blender Preserve Volume weights.

Run in an idle editor after enabling the engine DeformerGraph plugin. Animation
tracks and weights remain unchanged; both shield variants use the same deformer.
"""
from pathlib import Path
import unreal as u,json,hashlib

ROOT=Path(u.Paths.project_dir());OUT=ROOT/'Artifacts/NutKnightBodyFix_20261017'
GRAPH='/DeformerGraph/Deformers/DG_DualQuatSkin_Morph_Cloth'

def main():
    assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
    assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages()
    assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages()
    baseline=json.loads((OUT/'MeshBaseline.json').read_text())
    graph=u.load_asset(GRAPH)
    assert graph and graph.get_class().get_name()=='OptimusDeformer',str(graph)
    result={'passed':False,'deformer':graph.get_path_name(),'meshes':[]}
    for name,old in baseline.items():
        mesh=u.load_asset('/Game/Gameplay/CoreLoop/NutTank/'+name)
        assert mesh.get_editor_property('skeleton').get_path_name()==old['skeleton']
        assert [s.export_text() for s in mesh.get_editor_property('materials')]==old['materials']
        assert mesh.get_default_mesh_deformer() is None
        mesh.set_editor_property('default_mesh_deformer',graph)
        assert mesh.get_default_mesh_deformer()==graph
        assert u.EditorAssetLibrary.save_loaded_asset(mesh)
        assert [s.export_text() for s in mesh.get_editor_property('materials')]==old['materials']
        result['meshes'].append(mesh.get_path_name())
    protected=json.loads((OUT/'Baseline.json').read_text())
    for rel,sha in protected['protected_hashes'].items():assert hashlib.sha256((ROOT/rel).read_bytes()).hexdigest()==sha,rel
    result.update(passed=True,weights_geometry_animations_materials_skeleton_unchanged=True)
    (OUT/'IntegrationReport.json').write_text(json.dumps(result,indent=2));print('KNIGHT_BODY_VOLUME_INTEGRATED',result)

if __name__=='__main__':main()
