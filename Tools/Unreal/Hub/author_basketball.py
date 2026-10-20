"""Replace the Hub's decorative Sphere with the native simulated basketball.

Run in the editor after compiling MCHubBasketball. Backs up changed packages;
saves only Hub and SM_Basket. Existing scenery and designer edits are retained.
"""
import unreal as u
import json
import shutil
from pathlib import Path

def run():
    assert not u.EditorLevelLibrary.get_pie_worlds(False), 'Stop PIE before authoring'
    world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
    assert world.get_path_name()=='/Game/Maps/Hub.Hub'
    root=Path(u.Paths.project_dir());out=root/'Saved/HubBasketball_20261021'
    backup=out/'Before/SavedDesignerEdits'
    for rel in ['Content/Maps/Hub.umap','Content/FromBlender11/SM_Basket.uasset']:
        target=backup/rel;target.parent.mkdir(parents=True,exist_ok=True)
        if not target.exists():shutil.copy2(root/rel,target)
    actors=u.get_editor_subsystem(u.EditorActorSubsystem)
    placed=actors.get_all_level_actors()
    balls=[a for a in placed if isinstance(a,u.MCHubBasketball)]
    assert len(balls)<=1,'Duplicate basketballs'
    original=next((a for a in placed if a.get_actor_label()=='Sphere' and isinstance(a,u.StaticMeshActor)),None)
    if not balls:
        assert original is not None,'Expected the existing Hub Sphere'
        transform=original.get_actor_transform()
        source=original.static_mesh_component
        ball=actors.spawn_actor_from_class(u.MCHubBasketball,original.get_actor_location(),original.get_actor_rotation())
        assert ball
        ball.set_actor_label('Hub_Basketball')
        ball.visual.set_static_mesh(source.static_mesh)
        for i in range(source.get_num_materials()):ball.visual.set_material(i,source.get_material(i))
        # Preserve the authored sphere's visual diameter (native sphere mesh is 100 cm).
        ball.set_editor_property('radius',50.*abs(original.get_actor_scale3d().x))
        assert actors.destroy_actor(original)
    else:ball=balls[0]
    ball.set_editor_property('throw_speed',2100.)
    court=next(a for a in placed if isinstance(a,u.StaticMeshActor) and a.static_mesh_component.static_mesh
               and a.static_mesh_component.static_mesh.get_path_name()=='/Game/FromBlender11/SM_Basket.SM_Basket')
    mesh=court.static_mesh_component.static_mesh
    ball.set_editor_property('hoop',court)
    # Measured center of this SM_Basket's torus, stored in the hoop's local space
    # so ordinary editor moves/rotations of the court preserve aiming.
    ball.set_editor_property('local_rim_center',u.Vector(15.323569,-606.211697,122.417106))
    setup=mesh.get_editor_property('body_setup')
    setup.set_editor_property('collision_trace_flag',u.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
    setup.set_editor_property('double_sided_geometry',True)
    assert u.EditorAssetLibrary.save_loaded_asset(mesh,only_if_is_dirty=False)
    assert u.EditorLoadingAndSavingUtils.save_map(world,'/Game/Maps/Hub')
    report={'ball':ball.get_path_name(),'location':str(ball.get_actor_location()),'radius':ball.radius,
            'court_collision':str(setup.get_editor_property('collision_trace_flag')),
            'preserved_materials':[str(ball.visual.get_material(i)) for i in range(ball.visual.get_num_materials())]}
    (out/'authored.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    print('HUB_BASKETBALL_AUTHORED',json.dumps(report))
    return report

if __name__=='__main__':run()
