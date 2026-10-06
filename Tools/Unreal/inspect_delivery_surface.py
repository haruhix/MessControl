"""Read the live authored tongue's section and winding without saving packages."""
import json
import unreal

for actor in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    if 'Tongue' not in actor.get_class().get_name():
        continue
    mesh = actor.get_editor_property('source_mesh')
    vertices, indices, normals, uvs, tangents = unreal.ProceduralMeshLibrary.get_section_from_static_mesh(mesh, 0, 0)
    up = down = opposite = 0
    for offset in range(0, len(indices), 3):
        a, b, c = (indices[offset + i] for i in range(3))
        n = (normals[a] + normals[b] + normals[c]) / 3
        up += n.z > .01
        down += n.z < -.01
        cross = (vertices[b] - vertices[a]).cross(vertices[c] - vertices[a])
        opposite += cross.dot(n) < 0
    unreal.log('MC_DELIVERY_SURFACE ' + json.dumps(dict(mesh=mesh.get_path_name(), vertices=len(vertices), triangles=len(indices)//3, up=up, down=down, reverse_winding=opposite)))
