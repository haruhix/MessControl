"""Read-only inspection in an isolated Blender process; never save the input file."""
import bpy,json,hashlib
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3];OUT=ROOT/'Saved/HeadCosmetics20261024';OUT.mkdir(parents=True,exist_ok=True)
report={'source':bpy.data.filepath,'source_sha256':hashlib.sha256(Path(bpy.data.filepath).read_bytes()).hexdigest(),'blender':bpy.app.version_string,'objects':[],'materials':[],'images':[],'collections':[{'name':c.name,'objects':[o.name for o in c.objects]} for c in bpy.data.collections]}
for o in bpy.data.objects:
    row={'name':o.name,'type':o.type,'parent':o.parent.name if o.parent else None,'parent_type':o.parent_type,'parent_bone':o.parent_bone,'location':list(o.location),'rotation':list(o.rotation_euler),'scale':list(o.scale),'matrix_world':[list(r) for r in o.matrix_world],'hide_render':o.hide_render,'hide_viewport':o.hide_viewport,'collections':[c.name for c in o.users_collection]}
    if o.type=='MESH':
        ps=[o.matrix_world@v.co for v in o.data.vertices]
        row.update(vertices=len(ps),polygons=len(o.data.polygons),bounds_world=[[min(p[i] for p in ps) for i in range(3)],[max(p[i] for p in ps) for i in range(3)]] if ps else [],materials=[m.name if m else None for m in o.data.materials],vertex_groups=[g.name for g in o.vertex_groups],modifiers=[{'name':m.name,'type':m.type,'object':m.object.name if hasattr(m,'object') and m.object else None} for m in o.modifiers])
    if o.type=='ARMATURE':row['bones']=[{'name':b.name,'parent':b.parent.name if b.parent else None,'head':list(b.head_local),'tail':list(b.tail_local)} for b in o.data.bones]
    report['objects'].append(row)
for m in bpy.data.materials:
    row={'name':m.name,'diffuse_color':list(m.diffuse_color),'nodes':[]}
    if m.use_nodes:
        for n in m.node_tree.nodes:
            item={'name':n.name,'type':n.type}
            if n.type=='BSDF_PRINCIPLED':item['inputs']={x.name:list(x.default_value) if hasattr(x.default_value,'__iter__') else x.default_value for x in n.inputs if x.name in ['Base Color','Metallic','Roughness','Alpha','Transmission Weight','IOR']}
            if n.type=='TEX_IMAGE':item['image']=n.image.name if n.image else None
            row['nodes'].append(item)
    report['materials'].append(row)
for i in bpy.data.images:report['images'].append({'name':i.name,'filepath':i.filepath,'size':list(i.size),'packed':bool(i.packed_file)})
(OUT/'SourceInspection.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
print('HEAD_COSMETICS_SOURCE',json.dumps({'blender':report['blender'],'objects':[{k:r.get(k) for k in ['name','type','vertices','bounds_world','parent','materials','modifiers']} for r in report['objects']],'materials':[m['name'] for m in report['materials']],'images':report['images']},ensure_ascii=False))
