"""Infer a diagnostic reflection light from actual smooth mesh normals.

Targets are observed source glints. No normals or mesh positions are modified.
Requires an isolated Blender process, source jaw blend passed on command line.
"""
import json
from pathlib import Path
import sys
import bpy
import numpy as np
from mathutils import Vector

ROOT=Path(r'E:\DEVGAME\MessControl\ArtSource\UI\MainMenu3D\20261015_refB_v1')
out=ROOT/'Evidence/SourceAnalysis/SpecularTargets'
targets=json.loads((out/'upper_highlight_targets.json').read_text())
root=bpy.data.objects['MC_JawsRoot'];root['OpenAmount']=1
bpy.context.view_layer.update()
dg=bpy.context.evaluated_depsgraph_get()
points=[];directions=[];records=[]
for target in targets['parts']:
    obj=next(o for o in bpy.context.scene.objects if o.get('SourcePart')==target['part'])
    obj=obj.evaluated_get(dg)
    x,y=target['highlight_xy'];x=(x/963-.5)*16;z=(205-y)/963*16
    inv=obj.matrix_world.inverted()
    ray_origin=inv@Vector((x,-30,z));ray_dir=(inv.to_3x3()@Vector((0,1,0))).normalized()
    hit,point,normal,face=obj.ray_cast(ray_origin,ray_dir)
    if not hit:raise RuntimeError(f'Highlight missed {obj.name}')
    mesh=obj.data;mesh.calc_loop_triangles()
    triangles=[t for t in mesh.loop_triangles if t.polygon_index==face]
    interp=None
    for tri in triangles:
        a,b,c=[np.asarray(mesh.vertices[i].co,dtype='float64') for i in tri.vertices]
        p=np.asarray(point,dtype='float64')
        ab,ac=b-a,c-a
        uv=np.linalg.lstsq(np.column_stack((ab,ac)),p-a,rcond=None)[0]
        weights=np.array([1-uv.sum(),uv[0],uv[1]])
        if weights.min()>=-1e-4:
            normals=[np.asarray(mesh.corner_normals[i].vector,dtype='float64') for i in tri.loops]
            interp=Vector(sum(w*n for w,n in zip(weights,normals)))
            break
    if interp is None:raise RuntimeError(f'No smooth normal at {obj.name}')
    world_point=obj.matrix_world@point
    world_normal=(obj.matrix_world.inverted().transposed().to_3x3()@interp).normalized()
    view=Vector((0,-1,0))
    light_dir=(2*world_normal.dot(view)*world_normal-view).normalized()
    points.append(np.array(world_point));directions.append(np.array(light_dir))
    records.append({'part':target['part'],'target_xy':target['highlight_xy'],
                    'point_world':list(world_point),'smooth_normal_world':list(world_normal),
                    'reflection_light_direction':list(light_dir)})
lhs=np.zeros((3,3));rhs=np.zeros(3)
for point,direction in zip(points,directions):
    projector=np.eye(3)-np.outer(direction,direction)
    lhs+=projector;rhs+=projector@point
light=np.linalg.solve(lhs,rhs)
for record,point,direction in zip(records,points,directions):
    t=float((light-point)@direction)
    record['line_distance_world']=float(np.linalg.norm(light-(point+t*direction)))
    record['ray_forward_distance']=t
mean_direction=np.mean(directions,axis=0);mean_direction/=np.linalg.norm(mean_direction)
report={'schema':'smooth_normal_highlight_light_diagnostic.v1',
        'status':'DIAGNOSTIC_NOT_SOURCE_OR_LIGHT_ACCEPTANCE',
        'method':'Least squares intersection of reflection rays at observed glint centroids using actual interpolated mesh normals',
        'best_point_light_world':light.tolist(),'mean_distant_light_direction':mean_direction.tolist(),
        'rms_line_distance_world':float(np.sqrt(np.mean([r['line_distance_world']**2 for r in records]))),
        'limitations':['Source glints may come from an area environment and differing lobe roughness.',
                      'Glint centroid is not necessarily peak specular normal.',
                      'Model hidden depth is inferred. Actual render comparison remains required.'],
        'parts':records}
(out/'reflection_light_fit.json').write_text(json.dumps(report,indent=2))
print('HIGHLIGHT_LIGHT_FIT',json.dumps(report))
