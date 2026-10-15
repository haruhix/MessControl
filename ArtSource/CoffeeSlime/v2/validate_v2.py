"""Bounded source/fresh-import checks; no source or imported asset repairs."""
import argparse, json, math, sys
from pathlib import Path
import bpy, bmesh
from mathutils import Matrix, Vector
from mathutils.bvhtree import BVHTree

BASE=Path(__file__).resolve().parent
manifest=json.loads((BASE/'manifest.json').read_text())
parser=argparse.ArgumentParser(); parser.add_argument('--asset',required=True); parser.add_argument('--render',action='store_true')
args=parser.parse_args(sys.argv[sys.argv.index('--')+1:]); path=(BASE/args.asset).resolve()
if path.suffix=='.blend': bpy.ops.wm.open_mainfile(filepath=str(path))
else:
    bpy.ops.object.select_all(action='SELECT'); bpy.ops.object.delete(use_global=False)
    bpy.context.scene.render.fps=100
    if path.suffix=='.glb': bpy.ops.import_scene.gltf(filepath=str(path))
    else: bpy.ops.import_scene.fbx(filepath=str(path),use_anim=True)

errors=[]; report={'asset':args.asset,'blender_version':bpy.app.version_string,'meshes':[],'actions':[]}
def check(ok,message):
    if not ok: errors.append(message)
def choose(rig,action):
    rig.animation_data_create(); rig.animation_data.action=action
    if action and action.slots: rig.animation_data.action_slot=action.slots[0]
    if not action:
        for p in rig.pose.bones: p.matrix_basis=Matrix.Identity(4)
    bpy.context.view_layer.update()
def points(obj):
    ev=obj.evaluated_get(bpy.context.evaluated_depsgraph_get()); mesh=ev.to_mesh()
    pts=[ev.matrix_world@v.co for v in mesh.vertices]; ev.to_mesh_clear(); return pts
def bounds(pts): return [[min(p[i] for p in pts) for i in range(3)],[max(p[i] for p in pts) for i in range(3)]]
def gap(obj):
    ev=obj.evaluated_get(bpy.context.evaluated_depsgraph_get()); mesh=ev.to_mesh()
    pts=[ev.matrix_world@v.co for v in mesh.vertices]
    body=[]; marks=set()
    for p in mesh.polygons:
        if mesh.materials[p.material_index].name.startswith('M_CoffeeSlimeV2_Body'): body.append(list(p.vertices))
        else: marks.update(p.vertices)
    tree=BVHTree.FromPolygons(pts,body); distances=[]
    for i in marks:
        nearest,normal,_,distance=tree.find_nearest(pts[i])
        distances.append((distance,(pts[i]-nearest).dot(normal)))
    ev.to_mesh_clear()
    return {'max_distance_m':max(d[0] for d in distances),'min_signed_m':min(d[1] for d in distances)}

rigs=[o for o in bpy.context.scene.objects if o.type=='ARMATURE']
skins=[o for o in bpy.context.scene.objects if o.type=='MESH' and any(m.type=='ARMATURE' for m in o.modifiers)]
check(len(rigs)==1,'Expected one rig'); rig=rigs[0]
parents={b.name:b.parent.name if b.parent else None for b in rig.data.bones}
check(parents==manifest['skeleton']['bone_parents'],'Five-bone hierarchy mismatch')
choose(rig,None); report['rig']={'name':rig.name,'parents':parents,'object_scale':list(rig.scale)}
for obj in skins:
    mesh=obj.data; mesh.calc_loop_triangles(); pts=[obj.matrix_world@v.co for v in mesh.vertices]; lo,hi=bounds(pts)
    body_faces=[p for p in mesh.polygons if mesh.materials[p.material_index].name.startswith('M_CoffeeSlimeV2_Body')]
    used=sorted(set(i for p in body_faces for i in p.vertices)); indices={n:i for i,n in enumerate(used)}
    bm=bmesh.new(); temp=bpy.data.meshes.new('audit_body_temp')
    temp.from_pydata([mesh.vertices[i].co for i in used],[],[[indices[i] for i in p.vertices] for p in body_faces]); bm.from_mesh(temp)
    bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=1e-6)
    nonmanifold=sum(not edge.is_manifold for edge in bm.edges); bm.free(); bpy.data.meshes.remove(temp)
    weights=[[g.weight for g in v.groups if g.weight>1e-6] for v in mesh.vertices]
    bad_weights=sum(abs(sum(w)-1)>1e-4 for w in weights); max_influences=max(map(len,weights))
    finite_uv=bool(mesh.uv_layers) and all(math.isfinite(c) for uv in mesh.uv_layers.active.data for c in uv.uv)
    degenerate=sum(t.area<1e-12 for t in mesh.loop_triangles)
    materials=[m.name for m in mesh.materials]; dims=[hi[i]-lo[i] for i in range(3)]
    check(nonmanifold==0,'Body is not closed manifold'); check(degenerate==0,'Degenerate triangles')
    check(finite_uv,'UVs missing/nonfinite'); check(bad_weights==0 and max_influences<=4,'Invalid skin weights')
    check(materials==manifest['material_slots'],'Material order mismatch')
    check(len(mesh.loop_triangles)<15000,'Triangle budget'); check(all(abs(a-b)<.01 for a,b in zip(dims,manifest['expected_dimensions_m'])),'Rest dimensions mismatch')
    check(abs(lo[2])<.002,'Floor pivot mismatch')
    report['meshes'].append(dict(name=obj.name,triangles=len(mesh.loop_triangles),dimensions_m=dims,body_nonmanifold_edges=nonmanifold,
        degenerate_triangles=degenerate,finite_uv=finite_uv,materials=materials,max_influences=max_influences,bad_weights=bad_weights))

actions={name:next((a for a in bpy.data.actions if name in a.name),None) for name in manifest['clips']}
required=list(manifest['clips']) if path.suffix in ('.blend','.glb') else ([path.stem] if path.stem.startswith('A_') else [])
for name in required: check(actions[name] is not None,'Missing action '+name)
continuity={}
for name,action in actions.items():
    if not action: continue
    clip=manifest['clips'][name]; fr=list(action.frame_range); fps=bpy.context.scene.render.fps
    duration=(fr[1]-fr[0])/fps; check(abs(duration-clip['duration_seconds'])<.011,name+' duration mismatch')
    choose(rig,action); root0=None; max_root=0.; max_lift=0.; max_gap=0.; min_signed=0.; poses=[]
    for source_frame in clip['critical_frames']:
        f=fr[0]+(source_frame-1)/100*fps; bpy.context.scene.frame_set(int(f),subframe=f-int(f))
        root=rig.pose.bones['root'].matrix.copy()
        if root0 is None: root0=root.copy()
        max_root=max(max_root,max(abs(root[r][c]-root0[r][c]) for r in range(4) for c in range(4)))
        max_lift=max(max_lift,(rig.matrix_world@rig.pose.bones['gel_body'].matrix).translation.z)
        entry={'source_frame':source_frame}
        if skins:
            entry['bounds_m']=bounds(points(skins[0])); g=gap(skins[0]); entry['mark_surface_gap']=g
            max_gap=max(max_gap,g['max_distance_m']); min_signed=min(min_signed,g['min_signed_m'])
        poses.append(entry)
    check(max_root<1e-5,name+' moves root'); check(max_gap<.01,name+' detached surface marks'); check(min_signed>-.005,name+' marks inside body')
    if name.endswith('Hop'): check(max_lift>.30,'Hop lift not at least30cm')
    if name.endswith('PreBurst') or name.endswith('Burst'):
        f=fr[1] if name.endswith('PreBurst') else fr[0]; bpy.context.scene.frame_set(int(f),subframe=f-int(f))
        continuity[name]=[p.matrix.copy() for p in rig.pose.bones]
    report['actions'].append(dict(name=name,frame_range=fr,duration_seconds=duration,max_root_delta=max_root,max_gel_body_lift_m=max_lift,
        max_mark_gap_m=max_gap,min_mark_signed_m=min_signed,critical_poses=poses))
if len(continuity)==2:
    a,b=continuity.values(); delta=max(abs(x[r][c]-y[r][c]) for x,y in zip(a,b) for r in range(4) for c in range(4))
    report['preburst_burst_max_matrix_delta']=delta; check(delta<1e-5,'PreBurst/Burst discontinuity')
if path.stem=='SK_CoffeeSlime' or path.suffix in ('.blend','.glb'): check(len(skins)==1,'Expected one skinned mesh')

report.update(technical_pass=not errors,errors=errors)
audit=BASE/'audit'; audit.mkdir(exist_ok=True); output=audit/(path.stem+path.suffix.replace('.','_')+'.json')
output.write_text(json.dumps(report,indent=2),encoding='utf-8')
print('COFFEE_V2_AUDIT',json.dumps({'report':str(output),'pass':not errors,'errors':errors,'actions':len(report['actions'])}))
if errors: raise RuntimeError('; '.join(errors))

if args.render:
    # Append only the approved review stage. Imported geometry, skin, actions,
    # textures and shaders remain exactly as the fresh GLB importer created them.
    names=['ReviewFloor','Key','Fill','Rim','Review_three_quarter','Review_motion']
    with bpy.data.libraries.load(str(BASE/'CoffeeSlimeV2.blend'),link=False) as (src,dst): dst.objects=[n for n in names if n in src.objects]
    for obj in dst.objects: bpy.context.scene.collection.objects.link(obj)
    scene=bpy.context.scene; scene.render.engine='BLENDER_EEVEE'; scene.view_settings.view_transform='AgX'
    scene.world.use_nodes=True; bg=scene.world.node_tree.nodes['Background']; bg.inputs['Color'].default_value=(.24,.25,.25,1); bg.inputs['Strength'].default_value=.45
    scene.render.resolution_percentage=100; scene.render.image_settings.file_format='PNG'
    for role,frame,cam in [('Idle',1,'Review_three_quarter'),('Hop',44,'Review_motion'),('PreBurst',61,'Review_motion')]:
        action=actions['A_CoffeeSlime_'+role]; choose(rig,action)
        f=action.frame_range[0]+(frame-1)/100*scene.render.fps; scene.frame_set(int(f),subframe=f-int(f))
        scene.render.resolution_x=1200 if role=='Idle' else 720; scene.render.resolution_y=800 if role=='Idle' else 720
        scene.camera=bpy.data.objects[cam]; scene.render.filepath=str(BASE/'review'/('fresh_glb_'+role+'.png')); bpy.ops.render.render(write_still=True)
