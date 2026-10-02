"""Exact native-source collision planning without Unreal/package writes.

Dependency versions and native Tetgen bytes are pinned. Open boundaries require
an explicit cap certificate; nonlinear sockets must be fully hidden by a closed
source union. All output remains a geometry candidate until native cook/runtime QA.
"""
from __future__ import annotations
import copy
import hashlib
import importlib.util
import json
import platform
from pathlib import Path
import sys
import time
import numpy as np
from food_collision_components import analyze
from food_collision_geometry import triangle_distances
from food_collision_star import decompose
from food_collision_tetra import merge_tetrahedra
from plan_food_collision import orient_closed_faces


def cap_open(component, orient_closed, maximum_plane_residual_cm=.0002):
    points = np.asarray(component['vertex_data'], float)
    faces = np.asarray(component['index_data'], int).reshape(-1, 3)
    uses = {}
    for face in faces:
        for a, b in ((face[0], face[1]), (face[1], face[2]), (face[2], face[0])):
            key = tuple(sorted((int(a), int(b))))
            uses[key] = uses.get(key, 0)+1
    boundary = [edge for edge, count in uses.items() if count == 1]
    neighbours = {}
    for a, b in boundary:
        neighbours.setdefault(a, []).append(b)
        neighbours.setdefault(b, []).append(a)
    if not boundary or any(len(v) != 2 for v in neighbours.values()):
        return None, {'status': 'unsupported_boundary_graph', 'boundary_edges': len(boundary)}
    remaining = set(neighbours)
    loops, added = [], []
    while remaining:
        loop = [min(remaining)]
        while True:
            next_ids = sorted(v for v in neighbours[loop[-1]] if len(loop) < 2 or v != loop[-2])
            nxt = next_ids[0]
            if nxt == loop[0]:
                break
            if nxt in loop:
                return None, {'status': 'self_revisiting_boundary'}
            loop.append(nxt)
        remaining -= set(loop)
        p = points[loop]
        _, _, axes = np.linalg.svd(p-p.mean(axis=0))
        residual = float(abs((p-p.mean(axis=0)) @ axes[-1]).max())
        if residual > maximum_plane_residual_cm:
            return None, {'status': 'nonplanar_boundary_requires_review', 'loop': loop, 'max_plane_residual_cm': residual}
        uv = (p-p.mean(axis=0)) @ axes[:2].T
        area = np.sum(uv[:, 0]*np.roll(uv[:, 1], -1)-uv[:, 1]*np.roll(uv[:, 0], -1))
        order = list(range(len(loop)))
        if area < 0:
            order.reverse()

        def cross(a, b, c):
            x, y = uv[b]-uv[a], uv[c]-uv[a]
            return float(x[0]*y[1]-x[1]*y[0])

        triangles = []
        while len(order) > 3:
            found = False
            for k in range(len(order)):
                a, b, c = order[k-1], order[k], order[(k+1)%len(order)]
                if cross(a, b, c) <= 1e-12:
                    continue
                inside = any(cross(a,b,q) >= -1e-12 and cross(b,c,q) >= -1e-12 and cross(c,a,q) >= -1e-12
                             for q in order if q not in (a,b,c))
                if inside:
                    continue
                triangles.append([loop[a], loop[b], loop[c]])
                del order[k]
                found = True
                break
            if not found:
                return None, {'status': 'boundary_ear_clip_failed', 'loop': loop}
        triangles.append([loop[q] for q in order])
        added.extend(triangles)
        loops.append({'loop': loop, 'max_plane_residual_cm': residual, 'triangle_count': len(triangles)})
    combined = np.concatenate((faces, np.asarray(added, int)))
    oriented = orient_closed(points, combined)
    if oriented is None or oriented[1] <= 1e-9:
        return None, {'status': 'caps_not_valid_closed_solid', 'loops': loops}
    c = copy.deepcopy(component)
    c.update(closed=True, boundary_edges=0, nonmanifold_edges=0,
             index_data=oriented[0].reshape(-1).tolist(), triangle_count=len(combined))
    cap_status = ('collision_only_planar_caps' if all(loop['max_plane_residual_cm'] <= .0002 for loop in loops)
                  else 'proposed_collision_only_nonplanar_caps')
    return c, {'status': cap_status, 'loops': loops,
               'added_triangle_indices': added, 'closed_volume_cm3': oriented[1],
               'note': 'Render geometry remains unchanged. Added cap surface requires explicit distance/runtime review.'}


def area(p):
    if len(p)<3:
        return 0.0
    return float(np.linalg.norm(sum((np.cross(p[i]-p[0],p[i+1]-p[0])
                     for i in range(1,len(p)-1)), np.zeros(3)))/2)


def clip(p,n,d,inside=True):
    if len(p)<3:
        return []
    distance=p@n+d
    flags=distance<=0 if inside else distance>=0
    out=[]
    for i in range(len(p)):
        j=(i+1)%len(p)
        if flags[i]: out.append(p[i])
        if bool(flags[i]) != bool(flags[j]):
            t=distance[i]/(distance[i]-distance[j])
            out.append(p[i]+t*(p[j]-p[i]))
    if len(out)<3:
        return []
    q=np.asarray(out)
    if area(q)<1e-14:
        return []
    return q


def subtract(p,planes):
    """Partition convex polygon outside a convex solid, retaining all pieces."""
    n,d=planes
    distances=p@n.T+d
    if np.any(np.all(distances>0,axis=0)):
        return [p]
    if np.all(distances<=0):
        return []
    work=p
    outside=[]
    for ni,di in zip(n,d):
        if len(work)<3:
            break
        remainder=clip(work,ni,di,inside=False)
        if len(remainder)>=3:
            outside.append(remainder)
        work=clip(work,ni,di,inside=True)
    return outside


def planes(h):
    p=np.asarray(h['vertex_data'])
    f=np.asarray(h['index_data']).reshape(-1,3)
    t=p[f]
    n=np.cross(t[:,1]-t[:,0],t[:,2]-t[:,0])
    length=np.linalg.norm(n,axis=1)
    n=n[length>1e-15]/length[length>1e-15,None]
    d=-np.einsum('ij,ij->i',n,t[length>1e-15,0])
    reverse=p.mean(axis=0)@n.T+d>0
    n[reverse]*=-1;d[reverse]*=-1
    # This expands the tested envelope by 1e-7 cm, far below source weld error.
    return p.min(axis=0),p.max(axis=0),(n,d-1e-7)


def first_plane(points):
    p = np.asarray(points, dtype=np.float32)
    cross = np.cross(p[1]-p[0], p[2]-p[0])
    length = float(np.linalg.norm(cross))
    if length <= 1e-20:
        return {'noncollinear': False, 'cross_length_cm2': length,
                'maximum_distance_cm': 0.0}
    n = cross / np.float32(length)
    return {'noncollinear': True, 'cross_length_cm2': length,
            'maximum_distance_cm': float(abs((p-p[0]) @ n).max())}


def json_sha(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True).encode('utf-8')).hexdigest()


def validate_native_audit(source):
    """Guard source payload, exact menu scope and current row-scale evidence."""
    if source.get('complete') is not True or source.get('errors'):
        raise ValueError('A complete, error-free native audit is required')
    meshes, scope, rows = source.get('meshes'), source.get('scope'), source.get('saved_table_rows')
    if not isinstance(meshes, list) or not meshes or not isinstance(scope, list) or not isinstance(rows, list):
        raise ValueError('Native meshes, full saved menu scope and table rows are required')
    paths = [m['path'].split('.')[0] for m in meshes]
    if len(paths) != len(set(paths)) or len(scope) != len(set(scope)) or set(paths) != set(scope):
        raise ValueError('Audit scope and unique native mesh paths differ')
    expected_uses = {}
    for row in rows:
        for key, scale_key in (('WholeMeshes','Scale'),('FragmentMeshes','FragmentScale')):
            for path in row.get(key, []):
                if path and path != 'None':
                    expected_uses.setdefault(path.split('.')[0], []).append(
                        {'row':row['Name'], 'fragment':key=='FragmentMeshes', 'scale':row[scale_key]})
    if not set(expected_uses).issubset(set(scope)):
        raise ValueError('Current saved table references meshes absent from the audit scope')
    for mesh in meshes:
        sections = mesh.get('native_render_sections')
        if mesh.get('error') or not sections or json_sha(sections) != mesh.get('render_geometry_sha256'):
            raise ValueError(f"Native render hash/payload mismatch: {mesh.get('path')}")
        for section in sections:
            p = np.asarray(section['vertices'], dtype=float)
            f = np.asarray(section['triangles'], dtype=int)
            if p.ndim != 2 or p.shape[1] != 3 or not np.isfinite(p).all():
                raise ValueError('Non-finite or malformed native render positions')
            if not len(f) or len(f)%3 or f.min()<0 or f.max()>=len(p):
                raise ValueError('Invalid native triangle indices')
        actual = [u for u in mesh.get('table_uses',[]) if not u.get('native_fallback')]
        expected = expected_uses.get(mesh['path'].split('.')[0], [])
        if sorted(map(json_sha,actual)) != sorted(map(json_sha,expected)):
            raise ValueError(f"Current menu scale/use mismatch: {mesh['path']}")
        if not actual and not any(u.get('native_fallback') for u in mesh.get('table_uses', [])):
            raise ValueError('Unreferenced mesh is not an explicit native fallback')
    return {'scope_sha256':json_sha(sorted(scope)), 'saved_table_rows_sha256':json_sha(rows),
            'source_render_sha256':{m['path'].split('.')[0]:m['render_geometry_sha256'] for m in meshes}}


def load_exact_runtime(dependencies_root, tetgen_library=None):
    """Load only the pinned NumPy/SciPy/raw Tetgen runtime; no PyVista/VTK."""
    lock = json.loads((Path(__file__).parent/'food_collision_exact_dependencies.json').read_text())
    version = '.'.join(map(str,sys.version_info[:3]))
    if version != lock['python'] or platform.python_implementation() != lock['implementation']:
        raise RuntimeError(f"Exact source planning requires {lock['implementation']} {lock['python']}; got {version}")
    if sys.platform != 'win32' or platform.machine().lower() not in ('amd64','x86_64'):
        raise RuntimeError('Pinned raw Tetgen runtime supports Windows x64')
    sys.path.insert(0,str(Path(dependencies_root).resolve()))
    import scipy
    from scipy.optimize import linprog
    if np.__version__ != lock['packages']['numpy'] or scipy.__version__ != lock['packages']['scipy']:
        raise RuntimeError('NumPy/SciPy versions differ from the exact-source dependency lock')
    native_path = Path(tetgen_library or Path(dependencies_root)/'tetgen/_tetgen.pyd').resolve()
    native_hash = hashlib.sha256(native_path.read_bytes()).hexdigest()
    if native_hash != lock['tetgen_native_sha256']:
        raise RuntimeError('Raw Tetgen bytes differ from the checked pinned native wheel')
    spec = importlib.util.spec_from_file_location('_tetgen',native_path)
    native = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(native)
    evidence = {'python':version,'platform':lock['platform'], 'packages':lock['packages'],
                'tetgen_native_sha256':native_hash,'tetgen_native_path':str(native_path),
                'dependency_lock_sha256':hashlib.sha256((Path(__file__).parent/'food_collision_exact_dependencies.json').read_bytes()).hexdigest()}
    return linprog,native,evidence


def source_component_candidate(component, linprog, tetgen, cache):
    """Select source-kernel or constrained tetrahedron convex union, with proof data."""
    p=np.asarray(component['vertex_data'],float)
    f=np.asarray(component['index_data'],int).reshape(-1,3)
    oriented=orient_closed_faces(p,f)
    if oriented is None or oriented[1]<=1e-9:
        raise ValueError('Source component is not a consistently oriented closed solid')
    f,surface_volume=oriented
    key=hashlib.sha256(p.astype('<f8').tobytes()+f.astype('<i8').tobytes()).hexdigest()
    if key in cache:
        result=copy.deepcopy(cache[key]);result['identical_source_geometry_cache_hit']=True
        return result
    c=copy.deepcopy(component);c['index_data']=f.reshape(-1).tolist()
    t=p[f];n=np.cross(t[:,1]-t[:,0],t[:,2]-t[:,0]);n/=np.linalg.norm(n,axis=1)[:,None]
    d=-np.einsum('ij,ij->i',n,t[:,0])
    lp=linprog([0,0,0,-1],A_ub=np.column_stack((n,np.ones(len(n)))),b_ub=-d,
               bounds=[(None,None)]*4,method='highs')
    radius=float(lp.x[3]) if lp.success else None
    star=None
    if lp.success and radius>.0002:
        star=decompose(c,kernel=lp.x[:3])
        if star['status']!='exact_star_surface_convex_union':star=None
    def star_candidate():
        return {'algorithm':'source_kernel_star_convex_merge','convex_elems':star['convex_elems'],
                'convex_count':star['convex_count'],'source_volume_cm3':surface_volume,
                'kernel':star['kernel'],'kernel_radius_cm':radius,
                'support_tolerance_cm':star['support_tolerance_cm'],
                'volume_max_error_cm3':star['maximum_absolute_hull_volume_error_cm3']}
    if star is not None and star['convex_count']<=100:
        candidate=star_candidate()
    else:
        try:
            native=tetgen.PyTetgen()
            native.load_mesh(np.ascontiguousarray(p,dtype=np.float64),np.ascontiguousarray(f,dtype=np.int32))
            native.tetrahedralize(plc=1,quality=0,nobisect=1,nomergefacet=1,nomergevertex=1,quiet=1,zeroindex=1)
            nodes,cells=native.return_nodes(),native.return_tets()
            tetra=nodes[cells]
            volume=float(abs(np.einsum('ij,ij->i',tetra[:,1]-tetra[:,0],
                         np.cross(tetra[:,2]-tetra[:,0],tetra[:,3]-tetra[:,0]))).sum()/6)
            merged=merge_tetrahedra(nodes,cells)
            boundary={tuple(sorted(face)) for face in merged['initial_boundary_faces']}
            if not np.array_equal(nodes[:len(p)],p) or boundary!={tuple(sorted(face)) for face in f}:
                raise ValueError('Tetgen modified native source points/boundary triangles')
            if abs(volume-surface_volume)>max(1e-8,surface_volume*1e-7):
                raise ValueError('Tetgen volume differs from the oriented source volume')
            if star is not None and star['convex_count']<merged['convex_count']:
                candidate=star_candidate()
            else:
                candidate={'algorithm':'source_tetgen_convex_closure_merge','convex_elems':merged['convex_elems'],
                           'convex_count':merged['convex_count'],'source_volume_cm3':surface_volume,
                           'tetrahedron_count':len(cells),'tetgen_volume_cm3':volume,
                           'boundary_preserved_exactly':True,'kernel_radius_cm':radius,
                           'support_tolerance_cm':merged['support_epsilon_cm'],
                           'volume_max_error_cm3':merged['maximum_absolute_hull_volume_excess_cm3']}
        except Exception as error:
            if star is None:raise
            candidate=star_candidate();candidate['tetgen_fallback_reason']=str(error)
    cache[key]=copy.deepcopy(candidate)
    return candidate


def cap_union_certificate(caps, existing_hulls):
    """Cover every cap triangle by the current closed-source convex union."""
    hulls=[planes(h) for h in existing_hulls]
    records=[]
    for k,tri in enumerate(caps):
        lo,hi=tri.min(axis=0),tri.max(axis=0)
        candidates=[h for h in hulls if np.all(hi>=h[0]-1e-7) and np.all(lo<=h[1]+1e-7)]
        candidates.sort(key=lambda h:np.linalg.norm((h[0]+h[1])/2-tri.mean(axis=0)))
        pieces=[tri]
        for _,_,hp in candidates:
            pieces=[q for part in pieces for q in subtract(part,hp)]
            if not pieces:break
        records.append({'triangle':k,'source_area_cm2':area(tri),
                        'uncovered_area_cm2':sum(area(part) for part in pieces)})
    return {'all_cap_triangles_covered_by_closed_source_union':all(r['uncovered_area_cm2']<=1e-10 for r in records),
            'triangle_count':len(caps),'uncovered_area_cm2':sum(r['uncovered_area_cm2'] for r in records),
            'halfspace_expansion_tolerance_cm':1e-7,'area_discard_tolerance_cm2':1e-14,'triangles':records}


def bounded_cap_distance(caps, source_triangles, raw_budget_cm, maximum_depth=24):
    """Certify an external cap's distance budget using the 1-Lipschitz distance bound."""
    pending=[(tri,0) for tri in caps]
    accepted_upper=sampled_max=0.0
    evaluated=0
    while pending:
        batch=pending[:512];pending=pending[512:]
        triangles=np.asarray([tri for tri,_ in batch])
        centres=triangles.mean(axis=1)
        distance=triangle_distances(centres,source_triangles)
        radius=np.linalg.norm(triangles-centres[:,None],axis=2).max(axis=1)
        upper=distance+radius
        evaluated+=len(batch)
        sampled_max=max(sampled_max,float(distance.max()))
        for index,(tri,depth) in enumerate(batch):
            if distance[index]>raw_budget_cm:
                raise ValueError('External collision-only cap exceeds its world contact budget')
            if upper[index]<=raw_budget_cm:
                accepted_upper=max(accepted_upper,float(upper[index]));continue
            if depth>=maximum_depth or evaluated+len(pending)>250000:
                raise ValueError('External cap distance could not be certified within finite subdivision budget')
            pairs=((0,1),(1,2),(2,0))
            a,b=max(pairs,key=lambda pair:np.linalg.norm(tri[pair[0]]-tri[pair[1]]))
            c=3-a-b;middle=(tri[a]+tri[b])/2
            pending.extend(((np.asarray([tri[a],middle,tri[c]]),depth+1),
                            (np.asarray([middle,tri[b],tri[c]]),depth+1)))
    return {'method':'Nearest-triangle distance is 1-Lipschitz; recursively bounded centre distance plus triangle radius.',
            'certified_raw_maximum_distance_upper_bound_cm':accepted_upper,
            'maximum_sampled_distance_cm':sampled_max,'raw_budget_cm':raw_budget_cm,
            'evaluated_subtriangles':evaluated,'maximum_subdivision_depth':maximum_depth}


def order_and_guard_hulls(elements):
    """Native float32 planar guard plus exact face-preserving vertex reordering."""
    minimum_width=float('inf');minimum_first=float('inf')
    for index,h in enumerate(elements):
        p=np.asarray(h['vertex_data'],dtype=np.float64)
        f=np.asarray(h['index_data'],dtype=np.int64).reshape(-1,3)
        if len(p)<4 or len(p)>256 or not np.isfinite(p).all():
            raise ValueError('Invalid exact hull vertex count/positions')
        t=p[f];n=np.cross(t[:,1]-t[:,0],t[:,2]-t[:,0]);lengths=np.linalg.norm(n,axis=1)
        normals=n[lengths>1e-20]/lengths[lengths>1e-20,None]
        width=float(np.ptp(p@normals.T,axis=0).min())
        face=f[int(np.argmax(lengths))]
        order=list(map(int,face))+[i for i in range(len(p)) if i not in set(face)]
        inverse=np.empty(len(p),dtype=int);inverse[order]=np.arange(len(p))
        q,g=p[order],inverse[f]
        if not np.array_equal(q[g],p[f]):raise ValueError('Vertex order changed face geometry')
        guard=first_plane(q)
        if not guard['noncollinear'] or guard['maximum_distance_cm']<=.0001:
            raise ValueError(f'Exact hull {index} risks native planar prism inflation')
        h['vertex_data'],h['index_data']=q.tolist(),g.reshape(-1).tolist()
        minimum_width=min(minimum_width,width)
        minimum_first=min(minimum_first,guard['maximum_distance_cm'])
    return {'vertex_ordering':'first three points from widest existing face','face_geometry_changed':False,
            'native_float32_planar_suspects':0,'native_planar_tolerance_cm':.0001,
            'minimum_support_width_cm':minimum_width,'minimum_first_plane_max_distance_cm':minimum_first,
            'actual_native_cooking_verified':False}


def build_exact_plan(source, dependencies_root, tetgen_library=None, actor_scale=20,
                     maximum_external_cap_world_gap_cm=1, source_report=None, progress=None):
    """Generate the complete fresh-scope plan with automatic cap/cook certificates."""
    audit_guard=validate_native_audit(source)
    linprog,tetgen,environment=load_exact_runtime(dependencies_root,tetgen_library)
    if not np.isfinite(actor_scale) or actor_scale<=0 or maximum_external_cap_world_gap_cm<=0:
        raise ValueError('Positive actor scale and external cap distance budget required')
    result={'source_report':str(Path(source_report).resolve()) if source_report else None,
            'source_report_sha256':hashlib.sha256(Path(source_report).read_bytes()).hexdigest() if source_report else None,
            'scope':source['scope'],'scope_sha256':audit_guard['scope_sha256'],
            'saved_table_rows_sha256':audit_guard['saved_table_rows_sha256'],
            'coordinate_space':'native asset-local centimetres; current table scales recorded separately',
            'mode':'exact_source','dependencies':environment,'source_render_changes':False,
            'actual_engine_assets_changed':False,'actual_native_cooking_verified':False,
            'gameplay_performance_validated':False,'meshes':[],'errors':[],'complete':False}
    cache={}
    for mesh in source['meshes']:
        started=time.perf_counter()
        out={'path':mesh['path'].split('.')[0],'source_object_path':mesh['path'],
             'source_render_sha256':mesh['render_geometry_sha256'],'table_uses':mesh['table_uses'],
             'method':'source_convex','convex_elems':[],'voxel_components':[],'components':[]}
        try:
            groups=analyze(mesh)['components']
            candidates={}
            # Existing closed components are the independent cover for socket caps.
            for c in groups:
                if c['closed']:
                    candidates[c['component']]=source_component_candidate(c,linprog,tetgen,cache)
            existing_hulls=[h for candidate in candidates.values() for h in candidate['convex_elems']]
            max_row=max([max(abs(float(v)) for v in u.get('scale',{}).values())
                         for u in mesh['table_uses'] if u.get('scale')] or [1])
            full_source=np.concatenate([np.asarray(c['vertex_data'])[np.asarray(c['index_data']).reshape(-1,3)] for c in groups])
            for original in groups:
                c=original;certificate={'component':c['component'],'source_closed':c['closed']}
                if not c['closed']:
                    c,cap=cap_open(c,orient_closed_faces,maximum_plane_residual_cm=float('inf'))
                    if c is None:raise ValueError(f"Unsupported open boundary: {cap['status']}")
                    p=np.asarray(c['vertex_data']);caps=p[np.asarray(cap['added_triangle_indices'])]
                    cover=cap_union_certificate(caps,existing_hulls)
                    cap['closed_source_union_coverage_certificate']=cover
                    if not cover['all_cap_triangles_covered_by_closed_source_union']:
                        if cap['status']!='collision_only_planar_caps':
                            raise ValueError('Nonplanar socket cap is not completely buried inside closed source components')
                        raw_budget=maximum_external_cap_world_gap_cm/(max_row*actor_scale)
                        bound=bounded_cap_distance(caps,full_source,raw_budget)
                        bound['maximum_composed_scale']=max_row*actor_scale
                        bound['certified_world_maximum_distance_upper_bound_cm']=bound['certified_raw_maximum_distance_upper_bound_cm']*max_row*actor_scale
                        cap['external_cap_distance_certificate']=bound
                    certificate['collision_only_cap']=cap
                    candidates[c['component']]=source_component_candidate(c,linprog,tetgen,cache)
                candidate=candidates[c['component']]
                certificate.update({k:v for k,v in candidate.items() if k!='convex_elems'})
                certificate['status']='exact_closed_source_candidate'
                out['components'].append(certificate)
                out['convex_elems'].extend(candidate['convex_elems'])
            out['cook_guard']=order_and_guard_hulls(out['convex_elems'])
            out['convex_count']=len(out['convex_elems'])
            out['convex_vertex_count']=sum(len(h['vertex_data']) for h in out['convex_elems'])
            out['maximum_current_row_scale']=max_row
            out['stress_actor_scale']=actor_scale
            out['maximum_composed_scale']=max_row*actor_scale
            out['runtime_budget_note']='Source geometry candidate only; actual Chaos cook and compound shape cost require runtime validation.'
        except Exception as error:
            out['method']='requires_component_review';out['error']=str(error)
            result['errors'].append({'path':out['path'],'error':str(error)})
        out['planning_seconds']=time.perf_counter()-started
        result['meshes'].append(out)
        print(out['path'],out['method'],'hulls',out.get('convex_count'),flush=True)
        if progress:progress(result)
    result['planning_finished']=True
    result['complete']=not result['errors'] and len(result['meshes'])==len(source['scope'])
    result['total_convex_hulls']=sum(m.get('convex_count',0) for m in result['meshes'])
    if progress:progress(result)
    return result
