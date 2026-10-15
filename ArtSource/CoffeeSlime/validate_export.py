"""Fresh-process technical checks. Never edits source or launches an application UI."""
import argparse, json, math, sys
from pathlib import Path
import bpy, bmesh
from mathutils import Matrix, Vector

args=argparse.ArgumentParser(); args.add_argument('--asset',required=True); args.add_argument('--report',required=True)
args.add_argument('--render',default=None)
opt=args.parse_args(sys.argv[sys.argv.index('--')+1:])
path=Path(opt.asset).resolve(); manifest=json.loads((Path(__file__).parent/'v1'/'manifest.json').read_text())
if path.suffix=='.blend': bpy.ops.wm.open_mainfile(filepath=str(path))
else:
    bpy.ops.object.select_all(action='SELECT'); bpy.ops.object.delete(use_global=False)
    bpy.context.scene.render.fps=100
    if path.suffix=='.fbx': bpy.ops.import_scene.fbx(filepath=str(path),use_anim=True)
    elif path.suffix=='.glb': bpy.ops.import_scene.gltf(filepath=str(path))
    else: raise ValueError(path)

errors=[]; warnings=[]; result={'asset':str(path),'blender_version':bpy.app.version_string,'meshes':[],'rigs':[],'actions':[]}
def assert_ok(condition,message):
    if not condition: errors.append(message)

rigs=[o for o in bpy.context.scene.objects if o.type=='ARMATURE']
skins=[o for o in bpy.context.scene.objects if o.type=='MESH' and any(m.type=='ARMATURE' for m in o.modifiers)]
for rig in rigs:
    parents={b.name:b.parent.name if b.parent else None for b in rig.data.bones}
    required=manifest['skeleton']['required_bones']
    assert_ok(all(n in parents for n in required),'Missing required deform bones')
    for n in required:
        parent=manifest['skeleton']['bone_parents'][n]
        assert_ok(parents.get(n)==parent,f'Bone parent mismatch {n}: {parents.get(n)} expected {parent}')
    result['rigs'].append({'name':rig.name,'bones':parents,'scale':list(rig.scale)})
    rig.animation_data_create(); rig.animation_data.action=None
    for p in rig.pose.bones: p.matrix_basis=Matrix.Identity(4)
bpy.context.view_layer.update()
mesh_assets=[o for o in bpy.context.scene.objects if o.type=='MESH' and (o in skins or o.name.startswith('SM_CoffeeSlime') or o.name.startswith('UCX_'))]
for o in mesh_assets:
    me=o.data; me.calc_loop_triangles(); bm=bmesh.new(); bm.from_mesh(me)
    raw_nonmanifold=sum(not e.is_manifold for e in bm.edges)
    # glTF intentionally splits vertices at UV/material/normal seams. Evaluate closed
    # geometric topology on a temporary welded copy without changing the imported skin.
    bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=1e-6)
    nonmanifold=sum(not e.is_manifold for e in bm.edges)
    degenerates=sum(t.area<1e-12 for t in me.loop_triangles)
    min_co=Vector([min((o.matrix_world@v.co)[i] for v in me.vertices) for i in range(3)])
    max_co=Vector([max((o.matrix_world@v.co)[i] for v in me.vertices) for i in range(3)])
    weights=[[(g.group,g.weight) for g in v.groups if g.weight>1e-6] for v in me.vertices]
    bad_weights=sum(abs(sum(w for _,w in row)-1)>1e-4 for row in weights) if o in skins else 0
    max_inf=max((len(row) for row in weights),default=0)
    mats=[m.name if m else None for m in me.materials]
    uv_finite=bool(me.uv_layers) and all(math.isfinite(c) for uv in me.uv_layers.active.data for c in uv.uv)
    tex_missing=[]
    for m in me.materials:
        if m and m.use_nodes:
            for n in m.node_tree.nodes:
                if n.type=='TEX_IMAGE' and n.image and n.image.size[0]==0: tex_missing.append(n.image.name)
    assert_ok(nonmanifold==0,f'{o.name}: {nonmanifold} nonmanifold edges')
    assert_ok(degenerates==0,f'{o.name}: degenerate triangles')
    assert_ok(uv_finite,f'{o.name}: missing/nonfinite UVs')
    assert_ok(not tex_missing,f'{o.name}: missing textures {tex_missing}')
    if o in skins:
        assert_ok(len(me.loop_triangles)<=manifest['triangle_budget'],'Triangle budget exceeded')
        assert_ok(max_inf<=4 and bad_weights==0,'Skin weights invalid')
        assert_ok(len(mats)==6,'Expected six material slots')
        dims=max_co-min_co
        for got,want in zip(dims,manifest['expected_dimensions_m']): assert_ok(abs(got-want)<.02,f'Rest dimension {got} differs from {want}')
        assert_ok(abs(min_co.z)<.02,'Rest floor pivot mismatch')
    result['meshes'].append({'name':o.name,'vertices':len(me.vertices),'triangles':len(me.loop_triangles),'materials':mats,
        'bounds_min_m':list(min_co),'bounds_max_m':list(max_co),'dimensions_m':list(max_co-min_co),
        'raw_seam_edges':raw_nonmanifold,'nonmanifold_edges':nonmanifold,'degenerate_triangles':degenerates,'uv_finite':uv_finite,
        'max_skin_influences':max_inf,'unnormalized_vertices':bad_weights,'missing_textures':tex_missing})
    bm.free()
for a in bpy.data.actions:
    # Fresh imported names may include the armature prefix; matching clip suffix is explicit.
    name=next((n for n in manifest['clips'] if n in a.name),None)
    if not name: continue
    c=manifest['clips'][name]; fr=list(a.frame_range); duration=(fr[1]-fr[0])/bpy.context.scene.render.fps
    assert_ok(abs(duration-c['duration_seconds'])<.015,f'{name}: duration {duration} mismatch')
    data={'name':a.name,'clip':name,'frame_range':fr,'duration_seconds':duration,'critical_bounds':[],'max_root_delta':0}
    for rig in rigs:
        rig.animation_data.action=a
        # Blender5 layered slots must be assigned for actual fresh-import evaluation.
        if a.slots:
            compatible=[sl for sl in a.slots if sl.target_id_type=='OBJECT']
            if compatible: rig.animation_data.action_slot=compatible[0]
        baseline=None
        for f in c['critical_frames']:
            time=(f-1)/c['fps']; imported_frame=fr[0]+time*bpy.context.scene.render.fps
            bpy.context.scene.frame_set(int(imported_frame),subframe=imported_frame-int(imported_frame))
            mat=rig.pose.bones['root'].matrix.copy()
            if baseline is None: baseline=mat
            delta=max(abs(mat[r][col]-baseline[r][col]) for r in range(4) for col in range(4))
            data['max_root_delta']=max(data['max_root_delta'],delta)
            bounds=[]
            dg=bpy.context.evaluated_depsgraph_get()
            for o in skins:
                ev=o.evaluated_get(dg); m=ev.to_mesh()
                points=[ev.matrix_world@v.co for v in m.vertices]
                bounds.append({'mesh':o.name,'min_m':[min(v[i] for v in points) for i in range(3)],'max_m':[max(v[i] for v in points) for i in range(3)]})
                ev.to_mesh_clear()
            data['critical_bounds'].append({'source_frame':f,'bounds':bounds})
        assert_ok(data['max_root_delta']<1e-5,f'{name}: root motion')
    result['actions'].append(data)
if path.stem=='CoffeeSlime': assert_ok(len(result['actions'])==5,'Expected all five actions')
if path.stem.startswith('A_CoffeeSlime'): assert_ok(len(result['actions'])==1,'Expected one exported clip')
if path.stem=='SK_CoffeeSlime': assert_ok(len(skins)==1,'Expected one skin mesh')
result.update(errors=errors,warnings=warnings,technical_pass=not errors)
Path(opt.report).write_text(json.dumps(result,indent=2),encoding='utf-8')
if opt.render:
    # Render ONLY fresh imported asset and its own imported materials, without repairing them.
    for rig in rigs:
        rig.animation_data.action=None
        for p in rig.pose.bones: p.matrix_basis=Matrix.Identity(4)
    s=bpy.context.scene; s.frame_set(1); s.render.engine='BLENDER_EEVEE'
    s.render.resolution_x=1000; s.render.resolution_y=1000; s.render.resolution_percentage=100
    s.view_settings.view_transform='AgX'; s.render.image_settings.file_format='PNG'
    w=s.world; w.use_nodes=True; w.node_tree.nodes['Background'].inputs['Color'].default_value=(.23,.27,.30,1)
    w.node_tree.nodes['Background'].inputs['Strength'].default_value=.35
    for name,pos,energy,size,color in [('Key',(3,-3,4),450,4,(1,.85,.70)),('Fill',(2,3,2.5),220,3,(.70,.82,1)),('Rim',(-2,0,3.4),450,2,(1,.92,.78))]:
        d=bpy.data.lights.new(name,'AREA'); d.energy=energy; d.shape='DISK'; d.size=size; d.color=color
        o=bpy.data.objects.new(name,d); s.collection.objects.link(o); o.location=pos
        o.rotation_euler=(Vector((0,0,.65))-o.location).to_track_quat('-Z','Y').to_euler()
    bpy.ops.mesh.primitive_plane_add(size=200,location=(0,0,-.013)); floor=bpy.context.object
    m=bpy.data.materials.new('Review floor'); m.use_nodes=True
    p=m.node_tree.nodes.get('Principled BSDF'); p.inputs['Base Color'].default_value=(.12,.16,.18,1); p.inputs['Roughness'].default_value=.75
    floor.data.materials.append(m)
    cam=bpy.data.cameras.new('FreshImport_Camera'); o=bpy.data.objects.new('FreshImport_Camera',cam); s.collection.objects.link(o)
    o.location=(4,-3,2.5); o.rotation_euler=(Vector((0,0,.62))-o.location).to_track_quat('-Z','Y').to_euler()
    cam.type='ORTHO'; cam.ortho_scale=3; s.camera=o; s.render.filepath=str(Path(opt.render).resolve()); bpy.ops.render.render(write_still=True)
print('COFFEE_VALIDATION',json.dumps({'asset':path.name,'pass':not errors,'errors':errors,'meshes':[{k:m[k] for k in ('name','triangles','dimensions_m','nonmanifold_edges','max_skin_influences')} for m in result['meshes']],'actions':len(result['actions'])}))
if errors: raise RuntimeError('; '.join(errors))
