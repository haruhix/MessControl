"""Reapply arena contact settings after replacing artist meshes, via Epic MCP.

Geometry, material slots, level placement, and tongue materials are preserved.
Run with the editor open and PIE stopped.
"""
import json
from pathlib import Path

from native_mcp_client import NativeMCP


def main():
    client = NativeMCP()

    def call(toolset, tool, **arguments):
        result = client.tool('call_tool', dict(toolset_name=toolset,
                                              tool_name=tool, arguments=arguments))
        if result.get('isError'):
            raise RuntimeError(result)
        return json.loads(result['content'][0]['text'])['returnValue']

    objects = 'editor_toolset.toolsets.object.ObjectTools'
    meshes = 'editor_toolset.toolsets.static_mesh.StaticMeshTools'
    assets = 'editor_toolset.toolsets.asset.AssetTools'
    assert not call('EditorToolset.EditorAppToolset', 'IsPIERunning'), 'Stop PIE first'
    report = {}
    for name in ('SM_Gum', 'SM_Hole', 'SM_Wall_01'):
        package = '/Game/Art/Meshes/Arena/' + name
        mesh = dict(refPath=package + '.' + name)

        def geometry():
            return dict(
                triangles=call(meshes, 'get_triangle_count', mesh=mesh, lod_index=0),
                vertices=call(meshes, 'get_vertex_count', mesh=mesh, lod_index=0),
                bounds=call(meshes, 'get_bounds', mesh=mesh),
                materials=json.loads(call(objects, 'get_properties', instance=mesh,
                                          properties=['staticMaterials']))['staticMaterials'])

        before = geometry()
        body = json.loads(call(objects, 'get_properties', instance=mesh,
                               properties=['bodySetup']))['bodySetup']
        wanted = dict(collisionTraceFlag='CTF_UseComplexAsSimple', bDoubleSidedGeometry=True)
        previous = json.loads(call(objects, 'get_properties', instance=body,
                                   properties=list(wanted)))
        if previous != wanted:
            assert call(objects, 'set_properties', instance=body, values=json.dumps(wanted))
            assert call(assets, 'save_assets', asset_paths=[package])
        current = json.loads(call(objects, 'get_properties', instance=body,
                                  properties=list(wanted)))
        assert current == wanted, (name, current)
        assert geometry() == before, 'Geometry/materials changed: ' + name
        report[name] = dict(geometry=before, collision_before=previous, collision_after=current)
    destination = Path(__file__).resolve().parents[2] / 'Saved/ArtistMeshIntegration.json'
    destination.write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
