"""Repair the colleague's exported facial skin on a derived mesh. Blender --background --factory-startup --python ..."""
from pathlib import Path
import bpy, math, json
from mathutils import Quaternion

root=Path(__file__).resolve().parents[2]
folder=root/'ArtSource/CharacterGameplay'
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=str(folder/'TeethFaceSource.fbx'))
obj=next(o for o in bpy.context.scene.objects if o.type=='MESH')
rig=next(o for o in bpy.context.scene.objects if o.type=='ARMATURE')
# Classify connected shells across FBX's duplicated UV/normal seams. Do not weld the actual mesh.
weld={}; ids=[]
for v in obj.data.vertices:
    key=tuple(round(c,3) for c in v.co)
    if key not in weld: weld[key]=len(weld)
    ids.append(weld[key])
adj=[set() for _ in weld]
for e in obj.data.edges:
    a,b=(ids[i] for i in e.vertices); adj[a].add(b); adj[b].add(a)
left=set(range(len(adj))); repaired=[]; lid_bones=[]; body_remapped=0; enamel=set()
while left:
    a=left.pop(); group={a}; queue=[a]
    while queue:
        for b in adj[queue.pop()]:
            if b in left: left.remove(b); group.add(b); queue.append(b)
    verts=[v.index for v in obj.data.vertices if ids[v.index] in group]
    # These counts identify the four authored shells in the checked-in source revision.
    if len(group) in (140,120):
        side='l' if sum(obj.data.vertices[i].co.x for i in verts)>0 else 'r'
        part='top' if len(group)==140 else 'bot'; bone='c_eyelid_'+part+'_'+side
        center=rig.data.bones['c_eye_'+side].head_local
        for vg in obj.vertex_groups: vg.remove(verts)
        (obj.vertex_groups.get(bone) or obj.vertex_groups.new(name=bone)).add(verts,1,'REPLACE')
        for i in verts:
            delta=obj.data.vertices[i].co-center
            # 0.8 cm clearance prevents the low-poly lid from intersecting the eyeball when rotated.
            obj.data.vertices[i].co=center+delta.normalized()*(delta.length+.8)
        repaired.append({'bone':bone,'vertices':len(verts),'clearance_cm':.8})
        lid_bones.append((bone,48 if part=='top' else -28))
    elif len(group)==611:
        enamel.update(verts)
        # Moving eyelids must not pull neighbouring tooth enamel. Preserve every other body influence.
        for i in verts:
            vertex=obj.data.vertices[i]; transfer=[]
            for g in vertex.groups:
                name=obj.vertex_groups[g.group].name
                if name.startswith(('c_eyelid_','c_eye_offset_','c_eye_ref_track_')):
                    transfer.append((g.group,g.weight))
            if not transfer: continue
            head=obj.vertex_groups['head_x']
            existing=next((g.weight for g in vertex.groups if g.group==head.index),0)
            for index,weight in transfer: obj.vertex_groups[index].remove([i])
            head.add([i],existing+sum(w for _,w in transfer),'REPLACE'); body_remapped+=1
assert len(repaired)==4 and body_remapped>0, 'Source topology changed; review shell classification before rebuilding.'
# The lip ring is a separate shell. Match the enamel around its cutout to the lip
# weights, fading to the existing face weights away from the seam. Otherwise a
# moving mouth reveals a dark gap between two independently skinned surfaces.
lip_vertices=[]
for v in obj.data.vertices:
    if v.index in enamel or v.co.y>-24 or not 44<v.co.z<70 or abs(v.co.x)>22:continue
    lip_weight=sum(g.weight for g in v.groups if obj.vertex_groups[g.group].name.startswith('c_lips_'))
    if lip_weight>.55:lip_vertices.append(v)
assert len(lip_vertices)>30, 'Missing lip shell; review source topology.'
mouth_remapped=0
for index in sorted(enamel):
    v=obj.data.vertices[index]
    if v.co.y>-22 or not 42<v.co.z<74 or abs(v.co.x)>25:continue
    nearest=sorted(((v.co-l.co).length,l.index) for l in lip_vertices)[:3]
    distance=nearest[0][0]
    if distance>=6:continue
    blend=max(0,min(1,(6-distance)/3.5))
    weights={g.group:g.weight*(1-blend) for g in v.groups}
    normalizer=sum(1/max(.05,d)**2 for d,i in nearest)
    for d,i in nearest:
        influence=blend/max(.05,d)**2/normalizer
        for g in obj.data.vertices[i].groups:weights[g.group]=weights.get(g.group,0)+g.weight*influence
    for group in obj.vertex_groups:group.remove([index])
    total=sum(weights.values())
    for group,weight in weights.items():
        if weight>0:obj.vertex_groups[group].add([index],weight/total,'REPLACE')
    mouth_remapped+=1
assert mouth_remapped>=15, 'Mouth seam was not found.'
for vertex in obj.data.vertices:
    assert abs(sum(g.weight for g in vertex.groups)-1)<.001
obj.name='SK_TeethGameplay';obj.data.name='TeethGameplaySkin'
obj['repair_notes']='Rigid eyelid weights, 0.8 cm clearance, mouth cutout follows lip ring weights. Body/limb rig and UVs preserved.'
# Editable blink preview for the artist; not baked into the gameplay FBX.
for bone,degrees in lid_bones:
    p=rig.pose.bones[bone];p.rotation_mode='QUATERNION';basis=rig.data.bones[bone].matrix_local.to_quaternion()
    for frame,amount in [(1,0),(7,1),(13,0)]:
        p.rotation_quaternion=basis.inverted()@Quaternion((1,0,0),math.radians(degrees*amount))@basis
        p.keyframe_insert('rotation_quaternion',frame=frame,group=bone)
bpy.context.scene.frame_start=1;bpy.context.scene.frame_end=13;bpy.context.scene.render.fps=24;bpy.context.scene.frame_set(1)
bpy.ops.object.select_all(action='DESELECT');obj.select_set(True);rig.select_set(True);bpy.context.view_layer.objects.active=rig
bpy.ops.wm.save_as_mainfile(filepath=str(folder/'TeethGameplay.blend'))
bpy.ops.export_scene.fbx(filepath=str(folder/'SK_TeethGameplay.fbx'),use_selection=True,object_types={'ARMATURE','MESH'},
    add_leaf_bones=False,use_armature_deform_only=False,bake_anim=False,mesh_smooth_type='FACE')
(folder/'FaceRepair.json').write_text(json.dumps({'source':'TeethFaceSource.fbx','eyelids':repaired,'body_vertices_remapped':body_remapped,'mouth_vertices_remapped':mouth_remapped,
    'vertices':len(obj.data.vertices),'bones':len(rig.data.bones)},indent=2)+'\n',encoding='utf8')
print('MC_FACE_REPAIR_PASS')
