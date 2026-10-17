"""Standalone art study: tissue shape keys, staged enamel fracture, same eyes.

Source scenes are only read. Different native topologies are NOT claimed to be
compatible morph targets. Native phase 3 supplies the common infection mesh;
anatomical bone frames fit its contained and asymmetric targets. This is a
blocking study for review, not a production retopology or Unreal integration.
"""
import hashlib, json, math
from pathlib import Path
import bpy
from mathutils import Matrix, Quaternion, Vector
from mathutils.kdtree import KDTree
import numpy as np

ROOT = Path(__file__).resolve().parents[2]
DEST = ROOT/'ArtSource/CarriesTransformation'
BLEND = DEST/'Carries_ThreePhase_Transformation_v8.blend'
REPORT = DEST/'AuthorReport_v8.json'
SOURCES = [
    ROOT/'ArtSource/ZombieBoss/ZombieBoss.blend',
    ROOT/'ArtSource/CarriesStage2/HandSupportWalk/Carries_Stage2_HandSupportWalk_v4.blend',
    ROOT/'ArtSource/BossPhase3ARP/BossPhase3ARP_Source_20261007.blend',
]

def sha(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for b in iter(lambda:f.read(1024*1024), b''): h.update(b)
    return h.hexdigest()

def smooth(t):
    t=max(0.,min(1.,t)); return t*t*(3-2*t)

def envelope(frame, pairs):
    if frame<=pairs[0][0]: return pairs[0][1]
    for (a,x),(b,y) in zip(pairs,pairs[1:]):
        if frame<=b: return x+(y-x)*smooth((frame-a)/(b-a))
    return pairs[-1][1]

def frame_matrix(matrix):
    return Matrix.LocRotScale(matrix.translation, matrix.to_quaternion(), Vector((1,1,1)))

def snapshot(path, phase):
    bpy.ops.wm.open_mainfile(filepath=str(path))
    scene=bpy.context.scene
    scene.frame_set(0 if phase==1 else 1)
    bpy.context.view_layer.update()
    selected={1:['SK_ZombieBoss','ZombieBossMouth'],
              2:['SM_Character','SM_Character.001'],
              3:['SM_Character_006_002_003.002','SM_Character_006_002_003.001']}[phase]
    dg=bpy.context.evaluated_depsgraph_get()
    all_coords=[o.evaluated_get(dg).matrix_world@v.co for n in selected for o in [bpy.data.objects[n]] for v in o.evaluated_get(dg).data.vertices]
    # Normalize only the phase 3 artist scene, whose model is about six units tall.
    factor=1. if phase<3 else 1.45/(max(v.z for v in all_coords)-min(v.z for v in all_coords))
    floor=min(v.z for v in all_coords)
    center_x=0. if phase<3 else (min(v.x for v in all_coords)+max(v.x for v in all_coords))*.5
    norm=Matrix.Translation((-center_x*factor,0,-floor*factor+.025))@Matrix.Scale(factor,4)
    material_cache={}
    def material_record(mat):
        if mat is None: return dict(name='Missing', color=[.4,.22,.1,1], image=None)
        if mat.name in material_cache:return material_cache[mat.name]
        images=[n.image for n in mat.node_tree.nodes if n.type=='TEX_IMAGE' and n.image] if mat.use_nodes else []
        image=next((i for i in images if 'basecolor' in i.name.lower().replace('_','')), images[0] if images else None)
        saved=None
        if image:
            if len(image.pixels):_ = image.pixels[0]
            if not image.has_data:
                reused=list((DEST/'Textures').glob('Phase2_'+image.name+'*.png'))
                if reused:
                    image=bpy.data.images.load(str(reused[0]),check_existing=True)
                    _ = image.pixels[0]
                else:raise RuntimeError('Missing source texture: '+image.name)
            saved=DEST/'Textures'/('Phase%d_'%phase+image.name.replace(':','_').replace('/','_').replace('\\','_')+'.png')
            saved.parent.mkdir(parents=True,exist_ok=True)
            image.filepath_raw=str(saved);image.file_format='PNG';image.save()
        rec=dict(name='Phase%d_'%phase+mat.name, color=list(mat.diffuse_color), image=str(saved) if saved else None)
        material_cache[mat.name]=rec
        return rec
    objects=[]
    for name in selected:
        original=bpy.data.objects[name];ev=original.evaluated_get(dg);mesh=ev.to_mesh()
        assert len(mesh.vertices)==len(original.data.vertices), (name,'evaluated topology changed')
        objects.append(dict(name=name,
            vertices=[norm@ev.matrix_world@v.co for v in mesh.vertices],
            raw_vertices=[norm@original.matrix_world@v.co for v in original.data.vertices],
            faces=[list(p.vertices) for p in mesh.polygons],
            material_indices=[p.material_index for p in mesh.polygons],
            uv=[tuple(v.uv) for v in mesh.uv_layers.active.data] if mesh.uv_layers.active else None,
            groups=[{original.vertex_groups[g.group].name:g.weight for g in v.groups} for v in original.data.vertices],
            materials=[material_record(s.material) for s in original.material_slots]))
        ev.to_mesh_clear()
    rig=bpy.data.objects['Armature' if phase==1 else 'rig']
    bones={p.name:dict(matrix=frame_matrix(norm@rig.matrix_world@p.matrix),
                      length=(norm@rig.matrix_world@p.tail-norm@rig.matrix_world@p.head).length)
           for p in rig.pose.bones}
    bb_bones={}
    if phase==2:
        # The source has an ARP modifier followed by BoneBrush. Fit anatomical
        # frames through the same BoneBrush skin transforms, using native paint.
        bb=bpy.data.objects['BoneBrushRig']
        bb_bones={p.name:dict(matrix=frame_matrix(norm@bb.matrix_world@p.matrix),
                            length=(norm@bb.matrix_world@p.tail-norm@bb.matrix_world@p.head).length)
                  for p in bb.pose.bones}
        raw=objects[0]
        skin={p.name:norm@bb.matrix_world@p.matrix@p.bone.matrix_local.inverted()@bb.matrix_world.inverted()@norm.inverted()
              for p in bb.pose.bones if p.bone.use_deform}
        bb_names=set(skin)
        for name,record in bones.items():
            scores={}
            for weights in raw['groups']:
                a=weights.get(name,0.)
                if a:
                    for n,w in weights.items():
                        if n in bb_names:scores[n]=scores.get(n,0.)+a*w
            if scores:
                total=sum(scores.values()); matrix=Matrix(((0,0,0,0),)*4)
                for n,w in scores.items(): matrix+=skin[n]*(w/total)
                record['matrix']=frame_matrix(matrix@record['matrix'])
    return dict(objects=objects,bones=bones,bb_bones=bb_bones,normalization=factor)

def bone_name(name, bones, phase):
    candidates=[name]
    if phase==1:
        n=name.replace('.','_'); candidates += [n,n.removeprefix('c_')]
        n=n.removeprefix('c_')
        candidates += [n.replace('arm_stretch','arm').replace('forearm_stretch','forearm'),
                       n.replace('arm_twist_offset','arm').replace('forearm_twist_offset','forearm_twist')]
        for finger in ('middle','ring','pinky'):
            if finger in n:candidates += [n.replace(finger,'index')]
    return next((n for n in candidates if n in bones),None)

def fit_target(obj, reference, source_bones, phase):
    target=[]; matched=0
    for co,weights in zip(obj['vertices'],obj['groups']):
        position=Vector();total=0.
        for n,w in weights.items():
            if n not in source_bones:continue
            dst=bone_name(n,reference['bones'],phase)
            if dst is None:continue
            src=source_bones[n];to=reference['bones'][dst]
            # Uniform local length fit preserves finger cross sections and is
            # blended by the original anatomical skin weights.
            ratio=max(.045,min(3.,to['length']/max(src['length'],1e-5)))
            local=src['matrix'].inverted()@co
            point=to['matrix']@(local*ratio)
            position+=point*w;total+=w
        if total>.01:
            position/=total;matched+=1
        else:
            position=Vector((co.x*.65,co.y*.65,co.z*.75))
        target.append(position)
    return target,matched

def fit_asymmetric_surface(main3, phase2):
    """Fit with evaluated anatomical landmarks, not stacked-rig bind matrices."""
    source=main3;target=phase2['objects'][0]
    def centroid(obj,name,ids):
        rows=[(obj['vertices'][i],obj['groups'][i].get(name,0.))for i in ids]
        weight=sum(w for _,w in rows)
        return sum((co*w for co,w in rows),Vector())/weight if weight>1.5 else None
    names={n for w in source['groups'][:1301]for n in w}
    paired=[]
    for name in sorted(names):
        a=centroid(source,name,range(1301))
        b=centroid(target,name,range(2477) if any(t in name for t in('lips','jaw','cheek','head')) else range(1882,2477))
        if a is not None and b is not None:paired.append((a,b))
        elif a is not None and name.endswith('.r')and any(t in name for t in ('arm','hand','thumb','index','middle','ring','pinky')):
            paired.append((a,Vector((-.17+a.x*.09,.10+a.y*.08,.79+(a.z-.7)*.09))))
    assert len(paired)>12,len(paired)
    x=np.array([list(a)for a,b in paired]);y=np.array([list(b-a)for a,b in paired])
    distances=np.sum((x[:,None,:]-x[None,:,:])**2,axis=2)
    kernel=np.exp(-distances/(.42**2));kernel+=np.eye(len(x))*.002
    affine=np.concatenate((np.ones((len(x),1)),x),axis=1)
    system=np.block([[kernel,affine],[affine.T,np.zeros((4,4))]])
    coefficients=np.linalg.solve(system,np.concatenate((y,np.zeros((4,3))),axis=0))
    points=np.array([list(v)for v in source['vertices']])
    k=np.exp(-np.sum((points[:,None,:]-x[None,:,:])**2,axis=2)/(.42**2))
    p=np.concatenate((np.ones((len(points),1)),points),axis=1)
    result=points+np.concatenate((k,p),axis=1)@coefficients
    # Only upper tissue uses this fit; face gets explicit island fits below.
    return [Vector(v)for v in result],len(paired)

def fit_arm_chain(main3, co2, bones3, phase2):
    bb=phase2['bb_bones']
    def mapping(group):
        if not group.endswith('.l'):return None
        if 'forearm' in group:return 'forearm.l','segment_002_02',1.55
        if any(t in group for t in ('arm_stretch','arm_twist','shoulder')):return 'arm.l','segment_002_01',1.45
        if 'hand' in group:return 'hand.l','segment_008',1.6
        for finger,number in [('thumb','009'),('ring','010'),('pinky','011')]:
            if finger in group:
                digit=next((n for n in '123'if n in group),'1')
                if 'base' in group:return 'hand.l','segment_008',1.6
                return group,'segment_'+number+'_0'+digit,1.6
        return None
    for i in range(1301):
        original=main3['vertices'][i];point=Vector();weight=0.
        for name,w in main3['groups'][i].items():
            record=mapping(name)
            if record is None:continue
            srcname,dstname,cross=record
            if srcname not in bones3 or dstname not in bb:continue
            src=bones3[srcname];dst=bb[dstname]
            length=src['length']
            if srcname=='forearm.l':length=(bones3['hand.l']['matrix'].translation-src['matrix'].translation).length
            ratio=dst['length']/max(length,1e-6)
            local=src['matrix'].inverted()@original
            local=Vector((local.x*cross,local.y*ratio,local.z*cross))
            point+=dst['matrix']@local*w;weight+=w
        if weight>.001:co2[i]=co2[i].lerp(point/weight,min(1.,weight))

def fit_head_volume(main3, two):
    def mouth(obj,ids):
        selected=[obj['vertices'][i]for i in ids if any('lips' in n and w>.05 for n,w in obj['groups'][i].items())]
        return sum(selected,Vector())/len(selected)
    mouth3=mouth(main3,range(1301));mouth2=mouth(two,range(144))
    eye3=sum(main3['vertices'][2882:3330]+main3['vertices'][3330:3778],Vector())/896
    eye2=sum(two['vertices'][633:1081],Vector())/448
    top3=max(v.z for v in main3['vertices'][:1301]);top2=max(v.z for v in two['vertices'][144:633])
    srcz=[.29,mouth3.z,eye3.z,top3];dstz=[.37,mouth2.z,eye2.z,top2]
    assert all(a<b for a,b in zip(srcz,srcz[1:])),srcz
    output=[]
    for co in main3['vertices']:
        z=float(np.interp(co.z,srcz,dstz))
        output.append(Vector((co.x*.80+.065,co.y*.62-.055,z)))
    return output

def inverse_arm_fit(two,bones3,phase2):
    bb=phase2['bb_bones'];mapping={'segment_002_01':'arm.l','segment_002_02':'forearm.l','segment_008':'hand.l'}
    for number,finger in [('009','thumb'),('010','ring'),('011','pinky')]:
        for digit in '123':mapping['segment_'+number+'_0'+digit]=(''if digit=='1'else'c_')+finger+digit+'.l'
    output=[]
    for co,weights in zip(two['vertices'],two['groups']):
        core=Vector((.02+(co.x-.07)*.65,.08+co.y*.55,.80+(co.z-.65)*.92))
        point=Vector();total=0.
        for name,w in weights.items():
            if name not in mapping:continue
            dstname=mapping[name]
            if dstname not in bones3:continue
            src=bb[name];dst=bones3[dstname]
            local=src['matrix'].inverted()@co
            ratio=dst['length']/max(src['length'],1e-6)
            if name=='segment_002_02':ratio=(bones3['hand.l']['matrix'].translation-dst['matrix'].translation).length/src['length']
            local=Vector((local.x*.62,local.y*ratio,local.z*.62))
            point+=dst['matrix']@local*w;total+=w
        output.append(core.lerp(point/total,min(1.,total))if total>.001 else core)
    return output

def fit_island_bounds(source, target, source_ids, target_ids, output):
    a=[source['vertices'][i]for i in source_ids];b=[target['vertices'][i]for i in target_ids]
    amin=Vector([min(v[k]for v in a)for k in range(3)]);amax=Vector([max(v[k]for v in a)for k in range(3)])
    bmin=Vector([min(v[k]for v in b)for k in range(3)]);bmax=Vector([max(v[k]for v in b)for k in range(3)])
    for i in source_ids:
        co=source['vertices'][i]
        output[i]=Vector([bmin[k]+(co[k]-amin[k])*(bmax[k]-bmin[k])/max(amax[k]-amin[k],1e-6)for k in range(3)])

def material(rec):
    mat=bpy.data.materials.get(rec['name'])
    if mat:return mat
    mat=bpy.data.materials.new(rec['name']);mat.diffuse_color=rec['color'];mat.use_nodes=True
    if rec['image']:
        image=bpy.data.images.load(rec['image'],check_existing=True)
        image.pack()
        node=mat.node_tree.nodes.new('ShaderNodeTexImage');node.image=image
        mat.node_tree.links.new(node.outputs['Color'],mat.node_tree.nodes.get('Principled BSDF').inputs['Base Color'])
        mat.node_tree.nodes.active=node
    return mat

def mesh_object(name, source, face_ids=None, pivot=None, coords=None):
    face_ids=list(range(len(source['faces']))) if face_ids is None else list(face_ids)
    indices=sorted({v for i in face_ids for v in source['faces'][i]})
    remap={n:i for i,n in enumerate(indices)}
    pivot=Vector() if pivot is None else pivot
    coords=source['vertices'] if coords is None else coords
    data=bpy.data.meshes.new(name+'_Mesh')
    data.from_pydata([coords[i]-pivot for i in indices],[],[[remap[v]for v in source['faces'][i]]for i in face_ids]);data.update()
    ob=bpy.data.objects.new(name,data);bpy.context.collection.objects.link(ob);ob.location=pivot
    for rec in source['materials']:data.materials.append(material(rec))
    offset=[0]
    for f in source['faces']:offset.append(offset[-1]+len(f))
    uv=data.uv_layers.new(name='UVMap') if source['uv'] else None
    for polygon, old in zip(data.polygons,face_ids):
        polygon.material_index=source['material_indices'][old];polygon.use_smooth=True
        if uv:
            for j,loop in enumerate(polygon.loop_indices):uv.data[loop].uv=source['uv'][offset[old]+j]
    return ob,indices

def shapes(ob, indices, sets):
    ob.shape_key_add(name='Basis')
    keys={}
    for name,coords in sets.items():
        key=ob.shape_key_add(name=name)
        for v,i in zip(key.data,indices):v.co=coords[i]
        keys[name]=key
    return keys

def animate_visibility(ob, begin, end):
    for frame,hide in [(1,begin>1),(begin,False),(end,False),(end+1,True)]:
        ob.hide_render=hide;ob.hide_viewport=hide
        ob.keyframe_insert('hide_render',frame=frame);ob.keyframe_insert('hide_viewport',frame=frame)

def curves(action):
    if hasattr(action,'fcurves'):yield from action.fcurves
    else:
        for layer in action.layers:
            for strip in layer.strips:
                for bag in strip.channelbags:yield from bag.fcurves

def main():
    assert bpy.app.background and not BLEND.exists(),'Author a fresh revision'
    DEST.mkdir(parents=True,exist_ok=True)
    records=[dict(path=str(p),sha256=sha(p))for p in SOURCES]
    phase1,phase2,phase3=[snapshot(path,i+1)for i,path in enumerate(SOURCES)]
    main3=phase3['objects'][0]
    co1,matched1=fit_target(main3,phase1,phase3['bones'],1)
    co2=fit_head_volume(main3,phase2['objects'][0]);matched2=1301
    fit_arm_chain(main3,co2,phase3['bones'],phase2)
    # Keep the undeveloped second arm packed behind the remaining shell.
    for i,(co,weights) in enumerate(zip(co2,main3['groups'])):
        side=sum(w for n,w in weights.items()if n.endswith('.r')and any(t in n for t in('arm','hand','thumb','index','middle','ring','pinky','shoulder')))
        if side>.01 and i<1301:
            co2[i]=co.lerp(Vector((-.16+(co.x+.25)*.14,.10+(co.y-.10)*.16,.77+(co.z-.70)*.16)),min(1.,side))
    # Contained tissue fits inside the original crown. Hands unfold from the
    # shoulder instead of leaving the stacked source armature's folded spikes.
    for i in range(1301):
        old=main3['vertices'][i]
        co1[i]=Vector((old.x*.20,old.y*.30+.025,.77+(old.z-.90)*.30))
    two=phase2['objects'][0]
    fit_island_bounds(main3,two,range(2882,3330),range(2835,3265),co2)
    fit_island_bounds(main3,two,range(3330,3778),range(633,1081),co2)
    for j in range(310):co2[2236+j]=two['vertices'][2477+j]
    fit_island_bounds(main3,two,range(2546,2856),range(1449,1803),co2)
    fit_island_bounds(main3,phase2['objects'][1],range(2856,2882),range(35),co2)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene=bpy.context.scene;scene.render.fps=30;scene.frame_start=1;scene.frame_end=420
    scene.render.engine='BLENDER_WORKBENCH';scene.render.resolution_x=800;scene.render.resolution_y=600;scene.render.resolution_percentage=100
    sh=scene.display.shading;sh.light='STUDIO';sh.studiolight_rotate_z=.4;sh.color_type='TEXTURE'
    sh.show_shadows=True;sh.show_cavity=True;sh.cavity_type='BOTH';sh.curvature_ridge_factor=1.05;sh.curvature_valley_factor=.85
    sh.show_specular_highlight=True;sh.show_object_outline=False;sh.background_type='WORLD'
    scene.world=bpy.data.worlds.new('MC_ReviewWorld')
    scene.world.color=(.085,.095,.11);scene.view_settings.view_transform='Standard'
    tissue_faces=[i for i,f in enumerate(main3['faces'])if all(v<1301 for v in f)]
    face_faces=[i for i,f in enumerate(main3['faces'])if all(v>=2236 for v in f)]
    carrier,indices=mesh_object('MC_Infection_CommonSurface',main3,tissue_faces)
    keys=shapes(carrier,indices,{'Contained_Phase1':co1,'HalfExposed_Phase2':co2})
    carrier['Authoring_note']='Common phase3 topology fitted to anatomical frames; phase1 and phase2 shape targets are studies, not topology-identical source meshes.'
    lids3=phase3['objects'][1]
    lid1,_=fit_target(lids3,phase1,phase3['bones'],1);lid2,_=fit_target(lids3,phase2,phase3['bones'],2)
    lids,lidids=mesh_object('MC_Continuous_UpperLids',lids3)
    lidkeys=shapes(lids,lidids,{'Contained_Phase1':lid1,'HalfExposed_Phase2':lid2})
    fit_island_bounds(lids3,two,range(48),range(2787,2835),lid2)
    fit_island_bounds(lids3,two,range(48,96),range(1081,1435),lid2)
    for data,i in zip(lidkeys['HalfExposed_Phase2'].data,lidids):data.co=lid2[i]
    face,faceids=mesh_object('MC_Continuous_InfectedFace',main3,face_faces)
    facekeys=shapes(face,faceids,{'Contained_Phase1':co1,'HalfExposed_Phase2':co2})
    animate_visibility(carrier,221,420);animate_visibility(face,221,420);animate_visibility(lids,221,420)
    # Keep the artist's exact asymmetric phase2 silhouette. Local shape keys
    # grow its own arm from containment; a second set contracts it inside the
    # phase3 surface while the second arm opens. Facial parts switch at matched
    # poses. This avoids pretending the three native topologies are compatible.
    phase2_body_faces=[i for i,f in enumerate(two['faces'])if all(1882<=v<=2476 for v in f)]
    native2,native2ids=mesh_object('MC_Phase2_ArtistInfection',two,phase2_body_faces)
    contain2=[Vector((.04+(v.x-.08)*.12,.04+v.y*.10,.77+(v.z-.72)*.14))for v in two['vertices']]
    native2keys=shapes(native2,native2ids,{'Arm_Contained':contain2,'Inside_Phase3':inverse_arm_fit(two,phase3['bones'],phase2)})
    animate_visibility(native2,1,285)
    phase2_face_ids=[i for i,f in enumerate(two['faces'])if all(not(144<=v<=632 or 1827<=v<=2476 or 3265<=v<=3314)for v in f)]
    native2face,_=mesh_object('MC_Phase2_ArtistFace',two,phase2_face_ids)
    native2teeth,_=mesh_object('MC_Phase2_ArtistTeeth',phase2['objects'][1])
    animate_visibility(native2face,100,220);animate_visibility(native2teeth,100,220)
    # Native phase1 crown is physically split; the lower roots remain attached.
    source1=phase1['objects'][0]
    retained=KDTree(544)
    for index in list(range(144,633))+list(range(1827,1882)):retained.insert(two['raw_vertices'][index],index)
    retained.balance()
    shell_target=[v.copy()for v in source1['vertices']]
    for i in range(144,755):
        _,target_index,distance=retained.find(source1['raw_vertices'][i])
        shell_target[i]=two['vertices'][target_index]
    original_face_ids=[i for i,f in enumerate(source1['faces'])if all(v<144 or v>966 for v in f)]
    original_face,_=mesh_object('MC_Original_Phase1_Face',source1,original_face_ids)
    original_teeth,_=mesh_object('MC_Original_Phase1_Teeth',phase1['objects'][1])
    animate_visibility(original_face,1,99);animate_visibility(original_teeth,1,99)
    shell_faces=[i for i,f in enumerate(source1['faces'])if all(144<=v<=754 for v in f)]
    groups={}
    for i in shell_faces:
        center=sum((source1['vertices'][v]for v in source1['faces'][i]),Vector())/len(source1['faces'][i])
        if center.z<.40:region='Roots'
        else:
            angle=math.atan2(center.y,center.x)
            sector=int((angle+math.pi)/(2*math.pi)*8)%8
            region=('Upper' if center.z>.83 else 'Side')+'_%d'%sector
        groups.setdefault(region,[]).append(i)
    plates=[];root=None;attached_keys=[]
    for region,faces in groups.items():
        used={v for i in faces for v in source1['faces'][i]}
        center=sum((source1['vertices'][v]for v in used),Vector())/len(used)
        if region=='Roots':
            root,rootids=mesh_object('MC_Preserved_OriginalRoots',source1,faces)
            attached_keys.append(shapes(root,rootids,{'Follow_Phase2_Crown':shell_target})['Follow_Phase2_Crown'])
            continue
        pivot=Vector((center.x*.70,center.y*.70,.40))
        plate,plateids=mesh_object('MC_Enamel_'+region,source1,faces,pivot=pivot)
        solid=plate.modifiers.new('Broken enamel thickness','SOLIDIFY');solid.thickness=.024;solid.offset=-.6
        plate['fracture_pass']=1 if center.x>0 else 2
        if center.x<=0:
            local_target=[v-pivot for v in shell_target]
            attached_keys.append(shapes(plate,plateids,{'Follow_Phase2_Crown':local_target})['Follow_Phase2_Crown'])
            animate_visibility(plate,1,102)
        plates.append((plate,center,pivot))
    native2_plate_groups={}
    shell2ids=set(range(144,633))|set(range(1827,1882))
    for i,f in enumerate(two['faces']):
        if not all(v in shell2ids for v in f):continue
        center=sum((two['vertices'][v]for v in f),Vector())/len(f)
        if center.z<.39:continue
        angle=math.atan2(center.y,center.x)
        region=('Upper'if center.z>.81 else'Side')+str(int((angle+math.pi)/(2*math.pi)*8)%8)
        native2_plate_groups.setdefault(region,[]).append(i)
    for region,faces in native2_plate_groups.items():
        used={v for i in faces for v in two['faces'][i]}
        center=sum((two['vertices'][v]for v in used),Vector())/len(used)
        pivot=Vector((center.x*.65,center.y*.65,.40))
        plate,_=mesh_object('MC_Phase2_Enamel_'+region,two,faces,pivot=pivot)
        solid=plate.modifiers.new('Broken enamel thickness','SOLIDIFY');solid.thickness=.022;solid.offset=-.6
        plate['fracture_pass']=2;animate_visibility(plate,103,420);plates.append((plate,center,pivot))
    # Original small hand silhouettes make the contained tooth recognizable.
    for side,(a,b) in [('PosX',(755,860)),('NegX',(861,966))]:
        faces=[i for i,f in enumerate(source1['faces'])if all(a<=v<=b for v in f)]
        hand,_=mesh_object('MC_Original_SmallHand_'+side,source1,faces)
        animate_visibility(hand,1,76 if side=='PosX' else 102)
    # Two identical-topology eye spheres retain recognizable eyes throughout.
    for s3,s1,count in [(2882,967,448),(3330,1415,448)]:
        for j in range(count):co1[s3+j]=source1['vertices'][s1+j]
    for data,i in zip(keys['Contained_Phase1'].data,indices):data.co=co1[i]
    events={'sick_tooth':1,'first_pressure':50,'first_fracture':78,'first_arm':109,
            'half_exposed':145,'second_pressure':226,'second_fracture':263,'second_arm':301,'fully_exposed':366}
    for name,f in events.items():scene.timeline_markers.new(name,frame=f)
    for frame in range(1,421):
        first=envelope(frame,[(1,0),(54,0),(130,1)])
        second=envelope(frame,[(1,0),(221,0),(341,1)])
        native2keys['Arm_Contained'].value=1-first
        native2keys['Inside_Phase3'].value=second
        for key in native2keys.values():key.keyframe_insert('value',frame=frame)
        pulse=math.sin(frame*.61)*.006*envelope(frame,[(1,.4),(44,1),(76,1),(130,0),(206,0),(241,1),(292,1),(341,0)])
        for group in (keys,lidkeys,facekeys):
            group['Contained_Phase1'].value=1-first
            group['HalfExposed_Phase2'].value=first*(1-second)
            for key in group.values():key.keyframe_insert('value',frame=frame)
        for key in attached_keys:
            key.value=first*(1-second);key.keyframe_insert('value',frame=frame)
        # Anticipation, recoil and settling are baked as smooth body motion.
        lean=envelope(frame,[(1,0),(38,0),(64,7),(78,10),(103,-4),(130,0),(205,0),(238,9),(269,11),(305,-5),(341,0),(366,0)])
        body_offset=envelope(frame,[(1,0),(60,-.055),(81,-.035),(103,.018),(145,0),(224,-.02),(259,-.08),(291,-.025),(324,.04),(351,0)])
        for ob in (carrier,lids,face,native2):
            ob.location=(0,0,body_offset);ob.rotation_euler=(math.radians(lean),0,pulse*1.4)
            ob.keyframe_insert('location',frame=frame);ob.keyframe_insert('rotation_euler',frame=frame)
        # Native root legs are replaced by the matching phase3 lower mesh only
        # while crown debris crosses its seam. This is explicit in the report.
        root.scale=(1+second*.10,1+second*.09,1+second*.30)
        root.keyframe_insert('scale',frame=frame)
        for plate,center,pivot in plates:
            passno=plate['fracture_pass']
            start=(68 if passno==1 else 248)+int(abs(center.y)*19)+(8 if center.z<.83 else 0)
            u=max(0.,min(1.,(frame-start)/47))
            pressure=max(0.,math.sin(math.pi*max(0.,min(1.,(frame-start+20)/23))))*.035 if frame<start+3 else 0
            direction=Vector((1 if center.x>0 else-1,center.y*1.4+.25,0)).normalized()
            travel=direction*(.86*smooth(u)+pressure)
            lift=.56*math.sin(math.pi*u)-.30*u*u
            plate.location=pivot+travel+Vector((0,0,lift+body_offset*(1-smooth(u))))
            plate.rotation_euler=(math.radians(-55*u),math.radians((1 if center.x>0 else-1)*92*u),math.radians(65*u))
            plate.scale=(1,1,1) if u<.94 else(1-(u-.94)/.06,)*3
            plate.keyframe_insert('location',frame=frame);plate.keyframe_insert('rotation_euler',frame=frame);plate.keyframe_insert('scale',frame=frame)
    # Hide native3 lower enamel while the attached original roots serve as the
    # shared anchor. Only the upper infected mesh and face are presented here.
    for action in bpy.data.actions:
        for curve in curves(action):
            for key in curve.keyframe_points:key.interpolation='CONSTANT'if 'hide_'in curve.data_path else'LINEAR'
    bpy.ops.mesh.primitive_plane_add(size=200,location=(0,0,0))
    ground=bpy.context.object;ground.name='MC_Review_Ground';ground.color=(.15,.18,.21,1)
    ground.data.materials.append(material(dict(name='ReviewGround',color=[.15,.18,.21,1],image=None)))
    bpy.ops.object.camera_add(location=(3.4,-5.6,2.65))
    camera=bpy.context.object;camera.name='MC_Transform_ThreeQuarter';camera.data.type='ORTHO';camera.data.ortho_scale=3.85
    target=Vector((0,-.12,.82));camera.rotation_euler=(target-camera.location).to_track_quat('-Z','Y').to_euler();scene.camera=camera
    scene['MC_Scope']='Blender-only transformation blocking; no Unreal asset edits.'
    scene['MC_Mechanism']='Local tissue shape keys in native2/native3; enamel fragments; matched facial component swaps; preserved root anchor; no alpha crossfade.'
    scene.frame_set(1)
    for screen in bpy.data.screens:
        for area in screen.areas:
            if area.type=='VIEW_3D':
                area.spaces.active.region_3d.view_perspective='CAMERA'
                area.spaces.active.shading.color_type='TEXTURE'
    bpy.ops.wm.save_as_mainfile(filepath=str(BLEND))
    assert all(sha(Path(r['path']))==r['sha256']for r in records)
    report=dict(passed=True,blend=str(BLEND),blend_sha256=sha(BLEND),sources=records,fps=30,
                clips=[dict(name='Transformation',action='SCENE_TIMELINE',labelRU='Кариес · три стадии раскрытия',
                            useRU='Черновая пластика переходов · морфы ткани + раскол эмали',frames=420,event_frames=events)],
                scope='Standalone Blender transformation blocking for review before approval.',
                mechanisms=['Native phase2 infection grows via its own contained-arm shape key; exact artist silhouette is preserved during the phase2 hold.',
                            'Native phase3 surface expands from a fitted contained shape; native phase2 infection contracts inside it. Face components switch at matched poses.',
                            'Different source topologies are preserved; this is a hybrid local morph and component transition, not a shared-body mesh morph.',
                            'Separate native phase1 crown plates split into two successive fracture passes.',
                            'Source phase1 root legs preserved as shared anchor; phase3 native lower body masked.',
                            'Recognizable eyes across the original artist models; face components switch at authored poses.',
                            'The source2 leftover50-vertex eye fragment is omitted from the demonstration face to avoid a third eye during exposure.'],
                limitations=['Artistic blocking: anatomical fitting is not final retopology; hand and facial deformation need review.',
                             'The native phase3 contained target is approximate; facial component switches and shell joins need final artistic polishing.',
                             'No gameplay, VFX dust, collision, Unreal import or final character rig authored.'],
                source_normalization=[p['normalization']for p in (phase1,phase2,phase3)],
                vertices_fitted=[matched1,matched2],enamel_fragments=len(plates),source_files_unchanged=True)
    REPORT.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
    print('TRANSFORMATION_AUTHOR_COMPLETE',str(BLEND),flush=True)

if __name__=='__main__':main()
