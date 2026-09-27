"""Read-only geometry, material and floor audit of the current artist arena."""
from pathlib import Path
import json, traceback
import unreal as u

out = Path(u.Paths.project_saved_dir()) / 'TongueReview'
out.mkdir(parents=True, exist_ok=True)
report = {}
def path(obj): return obj.get_path_name() if obj else None
def vec(v): return [round(v.x, 4), round(v.y, 4), round(v.z, 4)]
def prop(obj, name):
    try: return obj.get_editor_property(name)
    except Exception: return None
def node_graph(mat, root):
    nodes, seen = [], set()
    def visit(node):
        if not node or path(node) in seen: return
        seen.add(path(node))
        item = dict(name=node.get_name(), kind=node.get_class().get_name())
        for key in ('desc', 'description', 'code', 'parameter_name', 'default_value', 'coordinate_index', 'material_function', 'const_a', 'const_b', 'r', 'constant', 'texture'):
            value = prop(node, key)
            if value is not None: item[key] = str(value)
        inputs = u.MaterialEditingLibrary.get_inputs_for_material_expression(mat, node)
        item['inputs'] = [n.get_name() if n else None for n in inputs]
        nodes.append(item)
        for child in inputs: visit(child)
    visit(root)
    return nodes
try:
    assert u.get_editor_subsystem(u.LevelEditorSubsystem).load_level('/Game/Maps/L_Mouth')
    actors = u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors()
    world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
    mesh_edit = u.get_editor_subsystem(u.StaticMeshEditorSubsystem)
    report['actors'] = []
    for actor in actors:
        comps = actor.get_components_by_class(u.PrimitiveComponent)
        if not comps: continue
        item = dict(label=actor.get_actor_label(), kind=actor.get_class().get_name(), location=vec(actor.get_actor_location()), rotation=str(actor.get_actor_rotation()), scale=vec(actor.get_actor_scale3d()), components=[])
        if isinstance(actor, u.MCTongue):
            item.update(source=path(actor.source_mesh), material=path(actor.surface_material), profile=path(actor.profile))
        for comp in comps:
            d = dict(name=comp.get_name(), kind=comp.get_class().get_name(), collision=str(comp.get_collision_enabled()), profile=str(comp.get_collision_profile_name()), hidden=prop(comp, 'hidden_in_game'), visible=prop(comp, 'visible'), transform=str(comp.get_world_transform()))
            if isinstance(comp, u.MeshComponent): d['materials'] = [path(comp.get_material(i)) for i in range(comp.get_num_materials())]
            if isinstance(comp, u.StaticMeshComponent):
                mesh = comp.static_mesh
                if mesh:
                    body = prop(mesh, 'body_setup')
                    d.update(mesh=path(mesh), cpu_access=prop(mesh, 'allow_cpu_access'), bounds=str(mesh.get_bounding_box()), triangles=mesh.get_num_triangles(0), simple_collision=mesh_edit.get_simple_collision_count(mesh), trace_flag=str(prop(body, 'collision_trace_flag')))
            if isinstance(comp, u.ProceduralMeshComponent): d['sections'] = comp.get_num_sections()
            item['components'].append(d)
        report['actors'].append(item)
    tongue = next(a for a in actors if isinstance(a, u.MCTongue))
    report['source'] = dict(cpu_access=prop(tongue.source_mesh, 'allow_cpu_access'), bounds=str(tongue.source_mesh.get_bounding_box()), triangles=tongue.source_mesh.get_num_triangles(0))
    report['pressure'] = str(tongue.profile.get_editor_property('pressure'))
    report['materials'] = []
    for material_path in ('/Game/Gameplay/Arena/MI_TonguePain', '/Game/Gameplay/Arena/M_TonguePain', '/Game/Gameplay/Arena/MI_Wall', '/Game/Gameplay/Arena/M_Wall', '/Game/Art/Materials/MM_Standart_SSS'):
        mat = u.load_asset(material_path)
        if not mat: continue
        entry = dict(path=path(mat), kind=mat.get_class().get_name())
        if isinstance(mat, u.MaterialInstanceConstant):
            entry['parent'] = path(mat.parent)
            for key in ('scalar_parameter_values', 'vector_parameter_values', 'texture_parameter_values', 'base_property_overrides'):
                entry[key] = str(prop(mat, key))
        else:
            entry['outputs'] = {}
            for key in ('MP_WORLD_POSITION_OFFSET', 'MP_PIXEL_DEPTH_OFFSET', 'MP_NORMAL', 'MP_DISPLACEMENT'):
                if hasattr(u.MaterialProperty, key):
                    node = u.MaterialEditingLibrary.get_material_property_input_node(mat, getattr(u.MaterialProperty, key))
                    entry['outputs'][key] = node_graph(mat, node)
        report['materials'].append(entry)
    report['floor'] = []
    def hit_at(x, y, top, bottom, ignored, complex_trace=False):
        hit = u.SystemLibrary.line_trace_single(world, u.Vector(x,y,top), u.Vector(x,y,bottom), u.TraceTypeQuery.TRACE_TYPE_QUERY1, complex_trace, ignored, u.DrawDebugTrace.NONE, True)
        if hit is None or not hit.to_tuple()[0]: return None
        parts=hit.to_tuple()
        return dict(point=vec(parts[5]), normal=vec(parts[7]), actor=parts[9].get_actor_label() if parts[9] else None, component=path(parts[10]))
    ignored=[a for a in actors if a != tongue]
    for x in (-900,-600,-300,0,300,600,900,1200):
        for y in (-600,-300,0,300,600):
            own=hit_at(x,y,1000,-1000,ignored,True)
            scene=hit_at(x,y,own['point'][2]+80 if own else 300,-1000,[])
            report['floor'].append(dict(x=x,y=y,tongue=own,scene=scene))
    for name in ('SM_TongueSurface','SM_MouthShell'):
        mesh=u.load_asset('/Game/Gameplay/Arena/'+name)
        verts, indices, normals, uv, tangents = u.ProceduralMeshLibrary.get_section_from_static_mesh(mesh,0,0)
        data=dict(vertices=[vec(v) for v in verts],indices=list(indices),normals=[vec(v) for v in normals],uv=[[v.x,v.y] for v in uv])
        (out/(name+'.json')).write_text(json.dumps(data),encoding='utf-8')
except Exception:
    report['error']=traceback.format_exc()
finally:
    (out/'audit.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    u.log('MC_TONGUE_REVIEW '+str(out/'audit.json'))
    u.SystemLibrary.quit_editor()
