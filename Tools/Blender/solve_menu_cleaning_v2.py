"""Deterministic contact animation on the existing deform rig, menu only.

Analytical elbows/knees avoid history-dependent IK flips on disconnected ARP
deform bones. The scene uses centimetre bones and metre object placement.
"""
import bpy,math,json,runpy
from pathlib import Path
from mathutils import Matrix,Vector,Quaternion,geometry
OUT=Path('E:/DEVGAME/MessControl/ArtSource/UI/MenuSceneV2_20261022/Blender')
PROJECT=OUT.parents[3]
rig=bpy.data.objects['RIG_Cleaner'];brush=bpy.data.objects['GEO_Cleaner_ExistingBrush'];scene=bpy.context.scene
character=bpy.data.objects['GEO_Cleaner_ExistingCharacter']
# Preflight the exact canonical geometry before clearing any current actions.
# The helper adds/verifies menu-only shapes, never changes Basis or weights.
mesh_helpers=runpy.run_path(str(PROJECT/'Tools/Blender/export_menu_cleaner_mesh_v2.py'))
menu_shapes=mesh_helpers['ensure_menu_correctives'](character)
acting_spec=mesh_helpers['_load_acting_spec'](character.data,mesh_helpers['DEFAULTS'])
if len(rig.data.bones)!=acting_spec['expected_bones']:raise RuntimeError('Expected canonical 107-bone menu rig.')
mitten_indices=acting_spec['mitten_vertex_indices'];hand_groups=acting_spec['hand_groups']
all_hand_indices=set(i for ids in mitten_indices.values() for i in ids)
all_hand_groups=set(name for names in hand_groups.values() for name in names)
if any(len(ids)!=106 for ids in mitten_indices.values()) or any(name not in rig.pose.bones for name in all_hand_groups):
    raise RuntimeError('Reviewed mitten islands or hand bones differ.')
if any(any(g.weight>1e-5 and character.vertex_groups[g.group].name in all_hand_groups for g in vertex.groups)
       for vertex in character.data.vertices if vertex.index not in all_hand_indices):
    raise RuntimeError('Hand recipe would deform geometry outside the two mitten islands.')
# The iris is painted on these fully weighted eye spheres. Eye-target controls
# no longer drive anything after constraints are removed below.
eye_indices={}
for side in ('l','r'):
    name='c_eye_'+side
    group=character.vertex_groups.get(name)
    if group is None or name not in rig.data.bones or not rig.data.bones[name].use_deform:
        raise RuntimeError('Expected actual weighted/deforming eye bone: '+name)
    ids=[]
    for vertex in character.data.vertices:
        weights={character.vertex_groups[g.group].name:g.weight for g in vertex.groups if g.weight>1e-7}
        if name in weights:
            if abs(weights[name]-1.0)>1e-6 or len(weights)!=1:
                raise RuntimeError('Eye sphere skin weights differ from the reviewed rigid island.')
            ids.append(vertex.index)
    if len(ids)!=448:raise RuntimeError('Expected 448 vertices fully driven by '+name)
    eye_indices[side]=ids
character.data.shape_keys.animation_data_clear()
for key in character.data.shape_keys.key_blocks:
    if key.name!='Basis':key.value=0.0
bpy.data.objects['ROOT_Cleaner'].matrix_world=Matrix.Identity(4)
rig.matrix_world=Matrix.Diagonal((.01,.01,.01,1))
rig.animation_data_clear();brush.animation_data_clear()
bpy.ops.object.select_all(action='DESELECT');rig.select_set(True);bpy.context.view_layer.objects.active=rig
bpy.ops.object.mode_set(mode='EDIT')
for bone in rig.data.edit_bones:bone.use_connect=False
bpy.ops.object.mode_set(mode='OBJECT')
for pb in rig.pose.bones:
    for c in list(pb.constraints):pb.constraints.remove(c)
    pb.matrix_basis=Matrix.Identity(4);pb.rotation_mode='QUATERNION'
rest={b.name:b.matrix_local.copy() for b in rig.data.bones}
palm_offsets={}
for side,indices in mitten_indices.items():
    points=[character.data.vertices[i].co.copy() for i in indices]
    center=sum(points,Vector())/len(points)
    palm_offsets[side]=center-rest['hand_'+side].translation

def bone_order():
    result=[]
    def visit(bone):
        result.append(bone)
        for child in bone.children:visit(child)
    for bone in rig.data.bones:
        if bone.parent is None:visit(bone)
    return result
ordered_bones=bone_order()
layout=json.loads((OUT/'Menu_ContactLayout.json').read_text())
native_hero=Matrix.Translation(Vector(layout['hero_location'])) @ Matrix.Rotation(math.radians(layout['hero_yaw']),4,'Z') @ Matrix.Diagonal((layout['hero_scale'],layout['hero_scale'],layout['hero_scale'],1))
B=Matrix.Diagonal((1,-1,1,1))
NATIVE_CAMERA_CM=(-1020,160,105)
NATIVE_CAMERA_TARGET_CM=(-300,250,75)  # Composition reference; aim uses camera position.
BODY_PLACEMENT_OFFSET_CM=Vector((-18,24,0))
# Native tongue triangle heights plus the own menu WPO ridge were measured
# beneath each sole. Keep the brush/tooth contact group fixed; settle each
# foot onto its different support height with 0.10 cm clearance.
FOOT_SUPPORT_OFFSET_CM={'l':Vector((0,0,-.88)),'r':Vector((0,0,-2.84))}
native_to_source=Matrix.Diagonal((.01,.01,.01,1)) @ B @ native_hero.inverted()
camera_source_world=native_to_source @ Vector(NATIVE_CAMERA_CM)
camera_rig_cm=rig.matrix_world.inverted() @ camera_source_world

# Determine the real painted pupil-forward axis from canonical UVs, rather than
# guessing an eye bone's local axis. No mesh, UV or morph coordinate is changed.
basis=character.data.shape_keys.key_blocks[0]
eye_centres={side:Vector([(min(basis.data[i].co[a] for i in ids)+max(basis.data[i].co[a] for i in ids))*.5 for a in range(3)])
             for side,ids in eye_indices.items()}
character.data.calc_loop_triangles()
if not character.data.uv_layers.active:raise RuntimeError('Painted eye aim requires the canonical atlas UV layer.')
eye_uv=character.data.uv_layers.active.data;painted_eye_axes={}
for side,ids in eye_indices.items():
    idset=set(ids);pupil=None;atlas=Vector((.345,.8405))
    for tri in character.data.loop_triangles:
        if not all(i in idset and basis.data[i].co.y<eye_centres[side].y-3 for i in tri.vertices):continue
        uv=[eye_uv[i].uv.copy() for i in tri.loops]
        if geometry.intersect_point_tri_2d(atlas,*uv):
            pupil=geometry.barycentric_transform(atlas.to_3d(),*(v.to_3d() for v in uv),*(basis.data[i].co for i in tri.vertices))
            break
    if pupil is None:raise RuntimeError('Canonical pupil atlas point was not found on eye '+side)
    painted_eye_axes[side]=(pupil-eye_centres[side]).normalized()
def smooth(t):return t*t*(3-2*t)
def curve(t,ks):
    for (a,x),(b,y) in zip(ks,ks[1:]):
        if a<=t<=b:return x+(y-x)*smooth((t-a)/(b-a))
    return ks[-1][1]
def oriented(name,head,rotation):
    m=rotation.to_matrix().to_4x4() @ rest[name].to_3x3().to_4x4();m.translation=head
    return m
def elbow(start,end,l1,l2,pole):
    delta=end-start;d=delta.length;n=delta.normalized()
    if d>=l1+l2:raise RuntimeError('Unreachable contact '+str((d,l1+l2)))
    a=(l1*l1-l2*l2+d*d)/(2*d)
    p=(pole-n*pole.dot(n)).normalized()
    return start+n*a+p*math.sqrt(max(0,l1*l1-a*a))
def limb(desired,names,target,pole,body_delta):
    upper,lower,end=names
    sr=rest[upper].translation;er=rest[lower].translation;wr=rest[end].translation
    s=body_delta @ sr
    e=elbow(s,target,(er-sr).length,(wr-er).length,pole)
    qu=(er-sr).rotation_difference(e-s);ql=(wr-er).rotation_difference(target-e)
    desired[upper]=oriented(upper,s,qu)
    if upper.startswith('shoulder'):
        arm='arm_stretch_'+upper[-1]
        desired[arm]=oriented(arm,s+qu@(rest[arm].translation-sr),qu)
    desired[lower]=oriented(lower,e,ql)
    desired[end]=oriented(end,target,ql)

def palm_thin_axis(points,centre):
    """PCA's smallest axis, using inverse iteration on a 3x3 covariance.

    Centimetres keep the covariance numerically well scaled. A deterministic
    +Z sign avoids camera-side flips when the palm is almost edge-on.
    """
    delta=[(p-centre)*100 for p in points]
    covariance=Matrix([[sum(v[a]*v[b] for v in delta)/len(delta) for b in range(3)] for a in range(3)])
    if abs(covariance.determinant())<1e-6:raise RuntimeError('Lower mitten PCA is degenerate.')
    inverse=covariance.inverted();normal=Vector((0,0,1))
    for _ in range(16):normal=(inverse@normal).normalized()
    if normal.z<0:normal.negate()
    return normal

def apply_shared_gaze(desired,source_pose,t,pleased_beat):
    centres={side:(source_pose['c_eye_'+side]@rest['c_eye_'+side].inverted())@eye_centres[side] for side in ('l','r')}
    midpoint=(centres['l']+centres['r'])*.5
    # Local brush point (80,0,-14) is the authored contact plane; a 3.5cm
    # upward offset looks at the stain being worked, not at the nearby handle.
    work_point=rig.matrix_world.inverted() @ (brush.matrix_world@Vector((80,0,-14))+Vector((0,0,.035)))
    work_direction=(work_point-midpoint).normalized()
    camera_direction=(camera_rig_cm-midpoint).normalized()
    direction=work_direction.lerp(camera_direction,pleased_beat)
    if direction.length<1e-7:raise RuntimeError('Shared work/camera gaze direction is degenerate.')
    direction.normalize()
    far_target=midpoint+direction*200
    # Shared parallel eye rays toward the 2m target direction avoid near-tool
    # convergence with this character's unusually widely spaced eye spheres.
    aim=(far_target-midpoint).normalized()
    intent=curve(t,[(0,0),(3.2,0),(3.65,1),(4.8,1),(5.6,0),(12,0)])
    limit=math.radians(8+2*intent-2*pleased_beat)
    report={}
    for side in ('l','r'):
        name='c_eye_'+side
        current=(source_pose[name]@rest[name].inverted()).to_3x3()@painted_eye_axes[side]
        correction=current.normalized().rotation_difference(aim)
        correction=Quaternion().slerp(correction,min(1,limit/max(1e-8,correction.angle)))
        pivot=centres[side]
        desired[name]=Matrix.Translation(pivot)@correction.to_matrix().to_4x4()@Matrix.Translation(-pivot)@source_pose[name]
        report[side]={'weighted_vertices':448,'applied_degrees':math.degrees(correction.angle),
                      'limit_degrees':math.degrees(limit),'eye_centre_rig_cm':list(pivot)}
    return report

def round_mitten_grips(regrip,pleased_beat,t):
    """Pose exclusive mittens + two weighted eyes; preserve all other matrices.

    Existing source contact, body, foot and tiny finger-curl solves run first.
    Centroids are evaluated on each actual frame, not copied from five poses.
    Pose matrices use cm; object/world matrices use metres. No x100 bake.
    """
    bpy.context.view_layer.update()
    deps=bpy.context.evaluated_depsgraph_get()
    evaluated=rig.evaluated_get(deps)
    source_pose={pb.name:pb.matrix.copy() for pb in evaluated.pose.bones}
    desired={name:matrix.copy() for name,matrix in source_pose.items()}
    skin=character.evaluated_get(deps);mesh=skin.to_mesh()
    try:
        if len(mesh.vertices)!=4006:
            raise RuntimeError('Evaluated mesh topology differs; mitten indices cannot be used safely.')
        points={side:[skin.matrix_world@mesh.vertices[i].co for i in indices] for side,indices in mitten_indices.items()}
        centres={side:sum(values,Vector())/len(values) for side,values in points.items()}
        lower_thin=palm_thin_axis(points['r'],centres['r'])
    finally:skin.to_mesh_clear()
    shaft=brush.matrix_world.to_3x3().col[0].normalized()
    rig_inverse=rig.matrix_world.inverted();palm_targets={};lower_report={}
    for side in ('l','r'):
        grip=(30.5-3*regrip) if side=='l' else (1.5-regrip)
        roll=(16+3*pleased_beat) if side=='l' else (-12-2*pleased_beat)
        target=brush.matrix_world@Vector((grip,0,0));palm_targets[side]=target*100
        world_delta=Matrix.Translation(target)@Quaternion(shaft,math.radians(roll)).to_matrix().to_4x4()@Matrix.Translation(-centres[side])
        if side=='r':
            thin=(world_delta.to_3x3()@lower_thin).normalized()
            view=(camera_source_world-target).normalized()
            tangent=thin.cross(view)
            angle=min(math.radians(20),math.acos(max(-1,min(1,thin.dot(view)))))
            if tangent.length>1e-7 and angle>1e-7:
                tilt=Quaternion(tangent.normalized(),angle)
                world_delta=Matrix.Translation(target)@tilt.to_matrix().to_4x4()@Matrix.Translation(-target)@world_delta
                after=tilt@thin
            else:after=thin;angle=0.0
            lower_report={'tilt_degrees':math.degrees(angle),'thin_normal_dot_view_before':thin.dot(view),
                          'thin_normal_dot_view_after':after.dot(view),'normal_sign_policy':'+Z deterministic; no camera-side sign flips'}
        rig_delta=rig_inverse@world_delta@rig.matrix_world
        for name in hand_groups[side]:desired[name]=rig_delta@source_pose[name]
    eye_report=apply_shared_gaze(desired,source_pose,t,pleased_beat)
    # Preserve non-hand children in world/rig pose space as well; reconnecting
    # hierarchy by local rotations alone would move unrelated control chains.
    for bone in ordered_bones:
        parent=desired[bone.parent.name] if bone.parent else Matrix.Identity(4)
        rig.pose.bones[bone.name].matrix_basis=bone.convert_local_to_pose(
            desired[bone.name],bone.matrix_local,parent_matrix=parent,
            parent_matrix_local=bone.parent.matrix_local if bone.parent else Matrix.Identity(4),invert=True)
    return {side:desired['hand_'+side].translation.copy() for side in ('l','r')},palm_targets,{'eyes':eye_report,'lower_hand':lower_report}

axis=Vector((0,-.25,.9682458)).normalized();normal=Vector((0,.9682458,.25)).normalized();across=normal.cross(axis).normalized()
base_rot=Matrix((axis,across,normal)).transposed().to_4x4()
contact_tree=None
if 'MC_ActualContactTooth' in bpy.data.objects:
    from mathutils.bvhtree import BVHTree
    tooth=bpy.data.objects['MC_ActualContactTooth']
    native=Matrix.Translation(Vector(layout['tooth_location'])) @ Matrix.Rotation(math.radians(layout['tooth_yaw']),4,'Z') @ Matrix.Diagonal((*layout['tooth_scale'],1))
    tooth.matrix_world=Matrix.Diagonal((.01,.01,.01,1)) @ B @ native_hero.inverted() @ native @ B
    bpy.context.view_layer.update();evaluated_tooth=tooth.evaluated_get(bpy.context.evaluated_depsgraph_get());data=evaluated_tooth.to_mesh()
    try:
        vertices=[tooth.matrix_world@v.co for v in data.vertices]
        contact_tree=BVHTree.FromPolygons(vertices,[list(p.vertices) for p in data.polygons])
    finally:evaluated_tooth.to_mesh_clear()
    anchor,n,_,_=contact_tree.find_nearest(Vector((.30,-.39,.70)))
    normal=n.normalized();axis=(Vector((0,0,1))-normal*normal.z).normalized();across=normal.cross(axis).normalized()
    base_rot=Matrix((axis,across,normal)).transposed().to_4x4()
scene.frame_start=1;scene.frame_end=289;scene.render.fps=24
report=[];previous={}
acting={name:[] for name in ['Mouth_Smile','Mouth_Effort','Eyes_Blink','Eyes_Scary','Eyes_delight','Pupil_Dilate','Pupil_Contract','BlinkCancel_Mouth_Smile','BlinkCancel_Mouth_Effort','Mouth_MenuSoftClosed','Mouth_MenuApertureFollow']}
if any(name not in character.data.shape_keys.key_blocks for name in acting):
    raise RuntimeError('A reviewed menu acting morph is missing.')
for frame in range(1,290):
    scene.frame_set(frame);t=(frame-1)/24
    # Work, notice stubborn coffee, regrip, decisive pass, enjoy the result.
    z=curve(t,[(0,0),(.35,-.06),(.85,.12),(1.28,-.055),(1.84,.13),(2.26,-.04),(2.95,.115),(3.35,.025),(4.55,.025),(5.1,-.035),(5.55,-.035),(6.05,-.08),(6.68,.145),(7.05,-.07),(7.75,.14),(8.22,.045),(9.8,.045),(10.6,.015),(11.3,-.045),(12,0)])
    x=curve(t,[(0,0),(1.84,.025),(3.35,-.025),(4.55,-.025),(5.55,.035),(6.68,.04),(8.22,-.015),(9.8,-.015),(12,0)])
    tilt=curve(t,[(0,-.23),(2.95,-.27),(3.5,-.20),(4.55,-.20),(5.55,-.35),(7.75,-.31),(8.5,-.20),(10,-.20),(12,-.23)])
    lift=curve(t,[(0,0),(3.1,0),(3.5,.018),(4.6,.018),(5.75,0),(8.1,0),(8.6,.025),(10.1,.025),(10.8,0),(12,0)])
    brush.rotation_mode='QUATERNION'
    brush.matrix_world=Matrix.Translation((.39+x,-.22,.25+z)) @ Matrix.Rotation(tilt,4,'Y') @ base_rot @ Matrix.Diagonal((.01,.01,.01,1))
    if contact_tree:
        co,_,_,_=contact_tree.find_nearest(Vector((.30+x,-.39,.70+z*.85)))
        stroke_axis=Quaternion(normal,tilt)@axis;stroke_across=normal.cross(stroke_axis).normalized()
        shot_rot=Matrix((stroke_axis,stroke_across,normal)).transposed().to_4x4()
        brush.matrix_world=Matrix.Translation(co-stroke_axis*.56+normal*(.098+lift)) @ shot_rot @ Matrix.Diagonal((.007,.007,.007,1))
    for prop in ('location','rotation_quaternion','scale'):brush.keyframe_insert(prop,frame=frame)
    # Turn the shell toward the new camera while the contact solve preserves
    # the authored bristle plane, mitten grips and both sole anchors.
    body_q=Quaternion((1,0,0),-.045+z*.34) @ Quaternion((0,0,1),-.375+x*.45)
    rb=rest['root_x'].translation;body=oriented('root_x',rb+BODY_PLACEMENT_OFFSET_CM+Vector((-4+x*36,0,-4.0+z*7)),body_q)
    body_delta=body @ rest['root_x'].inverted();desired={'root_x':body}
    regrip=curve(t,[(0,0),(4.6,0),(5.15,1),(5.7,0),(12,0)])
    pleased_beat=curve(t,[(0,0),(7.95,0),(8.65,1),(9.65,1),(10.6,0),(12,0)])
    palms={s:100*(brush.matrix_world @ p) for s,p in [('r',Vector((9-2*regrip,0,0))),('l',Vector((31-4*regrip,0,0)))]}
    hand_across=stroke_across if contact_tree else across
    contact_rot={s:palm_offsets[s].rotation_difference(hand_across*(1 if s=='l' else -1)) for s in ('l','r')}
    contacts={s:palms[s]-(contact_rot[s]@palm_offsets[s]) for s in ('l','r')}
    for side in ('l','r'):
        # The shipped tooth has floating hand islands, not connected human arms.
        # Its shoulder weights belong to the tooth shell. Keep them on the body;
        # animate the separate hand chain with local offsets instead of folding enamel.
        desired['shoulder_'+side]=body_delta @ rest['shoulder_'+side]
        desired['hand_'+side]=oriented('hand_'+side,contacts[side],contact_rot[side])
        hand_delta=desired['hand_'+side] @ rest['hand_'+side].inverted()
        for chain_bone in ('arm_stretch_','arm_twist_','arm_twist_2_','forearm_stretch_','forearm_twist_','forearm_twist_2_'):
            desired[chain_bone+side]=hand_delta @ rest[chain_bone+side]
        limb(desired,('thigh_stretch_'+side,'leg_stretch_'+side,'foot_'+side),rest['foot_'+side].translation+BODY_PLACEMENT_OFFSET_CM+FOOT_SUPPORT_OFFSET_CM[side],Vector((.15 if side=='l' else -.15,-1,0)),body_delta)
        # Foot retains the original orientation at a fixed sole, while knees flex.
        desired['foot_'+side]=rest['foot_'+side].copy()
        desired['foot_'+side].translation+=BODY_PLACEMENT_OFFSET_CM+FOOT_SUPPORT_OFFSET_CM[side]
    for bone in rig.data.bones:
        pb=rig.pose.bones[bone.name]
        if bone.name in desired:
            parent=desired.get(bone.parent.name) if bone.parent else Matrix.Identity(4)
            if parent is None:parent=rig.pose.bones[bone.parent.name].matrix.copy()
            pb.matrix_basis=bone.convert_local_to_pose(desired[bone.name],bone.matrix_local,parent_matrix=parent,parent_matrix_local=bone.parent.matrix_local if bone.parent else Matrix.Identity(4),invert=True)
        else:pb.matrix_basis=Matrix.Identity(4)
    pitch=curve(t,[(0,0),(.85,-.035),(2.95,.02),(3.55,-.105),(4.55,-.105),(5.7,.01),(7.75,-.035),(8.65,-.08),(9.6,-.055),(10.2,0),(12,0)])
    yaw=curve(t,[(0,0),(3.15,0),(3.7,-.105),(4.55,-.105),(5.6,.015),(8.3,0),(8.9,.065),(9.6,.065),(10.6,0),(12,0)])
    rig.pose.bones['head_x'].rotation_quaternion=Quaternion((1,0,0),pitch) @ Quaternion((0,0,1),yaw)
    rig.pose.bones['neck_x'].rotation_quaternion=Quaternion((1,0,0),z*.1)
    blink=curve(t,[(0,0),(3.12,0),(3.20,1),(3.34,0),(5.3,0),(5.38,1),(5.52,0),(9.05,0),(9.13,1),(9.27,0),(12,0)])
    pleased=curve(t,[(0,.22),(3.2,.22),(3.65,.03),(5.4,.03),(7.9,.22),(8.65,.48),(9.7,.48),(10.6,.22),(12,.22)])
    effort=curve(t,[(0,.025),(3.2,.025),(3.65,.16),(4.8,.16),(5.6,.04),(6.4,.12),(7.7,.12),(8.25,0),(10.2,0),(11,.025),(12,.025)])
    soft_closed=curve(t,[(0,.46),(3.2,.46),(3.65,.50),(4.7,.50),(5.7,.46),(7.8,.46),(8.65,.32),(9.7,.32),(10.6,.46),(12,.46)])
    values={'Mouth_Smile':pleased,'Mouth_Effort':effort,'Eyes_Blink':blink,'Eyes_Scary':0.0,
      'Eyes_delight':curve(t,[(0,0),(3.3,0),(4,0),(5.6,0),(7.9,0),(8.65,.075),(9.7,.075),(10.6,0),(12,0)]),
      'Pupil_Dilate':curve(t,[(0,.30),(7.9,.30),(8.65,.48),(9.7,.48),(10.6,.30),(12,.30)]),'Pupil_Contract':0.0,
      'BlinkCancel_Mouth_Smile':pleased*blink,'BlinkCancel_Mouth_Effort':effort*blink,
      'Mouth_MenuSoftClosed':soft_closed,'Mouth_MenuApertureFollow':soft_closed}
    for name,value in values.items():
        acting[name].append([t,value])
        key=character.data.shape_keys.key_blocks[name];key.value=value
        key.keyframe_insert('value',frame=frame,group='Menu acting')
    for s in ('l','r'):
        rig.pose.bones['c_eyebrow_full_'+s].location.z=curve(t,[(0,0),(3.2,0),(3.7,.65 if s=='l' else .12),(4.55,.65 if s=='l' else .12),(5.6,0),(8.3,0),(8.8,.3),(9.8,.3),(10.6,0),(12,0)])
        # Actual mitten weights require only a few degrees, not human-hand curls.
        sign=-1 if s=='l' else 1
        for name,angle in [('index1_base_',2),('index1_',3),('index2_',2)]:
            pb=rig.pose.bones.get(name+s)
            if pb:pb.rotation_quaternion=Quaternion((0,0,1),math.radians(sign*angle*(1-.2*regrip)))
    for name,axis_,angle in [('thumb1_l',(1,0,0),-3),('thumb2_l',(0,0,1),2),('thumb1_r',(0,0,1),-3),('thumb2_r',(1,0,0),2)]:
        pb=rig.pose.bones.get(name)
        if pb:pb.rotation_quaternion=Quaternion(axis_,math.radians(angle))
    hand_heads,palm_targets,camera_acting=round_mitten_grips(regrip,pleased_beat,t)
    for pb in rig.pose.bones:
        if pb.name in previous and pb.rotation_quaternion.dot(previous[pb.name])<0:pb.rotation_quaternion.negate()
        previous[pb.name]=pb.rotation_quaternion.copy()
        for prop in ('location','rotation_quaternion','scale'):pb.keyframe_insert(prop,frame=frame,group=pb.name)
    bpy.context.view_layer.update()
    ev=rig.evaluated_get(bpy.context.evaluated_depsgraph_get())
    if frame in [1,21,42,64,84,101,121,161,201,241,289]:
        skin=character.evaluated_get(bpy.context.evaluated_depsgraph_get());data=skin.to_mesh()
        try:palms_now={s:100*sum((skin.matrix_world@data.vertices[i].co for i in ids),Vector())/len(ids) for s,ids in mitten_indices.items()}
        finally:skin.to_mesh_clear()
        report.append({'frame':frame,'camera_acting':camera_acting,'wrist_error_cm':{s:(ev.pose.bones['hand_'+s].head-hand_heads[s]).length for s in ('l','r')},
            'palm_centroid_error_cm':{s:(palms_now[s]-palm_targets[s]).length for s in ('l','r')},
            'reviewed_grip_wrist_shift_cm':{s:(hand_heads[s]-contacts[s]).length for s in ('l','r')},
            'foot_error_cm':{s:(ev.pose.bones['foot_'+s].head-rest['foot_'+s].translation-BODY_PLACEMENT_OFFSET_CM-FOOT_SUPPORT_OFFSET_CM[s]).length for s in ('l','r')}})
rig.animation_data.action.name='A_Menu_Cleaning_Loop'
brush.animation_data.action.name='Menu_Brush_Contact_Loop'
character.data.shape_keys.animation_data.action.name='Menu_Acting_Morphs'
for layer in character.data.shape_keys.animation_data.action.layers:
    for strip in layer.strips:
        for slot in character.data.shape_keys.animation_data.action.slots:
            bag=strip.channelbag(slot)
            if bag:
                for fc in bag.fcurves:
                    for key in fc.keyframe_points:key.interpolation='LINEAR'
scene.frame_set(1);bpy.context.view_layer.update()
(OUT/'Animation_ContactReport.json').write_text(json.dumps({'frames':[1,289],'fps':24,'duration':12,'samples':report,
    'menu_correctives':menu_shapes,'hand_scope':'Exclusive-mitten groups, an extra 20 degrees of camera tilt on lower r only, plus weighted c_eye_l/r; body/feet/brush contact solve retained.',
    'native_camera_cm':NATIVE_CAMERA_CM,'native_camera_target_cm':NATIVE_CAMERA_TARGET_CM,
    'body_placement_offset_cm':list(BODY_PLACEMENT_OFFSET_CM),'body_camera_yaw_radians':-.375,'body_knee_settle_cm':-4.0,
    'foot_support_offsets_cm':{s:list(v) for s,v in FOOT_SUPPORT_OFFSET_CM.items()},
    'validation_scope':'Numerical contacts only. Native eye/tool framing, enamel contact appearance and continuous acting still require visual QA.'},indent=2),encoding='utf8')
(OUT/'Exports').mkdir(exist_ok=True)
(OUT/'Exports/Menu_ActingCurves.json').write_text(json.dumps({'duration':12,'fps':24,'frames':[1,289],
    'required_morphs':list(acting),'curve_relation':'Mouth_MenuApertureFollow equals Mouth_MenuSoftClosed on every frame; Pupil_Contract is explicitly zero.',
    'curves':acting},indent=2),encoding='utf8')
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'MenuScene_Work.blend'))
result={'report':report,'source':bpy.data.filepath}
