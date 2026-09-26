"""Redistribute the eye's front surface to resize its painted pupil on the same globe."""
import math
from mathutils import Vector, geometry

PUPIL_SHAPES={'Pupil_Dilate':1.8,'Pupil_Contract':.6}
def add_pupil_shapes(obj):
    mesh=obj.data;mesh.calc_loop_triangles();uv=mesh.uv_layers.active.data
    basis=mesh.shape_keys.key_blocks['Basis']
    # Both eye islands share the artist's UVs. This point lies inside the painted pupil.
    pupil_uv=Vector((.345,.8405))
    eyes=[]
    for side in ['l','r']:
        group=obj.vertex_groups['c_eye_'+side].index
        ids={v.index for v in mesh.vertices if any(g.group==group and g.weight>.99 for g in v.groups)}
        assert len(ids)==494, 'Eye topology changed; review the pupil mapping.'
        center=Vector([(min(basis.data[i].co[a] for i in ids)+max(basis.data[i].co[a] for i in ids))*.5 for a in range(3)])
        pupil=None
        for tri in mesh.loop_triangles:
            if not all(i in ids and basis.data[i].co.y<center.y-8 for i in tri.vertices):continue
            tex=[uv[i].uv.copy() for i in tri.loops]
            if geometry.intersect_point_tri_2d(pupil_uv,*tex):
                pupil=geometry.barycentric_transform(pupil_uv.to_3d(),*(u.to_3d() for u in tex),*(basis.data[i].co for i in tri.vertices));break
        assert pupil is not None
        axis=(pupil-center).normalized();assert axis.y<-.9
        eyes.append((ids,center,axis))
    max_radius_error=0;min_blended_radius_ratio=1
    for name,scale in PUPIL_SHAPES.items():
        key=obj.shape_key_add(name=name,from_mix=False)
        for ids,center,axis in eyes:
            for index in ids:
                p=basis.data[index].co;direction=p-center;radius=direction.length;direction.normalize()
                forward=direction.dot(axis)
                if forward<=0:continue
                tangent=direction-axis*forward
                if tangent.length<1e-6:continue
                theta=math.atan2(tangent.length*scale,forward)
                target=center+radius*(axis*math.cos(theta)+tangent.normalized()*math.sin(theta))
                key.data[index].co=target
                max_radius_error=max(max_radius_error,abs((target-center).length-radius))
                min_blended_radius_ratio=min(min_blended_radius_ratio,((target+p)*.5-center).length/radius)
        key.value=0;key.slider_min=0;key.slider_max=1
    assert max_radius_error<.0001 and min_blended_radius_ratio>.98
    return dict(shape_keys=PUPIL_SHAPES,eye_vertices=[len(e[0]) for e in eyes],
        maximum_radius_error_cm=max_radius_error,minimum_midpoint_radius_ratio=min_blended_radius_ratio)
