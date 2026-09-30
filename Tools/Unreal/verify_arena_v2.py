"""Validate saved arena connections in the editor, with PIE stopped."""
import json
from pathlib import Path
import unreal as u

actors=u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors()
assert not u.EditorLevelLibrary.get_pie_worlds(False), 'Stop PIE first'
markers=sorted([a for a in actors if isinstance(a,u.MCArenaToothSocket)],key=lambda a:a.tooth_id)
assert [a.tooth_id for a in markers]==list(range(1,9))
care=u.EditorAssetLibrary.load_asset('/Game/Gameplay/Arena/V2/M_ArenaToothCareV2')
assert u.EditorAssetLibrary.load_asset('/Game/Data/DA_ArenaTooth').gameplay_material==care
for a in markers:
    assert a.preview.static_mesh.get_path_name().startswith('/Game/Gameplay/Arena/V2/')
    assert a.preview.get_material(0)==care
    assert a.preview.static_mesh.get_editor_property('body_setup').get_editor_property('collision_trace_flag')==u.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE
bounds=[a for a in actors if 'MCCameraBounds' in [str(t) for t in a.tags]]
assert len(bounds)==1
box=bounds[0].get_component_by_class(u.BoxComponent)
assert box.get_collision_enabled()==u.CollisionEnabled.NO_COLLISION
throats=[a for a in actors if isinstance(a,u.MCThroat)]
assert len(throats)==1
t=throats[0]
assert t.uvula.static_mesh.get_path_name()=='/Game/Gameplay/Throat/SM_Uvula.SM_Uvula'
assert t.uvula.get_material(0).get_path_name()=='/Game/Art/Materials/MI_MouthPalate.MI_MouthPalate'
assert t.sculpted_tissue.skeletal_mesh_asset.get_path_name()=='/Game/Gameplay/Throat/SK_Throat.SK_Throat'
assert t.sculpted_tissue.get_material(0)==t.tissue_material
assert t.tissue_material.get_path_name()=='/Game/Art/Materials/MI_LivingThroat.MI_LivingThroat'
for a in actors:
    if isinstance(a,u.SkeletalMeshActor) and a.skeletal_mesh_component.skeletal_mesh_asset:
        if a.skeletal_mesh_component.skeletal_mesh_asset.get_path_name()=='/Game/FromBlender2/SK_Exit.SK_Exit':
            assert not a.skeletal_mesh_component.is_visible()
            assert a.skeletal_mesh_component.get_collision_enabled()==u.CollisionEnabled.NO_COLLISION
out=dict(gameplayTeeth=len(markers),cameraBounds=bounds[0].get_actor_label(),uvula=t.uvula.static_mesh.get_path_name(),uvulaMaterial=t.uvula.get_material(0).get_path_name(),throat=t.get_actor_label(),result='PASS')
Path(u.Paths.project_dir(),'Artifacts/ArenaV2/SavedMap.json').write_text(json.dumps(out,indent=2),encoding='utf-8')
u.log('MC_ARENA_V2_MAP_PASS')
