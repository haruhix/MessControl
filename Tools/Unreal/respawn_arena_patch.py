"""Root-invoked, unsaved static arena patch for the replacement sandbox.

Importing performs no Unreal calls. This module never saves a package, edits a
mesh/material asset, spawns a gameplay actor, or changes the active level.
``build_patch(stage_world, actor_subsystem, tag)`` reuses uniquely tagged owned
proxy actors, preserving their Sequencer bindings. Old blocking Cube/Sphere/
EmptySocket actors are deliberately left for the owning author to remove.
"""
import math


def build_patch(stage_world, actor_subsystem, tag):
    """Return live actors/components, mapping callables, and JSON-safe report.

    Source socket 5 and same-row neighbours 3/7 retain exact mesh references,
    materials and nonuniform scales. One world-space rigid yaw/translation maps
    source->native tongue actor centre to lab -Y and socket XY to (75, 170).
    All source Z values are retained; the lab is not flattened or rescaled.
    """
    import unreal as u

    editor = u.get_editor_subsystem(u.UnrealEditorSubsystem)
    levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
    assert not levels.is_in_play_in_editor(), 'Author only in idle Editor'
    assert stage_world and editor.get_editor_world() == stage_world
    stage_package = stage_world.get_path_name().split('.')[0]
    assert stage_package.startswith('/Game/Tests/') and 'RespawnReplacement' in stage_package, stage_package
    assert str(tag), 'An explicit ownership tag is required'
    ownership = str(tag)
    selected = list(actor_subsystem.get_selected_level_actors())
    before_dirty = {p.get_path_name() for p in
                    list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages()) +
                    list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())}

    source_world = u.EditorAssetLibrary.load_asset('/Game/Maps/L_Mouth')
    assert source_world and source_world != stage_world
    source_package = source_world.get_path_name().split('.')[0]
    assert source_package == '/Game/Maps/L_Mouth'

    def members(cls):
        return list(u.GameplayStatics.get_all_actors_of_class(source_world, cls))

    socket_class = u.load_class(None, '/Script/MessControl.MCArenaToothSocket')
    tongue_class = u.load_class(None, '/Script/MessControl.MCTongue')
    assert socket_class and tongue_class
    sockets = {}
    for a in members(socket_class):
        i = int(a.get_editor_property('tooth_id'))
        assert i not in sockets, 'Duplicate source ToothId {}'.format(i)
        sockets[i] = a
    assert all(i in sockets for i in (3, 5, 7)), 'Need source sockets 3, 5, 7'
    tongues = members(tongue_class)
    assert len(tongues) == 1, 'Expected one native tongue in saved L_Mouth'
    tongue = tongues[0]
    tongue_surface = tongue.get_editor_property('surface')
    tongue_mesh = tongue.get_editor_property('source_mesh')
    assert tongue_surface and tongue_mesh

    gums = []
    for a in members(u.StaticMeshActor):
        c = a.static_mesh_component
        m = c.static_mesh
        if m and m.get_name() == 'SM_Gum_Bot_Contact':
            gums.append(a)
    assert len(gums) == 1, 'Expected current lower contact gum, not legacy gum'
    gum = gums[0]

    reserve_source = sockets[5].get_editor_property('preview')
    anchor = reserve_source.get_world_transform().translation
    centre = tongue.get_actor_location()  # Same centre used by GameMode respawn.
    dx, dy = centre.x - anchor.x, centre.y - anchor.y
    assert math.hypot(dx, dy) > 1.0, 'Undefined source inward direction'
    yaw = -90.0 - math.degrees(math.atan2(dy, dx))
    angle = math.radians(yaw)
    ca, sa = math.cos(angle), math.sin(angle)
    qz, qw = math.sin(angle / 2), math.cos(angle / 2)

    def xyz(v):
        return [float(v.x), float(v.y), float(v.z)]

    def map_point(p):
        x, y = p.x - anchor.x, p.y - anchor.y
        return u.Vector(75 + ca*x - sa*y, 170 + sa*x + ca*y, p.z)

    def source_point(p):
        x, y = p.x - 75, p.y - 170
        return u.Vector(anchor.x + ca*x + sa*y, anchor.y - sa*x + ca*y, p.z)

    def map_direction(v):
        return u.Vector(ca*v.x - sa*v.y, sa*v.x + ca*v.y, v.z)

    def map_transform(t):
        q = t.rotation
        result = u.Transform()
        result.translation = map_point(t.translation)
        # Explicit world-space qYaw * qSource; retain arbitrary source tilt.
        result.rotation = u.Quat(qw*q.x - qz*q.y, qw*q.y + qz*q.x,
                                 qw*q.z + qz*q.w, qw*q.w - qz*q.z)
        result.scale3d = t.scale3d
        return result

    def transform_record(t):
        q = t.rotation
        return {'translation': xyz(t.translation),
                'quaternion_xyzw': [float(q.x), float(q.y), float(q.z), float(q.w)],
                'scale': xyz(t.scale3d)}

    def asset_path(obj):
        if not obj:
            return None
        path = obj.get_path_name()
        assert path.startswith(('/Game/', '/Engine/')), 'Do not reference transient material: ' + path
        return path

    def materials(c, mesh, tongue_fallback=None):
        count = max(c.get_num_materials(), len(mesh.get_editor_property('static_materials')))
        out = []
        for i in range(count):
            material = c.get_material(i)
            if material is None and i == 0:
                material = tongue_fallback
            if material is None:
                material = mesh.get_material(i)
            asset_path(material)
            out.append(material)
        assert out and out[0], 'Missing source surface material'
        return out

    # Complete every source read/validation before changing any stage actor.
    specs = [
        ('ActualGum', gum, gum.static_mesh_component,
         gum.static_mesh_component.static_mesh, None),
        ('ActualTongue', tongue, tongue_surface, tongue_mesh,
         tongue.get_editor_property('surface_material')),
        ('ReserveNeighbor_0', sockets[3], sockets[3].get_editor_property('preview'),
         sockets[3].get_editor_property('preview').static_mesh, None),
        ('ActualReserve', sockets[5], reserve_source, reserve_source.static_mesh, None),
        ('ReserveNeighbor_1', sockets[7], sockets[7].get_editor_property('preview'),
         sockets[7].get_editor_property('preview').static_mesh, None),
    ]
    prepared = []
    for role, owner, comp, mesh, fallback in specs:
        assert mesh
        asset_path(mesh)
        source_transform = comp.get_world_transform()
        prepared.append((role, owner, comp, mesh, materials(comp, mesh, fallback),
                         source_transform, map_transform(source_transform)))

    existing = list(actor_subsystem.get_all_level_actors())
    owned = {}
    for role, *_ in prepared:
        matches = [a for a in existing if role in [str(x) for x in a.tags]]
        assert len(matches) <= 1, 'Duplicate stage role tag: ' + role
        if matches:
            a = matches[0]
            assert ownership in [str(x) for x in a.tags], 'Unowned actor occupies role ' + role
            assert isinstance(a, u.StaticMeshActor), role + ' is not a StaticMeshActor'
            owned[role] = a

    actors, components, rows = {}, {}, []
    try:
        for role, owner, source_comp, mesh, mats, src, target in prepared:
            a = owned.get(role)
            if a is None:
                a = actor_subsystem.spawn_actor_from_class(u.StaticMeshActor, target.translation)
                assert a and a.get_world() == stage_world, role
            a.set_actor_label('Respawn_' + role)
            a.tags = [u.Name(x) for x in sorted({str(x) for x in a.tags} |
                     {ownership, role, 'MC.RespawnReplacement.RealArenaPatch'})]
            a.set_folder_path('RespawnReplacement/RealArenaPatch')
            c = a.static_mesh_component
            c.set_mobility(u.ComponentMobility.MOVABLE)
            if c.static_mesh != mesh:
                assert c.set_static_mesh(mesh), role
            c.set_editor_property('override_materials', mats)
            c.set_collision_enabled(u.CollisionEnabled.NO_COLLISION)
            a.set_actor_transform(target, False, False)
            a.set_actor_hidden_in_game(False)
            c.set_visibility(True)
            c.set_hidden_in_game(False)
            actors[role], components[role] = a, c
            rows.append({'role': role, 'source_actor': owner.get_path_name(),
                         'source_component': source_comp.get_path_name(),
                         'target_actor': a.get_path_name(), 'mesh': mesh.get_path_name(),
                         'materials': [asset_path(m) for m in mats],
                         'source_transform': transform_record(src),
                         'target_transform': transform_record(c.get_world_transform()),
                         'reused_actor': role in owned})
        after_dirty = {p.get_path_name() for p in
                       list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages()) +
                       list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())}
        assert (after_dirty - before_dirty) <= {stage_package}, 'Unexpected non-stage package dirtied'
        assert editor.get_editor_world() == stage_world, 'Active world changed'
    finally:
        actor_subsystem.set_selected_level_actors(selected)

    length = math.hypot(dx, dy)
    inward = u.Vector(dx/length, dy/length, 0)
    report = {'source_map': source_world.get_path_name(), 'stage_map': stage_world.get_path_name(),
              'source_tooth_id': 5, 'neighbor_ids': [3, 7], 'source_anchor': xyz(anchor),
              'target_anchor': [75., 170., float(anchor.z)],
              'source_tongue_centre': xyz(centre), 'yaw_offset_degrees': yaw,
              'source_inward': xyz(inward), 'target_inward': xyz(map_direction(inward)),
              'source_z_retained': True, 'scales_retained': True, 'actors': rows,
              'saved_packages': [], 'production_rules_changed': False,
              'limitation': 'Static rest-shape tissue proxies; no native tongue deformation or gameplay.'}
    return {'actors': actors, 'components': components, 'reserve_actor': actors['ActualReserve'],
            'reserve_component': components['ActualReserve'],
            'reserve_mesh': reserve_source.static_mesh,
            'reserve_scale': reserve_source.get_world_transform().scale3d,
            'source_to_target_point': map_point, 'target_to_source_point': source_point,
            'source_to_target_direction': map_direction, 'source_to_target_transform': map_transform,
            'report': report}


def make_support_query(mesh_component):
    """Optional read-only GeometryScript ray against this proxy's render LOD0.

    Returns ``query(world_xy, top_z=2000, bottom_z=-2000)`` -> Vector or None.
    Only a transient DynamicMesh/BVH is created. No collision recook or asset
    changes. Keep the returned callable alive for its transient mesh lifetime.
    Engine signatures checked against UE5.8 MeshAssetFunctions.h/Spatial.h;
    root must validate live Python wrappers before relying on the result.
    """
    import unreal as u

    mesh = mesh_component.static_mesh
    assert mesh and u.SystemLibrary.is_valid(mesh_component)
    dynamic = u.DynamicMesh()
    lod = u.GeometryScriptMeshReadLOD()
    lod.set_editor_property('lod_type', u.GeometryScriptLODType.RENDER_DATA)
    lod.set_editor_property('lod_index', 0)
    copied = u.GeometryScript_AssetUtils.copy_mesh_from_static_mesh(
        mesh, dynamic, u.GeometryScriptCopyMeshFromAssetOptions(), lod)
    assert isinstance(copied, tuple) and len(copied) == 2, 'Inspect live copy wrapper return'
    dynamic, outcome = copied
    assert outcome == u.GeometryScriptOutcomePins.SUCCESS, str(outcome)
    built = u.GeometryScript_MeshSpatial.build_bvh_for_mesh(dynamic)
    assert isinstance(built, tuple) and len(built) == 2, 'Inspect live BVH wrapper return'
    dynamic, bvh = built

    def query(world_xy, top_z=2000.0, bottom_z=-2000.0):
        assert u.SystemLibrary.is_valid(mesh_component)
        transform = mesh_component.get_world_transform()
        start = u.Vector(world_xy.x, world_xy.y, top_z)
        end = u.Vector(world_xy.x, world_xy.y, bottom_z)
        local_start = u.MathLibrary.inverse_transform_location(transform, start)
        local_end = u.MathLibrary.inverse_transform_location(transform, end)
        delta = local_end - local_start
        distance = math.sqrt(delta.x*delta.x + delta.y*delta.y + delta.z*delta.z)
        assert distance > 0
        direction = u.Vector(delta.x/distance, delta.y/distance, delta.z/distance)
        options = u.GeometryScriptSpatialQueryOptions()
        options.set_editor_property('max_distance', distance)
        result = u.GeometryScript_MeshSpatial.find_nearest_ray_intersection_with_mesh(
            dynamic, bvh, local_start, direction, options)
        assert isinstance(result, tuple) and len(result) == 3, 'Inspect live ray wrapper return'
        _, hit, _ = result
        if not hit.get_editor_property('hit'):
            return None
        return u.MathLibrary.transform_location(transform, hit.get_editor_property('hit_position'))
    return query
