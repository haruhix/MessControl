"""One-time correction: convert the existing lower molars into eight authored gameplay sockets.
Keeps the original mesh, size and selected placements. Does not rebuild the user's map.
"""
import unreal as u

levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
actors = u.get_editor_subsystem(u.EditorActorSubsystem)
if not levels.load_level('/Game/Maps/L_Mouth'):
    raise RuntimeError('Could not load L_Mouth')
all_actors = actors.get_all_level_actors()
sockets = [a for a in all_actors if isinstance(a,u.MCArenaToothSocket)]
legacy = [a for a in all_actors if isinstance(a,u.StaticMeshActor) and a.get_actor_label().startswith('ART | Lower molar ')]
if sockets:
    if len(sockets) != 8 or legacy:
        raise RuntimeError('Mixed/edited arena layout; inspect manually instead of overwriting it')
    u.log('MC_ARENA_LAYOUT_EXISTS: preserving eight authored teeth')
else:
    rows = [sorted([a for a in legacy if a.get_actor_location().y*side>0], key=lambda a:a.get_actor_location().x) for side in (-1,1)]
    if any(len(row)!=8 for row in rows):
        raise RuntimeError('Expected the original two lower rows of eight teeth; map was not changed')
    # Keep four existing positions per side, including the first and last molars.
    for index, original_index in enumerate((0,2,5,7)):
        for side_index, row in enumerate(rows):
            original = row[original_index]
            slot = actors.spawn_actor_from_class(u.MCArenaToothSocket, original.get_actor_location(), original.get_actor_rotation())
            slot.set_actor_scale3d(original.get_actor_scale3d())
            slot.preview.set_static_mesh(original.static_mesh_component.static_mesh)
            number = index*2+side_index+1
            slot.set_editor_property('tooth_id',number)
            slot.set_actor_label('ARENA | Tooth %02d'%number)
    # The old side walls ran through the decorative molars and blocked access to them.
    # Move walls outside the tooth rows; extend support beneath the gums for fallen teeth.
    for actor in all_actors:
        label = actor.get_actor_label()
        if label in ('COLLISION | Left boundary','COLLISION | Right boundary'):
            p = actor.get_actor_location()
            actor.set_actor_location(u.Vector(p.x,-1020 if p.y<0 else 1020,p.z),False,False)
        elif label == 'COLLISION | Tongue floor':
            scale = actor.get_actor_scale3d()
            actor.set_actor_scale3d(u.Vector(scale.x,20,scale.z))
        elif label in ('COLLISION | Front boundary','COLLISION | Back boundary'):
            scale = actor.get_actor_scale3d()
            actor.set_actor_scale3d(u.Vector(scale.x,21,scale.z))
    for original in legacy:
        actors.destroy_actor(original)
    if not levels.save_current_level():
        raise RuntimeError('Could not save corrected tooth layout')
    u.log('MC_ARENA_LAYOUT_PASS: eight large molars, four per side; no inner row')
