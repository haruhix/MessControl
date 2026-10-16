"""Isolated, non-saving Hello1 foot/tongue contact audit for the menu candidate.

Source preparation only until the lead grants a serialized Blender slot. Default
execution evaluates four poses without rendering. --render-contact-sheet adds
exactly four bounded CPU previews; it never modifies or saves a source BLEND.
"""
from pathlib import Path
import argparse, csv, hashlib, json, math, struct, sys, time, traceback, zlib

PROJECT=Path(r'E:\DEVGAME\MessControl')
ROOT=PROJECT/'ArtSource/UI/MainMenu3D/20261015_refB_v1'
ALLOWED_OUTPUT=ROOT/'Assembly/Review/CharacterContact'
DEFAULT_CANDIDATE=ROOT/'Assembly/MC_Menu3D_Candidate_Combined_geometry_v10.blend'
FRAMES=(1,15,30,50)
SOURCE_EXPECTED={
    PROJECT/'ArtSource/CharacterCurrent/CharacterGameplay.blend':'6d835c6a7ec3a1d44a7ed24b9bcdb4a2ebde01c1fcce35501847bac5266f144c',
    PROJECT/'ArtSource/CharacterAnimation/Teeth3/Teeth3GameplayAnimations.blend':'e6aee91e3304f4737d19cf9bbc65f75046cd0f02eafb822bcf7f2e5ab0e2df1f'}

def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def vec(value): return [float(v) for v in value]
def quantile(values,q):
    values=sorted(values)
    if not values:return None
    position=(len(values)-1)*q;lo=int(position);hi=min(lo+1,len(values)-1)
    return values[lo]+(values[hi]-values[lo])*(position-lo)
def statistics(values,weights=None):
    if not values:return {'count':0,'min':None,'max':None,'mean':None,'weighted_mean':None,'median':None,'p05':None,'p95':None}
    return {'count':len(values),'min':min(values),'max':max(values),'mean':sum(values)/len(values),
        'weighted_mean':sum(a*b for a,b in zip(values,weights))/sum(weights) if weights and sum(weights)>0 else None,
        'median':quantile(values,.5),'p05':quantile(values,.05),'p95':quantile(values,.95)}

def args_after_separator():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--candidate',type=Path,default=DEFAULT_CANDIDATE)
    parser.add_argument('--expected-candidate-sha256',help='Optional caller-pinned candidate hash')
    parser.add_argument('--out',type=Path,default=ALLOWED_OUTPUT)
    parser.add_argument('--weight-threshold',type=float,default=.45)
    parser.add_argument('--sole-band',type=float,default=.035,help='World Z band above each weighted-foot minimum')
    parser.add_argument('--contact-tolerance',type=float,default=.025)
    parser.add_argument('--penetration-tolerance',type=float,default=.025)
    parser.add_argument('--gap-warning',type=float,default=.10)
    parser.add_argument('--render-contact-sheet',action='store_true',help='Only after a lead-granted render slot')
    parser.add_argument('--edge',type=int,default=640,help='CPU render width, at most960')
    parser.add_argument('--samples',type=int,default=8,help='CPU samples, at most16')
    return parser.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])

def action_signature(action):
    """Read layered/legacy curves without changing keys or action slots."""
    if hasattr(action,'fcurves'):curves=list(action.fcurves)
    else:
        curves=[]
        for layer in action.layers:
            for strip in layer.strips:
                for bag in getattr(strip,'channelbags',[]):curves.extend(bag.fcurves)
    records=[]
    for curve in curves:
        records.append((curve.data_path,curve.array_index,[(vec(key.co),vec(key.handle_left),vec(key.handle_right),key.interpolation) for key in curve.keyframe_points]))
    return hashlib.sha256(json.dumps(sorted(records),separators=(',',':')).encode()).hexdigest()

def valid_collection_objects(collection,null_entries):
    objects=[]
    for index,obj in enumerate(collection.all_objects):
        if obj is None:
            null_entries.append({'collection':collection.name,'scope':'all_objects','index':index})
        else:objects.append(obj)
    return objects

def reveal_collection(collection,null_entries,visited=None):
    if visited is None:visited=set()
    if collection is None:
        null_entries.append({'collection':None,'scope':'collection_argument'});return
    if collection.name in visited:return
    visited.add(collection.name)
    collection.hide_render=collection.hide_viewport=False
    for index,child in enumerate(collection.children):
        if child is None:null_entries.append({'collection':collection.name,'scope':'children','index':index})
        else:reveal_collection(child,null_entries,visited)
    for obj in valid_collection_objects(collection,null_entries):
        obj.hide_render=obj.hide_viewport=False;obj.hide_set(False)
        parent=obj.parent
        while parent:
            parent.hide_render=parent.hide_viewport=False;parent.hide_set(False);parent=parent.parent

def reveal_layer_path(layer,targets):
    if layer is None:return False
    descendant=layer.collection.name in targets
    for child in layer.children:descendant=reveal_layer_path(child,targets) or descendant
    if descendant:layer.exclude=False;layer.hide_viewport=False
    return descendant

def freeze_mouth(root):
    record={'root':root.name,'property':'OpenAmount','value':1.,'disconnected_action':None,'muted_nla':[],'muted_property_drivers':[]}
    ad=root.animation_data
    if ad:
        if ad.action:record['disconnected_action']=ad.action.name;ad.action=None
        for track in ad.nla_tracks:record['muted_nla'].append(track.name);track.mute=True
        # Keep all other drivers, especially jaw-child drivers, untouched.
        for driver in ad.drivers:
            if driver.data_path=='["OpenAmount"]':driver.mute=True;record['muted_property_drivers'].append(driver.data_path)
    root['OpenAmount']=1.;root.update_tag()
    return record

def foot_membership(mesh,threshold):
    result={}
    for side,suffix in [('LEFT','_l'),('RIGHT','_r')]:
        groups={group.index:('toes' if group.name.startswith('toes_') else 'foot') for group in mesh.vertex_groups if group.name.endswith(suffix) and group.name.startswith(('foot_','toes_'))}
        if not groups:raise RuntimeError(f'Missing {side} foot/toes vertex groups')
        categories={'foot':{},'toes':{},'combined':{}}
        for vertex in mesh.data.vertices:
            weights={'foot':0.,'toes':0.}
            for group in vertex.groups:
                if group.group in groups:weights[groups[group.group]]+=group.weight
            for category in ('foot','toes'):
                if weights[category]>threshold:categories[category][vertex.index]=weights[category]
            total=sum(weights.values())
            if total>threshold:categories['combined'][vertex.index]=total
        if not categories['combined']:raise RuntimeError(f'No {side} weighted foot vertices')
        result[side]={'groups':[mesh.vertex_groups[index].name for index in groups],'categories':categories}
    return result

def world_tongue_tree(tongue,dg):
    evaluated=tongue.evaluated_get(dg);mesh=evaluated.to_mesh()
    try:
        points=[evaluated.matrix_world@vertex.co for vertex in mesh.vertices]
        faces=[tuple(polygon.vertices) for polygon in mesh.polygons]
        bounds={'min':[min(p[i] for p in points) for i in range(3)],'max':[max(p[i] for p in points) for i in range(3)]}
        tree=BVHTree.FromPolygons(points,faces,all_triangles=False,epsilon=0.)
    finally:evaluated.to_mesh_clear()
    return tree,bounds

def evaluate_points(mesh,dg):
    evaluated=mesh.evaluated_get(dg);data=evaluated.to_mesh()
    try:
        if len(data.vertices)!=len(mesh.data.vertices):raise RuntimeError('Topology-changing modifier prevents reliable source vertex-group index correspondence')
        points=[evaluated.matrix_world@vertex.co for vertex in data.vertices]
    finally:evaluated.to_mesh_clear()
    return points

def sample_category(points,weights,tree,tongue_bounds,opt,frame,side,category,rows):
    if not weights:return {'status':'NO_CATEGORY_VERTICES','weighted_vertices':0}
    minimum=min(points[index].z for index in weights)
    sole=[index for index in weights if points[index].z<=minimum+opt.sole_band]
    ztop=max(tongue_bounds['max'][2],max(points[index].z for index in sole))+2.
    raylength=ztop-tongue_bounds['min'][2]+2.
    clearances=[];usedweights=[];supported=0;unsupported=0;grazing=0
    center=sum((points[index]*weights[index] for index in sole),Vector())/sum(weights[index] for index in sole)
    for index in sole:
        point=points[index];hit,normal,face,distance=tree.ray_cast(Vector((point.x,point.y,ztop)),Vector((0,0,-1)),raylength)
        valid=hit is not None and normal.z>.10
        if hit is None:unsupported+=1
        elif not valid:grazing+=1
        else:
            supported+=1;clearances.append(float(point.z-hit.z));usedweights.append(weights[index])
        rows.append({'frame':frame,'side':side,'category':category,'vertex':index,'weight':weights[index],
            'world_x':point.x,'world_y':point.y,'world_z':point.z,'floor_z':hit.z if hit is not None else None,
            'floor_normal_z':normal.z if hit is not None else None,'clearance':point.z-hit.z if valid else None,
            'support':'top_surface' if valid else 'no_ray_hit' if hit is None else 'grazing_or_sidewall'})
    stats=statistics(clearances,usedweights)
    if not clearances:classification='NO_VALID_DORSAL_SUPPORT'
    elif stats['min']< -opt.penetration_tolerance:classification='PENETRATION_CANDIDATE'
    elif stats['min']>opt.gap_warning:classification='AIRBORNE_OR_GAP_CANDIDATE'
    elif abs(stats['min'])<=opt.contact_tolerance:classification='SAMPLED_LOWEST_SOLE_NEAR_FLOOR'
    else:classification='CLEARANCE_REVIEW_REQUIRED'
    return {'status':classification,'weighted_vertices':len(weights),'sole_vertices':len(sole),'sole_vertex_ids':sole,
        'sole_band_world':opt.sole_band,'minimum_z_world':minimum,'weighted_sole_centroid_world':vec(center),
        'supported_vertices':supported,'outside_tongue_rays':unsupported,'grazing_or_sidewall_hits':grazing,
        'signed_clearance_world':stats,'penetrating_vertices':sum(value< -opt.penetration_tolerance for value in clearances),
        'gap_vertices':sum(value>opt.gap_warning for value in clearances),
        'interpretation':'Positive=sole above dorsal floor; negative=penetration. Raised toes/feet may be authored motion; this classification is a review cue, not automatic art approval.'}

def png_decode(data):
    if data[:8]!=b'\x89PNG\r\n\x1a\n':raise RuntimeError('Not a PNG')
    offset=8;compressed=[];width=height=channels=0
    while offset<len(data):
        length=struct.unpack('>I',data[offset:offset+4])[0];kind=data[offset+4:offset+8];payload=data[offset+8:offset+8+length];offset+=12+length
        if kind==b'IHDR':
            width,height,depth,color,compression,filtering,interlace=struct.unpack('>IIBBBBB',payload)
            if depth!=8 or color not in (2,6) or interlace:raise RuntimeError('Contact sheet requires noninterlaced8-bit RGB/RGBA PNGs')
            channels=3 if color==2 else 4
        elif kind==b'IDAT':compressed.append(payload)
        elif kind==b'IEND':break
    raw=zlib.decompress(b''.join(compressed));stride=width*channels;previous=bytearray(stride);rows=[];offset=0
    for _ in range(height):
        filt=raw[offset];row=bytearray(raw[offset+1:offset+1+stride]);offset+=stride+1
        for i in range(stride):
            a=row[i-channels] if i>=channels else 0;b=previous[i];c=previous[i-channels] if i>=channels else 0
            if filt==1:predict=a
            elif filt==2:predict=b
            elif filt==3:predict=(a+b)//2
            elif filt==4:
                p=a+b-c;pa,pb,pc=abs(p-a),abs(p-b),abs(p-c);predict=a if pa<=pb and pa<=pc else b if pb<=pc else c
            elif filt==0:predict=0
            else:raise RuntimeError('Unsupported PNG filter')
            row[i]=(row[i]+predict)&255
        previous=row
        if channels==3:
            rgba=bytearray()
            for i in range(0,stride,3):rgba.extend(row[i:i+3]);rgba.append(255)
            rows.append(bytes(rgba))
        else:rows.append(bytes(row))
    return width,height,rows

def png_encode(width,height,rows):
    def chunk(kind,payload):return struct.pack('>I',len(payload))+kind+payload+struct.pack('>I',zlib.crc32(kind+payload)&0xffffffff)
    return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',width,height,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(b''.join(b'\0'+row for row in rows),6))+chunk(b'IEND',b'')

def contact_sheet(paths,out):
    images=[png_decode(path.read_bytes()) for path in paths];w,h=images[0][:2]
    if any(image[:2]!=(w,h) for image in images):raise RuntimeError('Preview image sizes differ')
    gap=10;header=26;width=w*2+gap*3;height=(h+header)*2+gap*3
    rows=[bytearray(bytes((23,27,31,255))*width) for _ in range(height)]
    digits={'0':['111','101','101','101','111'],'1':['010','110','010','010','111'],'3':['111','001','111','001','111'],'5':['111','100','111','001','111']}
    for index,(iw,ih,source) in enumerate(images):
        x=gap+(index%2)*(w+gap);y=gap+(index//2)*(h+header+gap)
        for j,row in enumerate(source):rows[y+header+j][x*4:(x+w)*4]=row
        # Small pixel digits label actual frame numbers; beauty pixels unchanged.
        for k,digit in enumerate(str(FRAMES[index])):
            for dy,line in enumerate(digits[digit]):
                for dx,cell in enumerate(line):
                    if cell=='1':
                        for sy in range(3):
                            for sx in range(3):
                                tx=x+8+k*12+dx*3+sx;ty=y+4+dy*3+sy;rows[ty][tx*4:tx*4+4]=bytes((238,231,220,255))
    out.write_bytes(png_encode(width,height,rows))

def main():
    global bpy,Vector,BVHTree
    import bpy
    from mathutils import Vector
    from mathutils.bvhtree import BVHTree
    if not bpy.app.background:raise RuntimeError('Run only in an isolated --background --factory-startup process; live Blender documents are protected')
    if bpy.app.version<(5,0,0):raise RuntimeError('This validator requires Blender5+ layered action slots')
    opt=args_after_separator();candidate=opt.candidate.resolve();out=opt.out.resolve()
    if not out.is_relative_to(ALLOWED_OUTPUT.resolve()):raise RuntimeError('Output must remain under Assembly/Review/CharacterContact')
    if candidate.suffix.lower()!='.blend' or not candidate.is_relative_to((ROOT/'Assembly').resolve()):raise RuntimeError('Candidate must be a task-owned Assembly BLEND')
    if not(0<opt.weight_threshold<1 and 0<opt.sole_band<=.15 and 0<opt.contact_tolerance<=.15 and 0<opt.penetration_tolerance<=.15 and opt.gap_warning>0):raise RuntimeError('Invalid contact thresholds')
    if not(128<=opt.edge<=960 and 1<=opt.samples<=16):raise RuntimeError('Preview budget exceeds bounded CPU limit')
    source_before={str(path):sha(path) for path in SOURCE_EXPECTED}
    for path,expected in SOURCE_EXPECTED.items():
        if source_before[str(path)]!=expected:raise RuntimeError(f'Existing source differs from reviewed reuse probe: {path}')
    candidate_before=sha(candidate)
    if opt.expected_candidate_sha256 and candidate_before!=opt.expected_candidate_sha256.lower():raise RuntimeError('Pinned candidate hash changed')
    out.mkdir(parents=True,exist_ok=True);started=time.time()
    bpy.ops.wm.open_mainfile(filepath=str(candidate),load_ui=False)
    scene=bpy.context.scene;root=bpy.data.objects.get('MC_JawsRoot')
    character=bpy.data.collections.get('Menu_ExistingCharacter');interior=bpy.data.collections.get('MenuInterior_Geometry')
    if any(value is None for value in (root,character,interior)):raise RuntimeError('Required assembled mouth/character/interior hierarchy is missing')
    mouth_freeze=freeze_mouth(root)
    null_entries=[]
    for collection in (character,interior):reveal_collection(collection,null_entries)
    targets={collection.name for col in (character,interior) for collection in [col,*col.children_recursive] if collection is not None}
    reveal_layer_path(bpy.context.view_layer.layer_collection,targets)
    character_objects=valid_collection_objects(character,null_entries)
    interior_objects=valid_collection_objects(interior,null_entries)
    rigs=[obj for obj in character_objects if obj.type=='ARMATURE']
    meshes=[obj for obj in character_objects if obj.type=='MESH' and obj.name.startswith('SK_CharacterGameplay')]
    tongues=[obj for obj in interior_objects if obj.type=='MESH' and 'Tongue' in obj.name]
    selection={'null_entries':null_entries,'character_objects':[obj.name for obj in character_objects],'interior_objects':[obj.name for obj in interior_objects],
        'required_candidates':{'rig':[obj.name for obj in rigs],'mesh':[obj.name for obj in meshes],'tongue':[obj.name for obj in tongues]}}
    (out/'collection_selection.json').write_text(json.dumps(selection,indent=2),encoding='utf-8')
    if len(rigs)!=1 or len(meshes)!=1 or len(tongues)!=1:raise RuntimeError('Required actual current rig/mesh/tongue must each be present once; see collection_selection.json')
    rig,mesh,tongue=rigs[0],meshes[0],tongues[0]
    if not any(mod.type=='ARMATURE' and mod.object==rig for mod in mesh.modifiers):raise RuntimeError('Current mesh is not skinned to reused rig')
    if len(mesh.data.vertices)!=4006 or len(rig.data.bones)!=107 or not mesh.data.shape_keys or len(mesh.data.shape_keys.key_blocks)!=48:
        raise RuntimeError('Candidate character identity differs from reviewed Current4006/107bones/48shape-keys; do not sample the old3138-vertex preview skin')
    if any(mod.show_viewport and mod.type!='ARMATURE' for mod in mesh.modifiers):
        raise RuntimeError('Unexpected evaluated character modifier; reviewed Current uses only its existing Armature modifier')
    action=bpy.data.actions.get('A_Teeth_Hello1')
    if action is None:raise RuntimeError('Existing A_Teeth_Hello1 action is absent; do not substitute an invented motion')
    if any(abs(a-b)>1e-6 for a,b in zip(action.frame_range,(1.,50.))):raise RuntimeError('Hello1 frame range differs from reviewed source1–50')
    action_hash=action_signature(action);ad=rig.animation_data_create()
    previous_action=ad.action.name if ad.action else None;previous_slot=getattr(ad.action_slot,'identifier',None)
    slot=ad.action_slot if ad.action==action and ad.action_slot and ad.action_slot.identifier=='OBroot' else next((slot for slot in action.slots if slot.identifier=='OBroot'),None)
    if slot is None:raise RuntimeError('Hello1 OBroot action slot is missing')
    ad.action=action;ad.action_slot=slot;ad.use_nla=False
    selected_slot=slot.identifier;scene.render.fps=30;scene.render.fps_base=1.
    membership=foot_membership(mesh,opt.weight_threshold)
    rows=[];samples=[];render_paths=[]
    if opt.render_contact_sheet:
        if scene.camera is None:raise RuntimeError('Candidate has no reviewed camera')
        ratio=scene.render.resolution_y/max(1,scene.render.resolution_x)
        scene.render.engine='CYCLES';scene.cycles.device='CPU';scene.cycles.samples=opt.samples;scene.cycles.use_denoising=True
        scene.render.threads_mode='FIXED';scene.render.threads=2
        scene.render.resolution_x=opt.edge;scene.render.resolution_y=max(1,round(opt.edge*ratio));scene.render.resolution_percentage=100
        scene.render.image_settings.file_format='PNG';scene.render.image_settings.color_mode='RGBA';scene.render.image_settings.color_depth='8'
        scene.render.use_file_extension=True
    for frame in FRAMES:
        scene.frame_set(frame);root['OpenAmount']=1.;root.update_tag();bpy.context.view_layer.update()
        if abs(float(root['OpenAmount'])-1)>1e-8:raise RuntimeError('Mouth opening is still being animated')
        if ad.action!=action or ad.action_slot!=slot:raise RuntimeError('Existing character action/slot changed while sampling')
        dg=bpy.context.evaluated_depsgraph_get();points=evaluate_points(mesh,dg);tree,tongue_bounds=world_tongue_tree(tongue,dg)
        posed_bbox={'min':[min(point[i] for point in points) for i in range(3)],'max':[max(point[i] for point in points) for i in range(3)]}
        feet={}
        for side,record in membership.items():
            feet[side]={category:sample_category(points,weights,tree,tongue_bounds,opt,frame,side,category,rows) for category,weights in record['categories'].items()}
        evaluated_rig=rig.evaluated_get(dg);bones={}
        for name in ('foot_l','foot_r','toes_01_l','toes_01_r'):
            bone=evaluated_rig.pose.bones.get(name)
            if bone:bones[name]=vec((evaluated_rig.matrix_world@bone.matrix).translation)
        samples.append({'frame':frame,'time_seconds':(frame-1)/30,'open_amount':float(root['OpenAmount']),'posed_bbox_world':posed_bbox,'tongue_bbox_world':tongue_bounds,'feet':feet,'bone_anchors_world':bones})
        if opt.render_contact_sheet:
            path=out/f'Hello1_frame_{frame:03}.png';scene.render.filepath=str(path);bpy.ops.render.render(write_still=True);render_paths.append(path)
        print(f'CONTACT_SAMPLE frame={frame} left={feet["LEFT"]["combined"]["status"]} right={feet["RIGHT"]["combined"]["status"]}',flush=True)
    drift={}
    for side in membership:
        centers=[sample['feet'][side]['combined']['weighted_sole_centroid_world'] for sample in samples]
        drift[side]={'first_frame_centroid_world':centers[0],'sampled_sole_centroid_XY_drift_world':[math.hypot(center[0]-centers[0][0],center[1]-centers[0][1]) for center in centers],
            'caution':'Sole membership can change with foot roll. Compare bone anchors and actual frames before calling this sliding or an intentional step.'}
    if action_signature(action)!=action_hash:raise RuntimeError('Hello1 source action keys changed during read-only audit')
    if sha(candidate)!=candidate_before:raise RuntimeError('Candidate source file changed during isolated audit')
    source_after={str(path):sha(path) for path in SOURCE_EXPECTED}
    if source_after!=source_before:raise RuntimeError('Original character or animation source file changed')
    if render_paths:contact_sheet(render_paths,out/'Hello1_contact_sheet.png')
    report={'schema':'messcontrol.existing_character_contact.v1','status':'SAMPLED_CONTACT_REVIEW_REQUIRED_NOT_ANIMATION_ACCEPTANCE','blender':bpy.app.version_string,
        'candidate':str(candidate),'candidate_sha256':candidate_before,'source_hashes_before':source_before,'source_hashes_after':source_after,'source_files_unchanged':True,
        'original_sources_saved':False,'candidate_saved':False,'mouth_freeze':mouth_freeze,
        'collection_selection':selection,
        'character':{'rig':rig.name,'mesh':mesh.name,'vertices':len(mesh.data.vertices),'shape_keys':len(mesh.data.shape_keys.key_blocks) if mesh.data.shape_keys else 0,
            'action':action.name,'action_slot':selected_slot,'previous_action':previous_action,'previous_slot':previous_slot,'action_curve_sha256_before_after':action_hash,
            'binding_preserved_during_sampling':True,'bone_count':len(rig.data.bones)},
        'tongue':tongue.name,'frames':list(FRAMES),'fps':30,'tolerances_world':{'weight_threshold':opt.weight_threshold,'sole_band':opt.sole_band,'near_contact':opt.contact_tolerance,'penetration':opt.penetration_tolerance,'gap_review':opt.gap_warning},
        'foot_group_names':{side:record['groups'] for side,record in membership.items()},'samples':samples,'horizontal_drift':drift,
        'renders':[str(path) for path in render_paths],'contact_sheet':str(out/'Hello1_contact_sheet.png') if render_paths else None,
        'render_budget':{'enabled':bool(render_paths),'device':'CPU','threads':2,'edge':opt.edge,'samples':opt.samples},
        'limitations':['Four authored poses, not continuous collision checking.','Signed vertical dorsal-floor clearance only, not full-body intersection.','Raising a toe/foot may be authored; gap/slide labels are review cues.','Near-contact thresholds are candidate scene-unit tolerances, not final gameplay tolerances.','No source character, mesh, rest rig, weights, shape keys, action keys or runtime asset is edited.'],
        'elapsed_seconds':time.time()-started}
    (out/'contact_report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    with (out/'sole_vertex_samples.csv').open('w',newline='',encoding='utf-8') as stream:
        writer=csv.DictWriter(stream,fieldnames=list(rows[0]));writer.writeheader();writer.writerows(rows)
    print(json.dumps({'status':report['status'],'report':str(out/'contact_report.json'),'frames':list(FRAMES),'rendered':len(render_paths),'source_files_unchanged':True},indent=2),flush=True)

if __name__=='__main__':
    try:main()
    except Exception:
        traceback.print_exc()
        raise
