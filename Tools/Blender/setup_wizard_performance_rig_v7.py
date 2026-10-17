"""Callable V7 rig setup. Importing this module has no scene/file side effects.

The caller owns the working rig/mesh copies, action backup, saving, and visual QA.
No original bone is reparented. All original rest matrices are guarded, as are
native deform flags and nearest-deform ancestors used by the ARP export path.
"""
import bpy
from collections import Counter, defaultdict


def _rest_signature(rig):
    return {b.name: dict(parent=b.parent.name if b.parent else None,
                        deform=b.use_deform, matrix=[list(r) for r in b.matrix_local],
                        length=b.length) for b in rig.data.bones}


def _nearest_deform_ancestor(bone):
    parent=bone.parent
    while parent and not parent.use_deform:
        parent=parent.parent
    return parent.name if parent else None


def _weight_summary(mesh, deform_names, vertex_ids=None):
    selected=set(vertex_ids) if vertex_ids is not None else None
    group_names={g.index:g.name for g in mesh.vertex_groups}
    sums=defaultdict(float);counts=Counter();totals=[]
    for vertex in mesh.data.vertices:
        if selected is not None and vertex.index not in selected:
            continue
        total=0.0
        for g in vertex.groups:
            name=group_names[g.group]
            if name in deform_names and g.weight>0:
                sums[name]+=g.weight;counts[name]+=1;total+=g.weight
        totals.append(total)
    return dict(vertices=len(totals), sum_range=[min(totals),max(totals)] if totals else [],
                groups={name:dict(sum=amount,vertices=counts[name]) for name,amount in sums.items()})


def _body_vertex_ids(mesh):
    """Recognize the seven inspected complete islands, never split by height."""
    indices={g.name:g.index for g in mesh.vertex_groups}
    if 'root.x' not in indices or 'head.x' not in indices:
        raise ValueError('Expected original root.x and head.x vertex groups')
    root_index,head_index=indices['root.x'],indices['head.x']
    selected=set()
    for vertex in mesh.data.vertices:
        weights={g.group:g.weight for g in vertex.groups}
        if weights.get(root_index,0)>.99 and weights.get(head_index,0)>.99:
            selected.add(vertex.index)
    adjacency=[[] for _ in mesh.data.vertices]
    for edge in mesh.data.edges:
        a,b=edge.vertices;adjacency[a].append(b);adjacency[b].append(a)
    pending=set(range(len(mesh.data.vertices)));islands=[]
    while pending:
        start=pending.pop();stack=[start];island={start}
        while stack:
            for neighbor in adjacency[stack.pop()]:
                if neighbor in pending:
                    pending.remove(neighbor);island.add(neighbor);stack.append(neighbor)
        if island & selected:
            if not island <= selected:
                raise ValueError('Root/head overlap cuts a connected island; refusing arbitrary split')
            islands.append(island)
    sizes=sorted(map(len,islands))
    if len(mesh.data.vertices)!=2361 or sizes!=[52,52,170,170,332,332,343] or len(selected)!=1451:
        raise ValueError(f'Unexpected wizard topology/weights: vertices={len(mesh.data.vertices)}, islands={sizes}')
    return sorted(selected),sizes


def _driver_shape_visibility(rig,bone_name,property_bone,expression):
    bone=rig.pose.bones.get(bone_name)
    if not bone or not bone.custom_shape:
        return
    for axis in range(3):
        try:
            bone.driver_remove('custom_shape_scale_xyz',axis)
        except (TypeError,RuntimeError):
            pass
        fcurve=bone.driver_add('custom_shape_scale_xyz',axis)
        driver=fcurve.driver;driver.type='SCRIPTED';driver.expression=expression
        while driver.variables:
            driver.variables.remove(driver.variables[0])
        var=driver.variables.new();var.name='switch';var.type='SINGLE_PROP'
        var.targets[0].id=rig
        var.targets[0].data_path=f'pose.bones["{property_bone}"]["ik_fk_switch"]'


def apply_rig_v7(rig,mesh):
    """Modify only the supplied working datablocks; return detailed QA evidence.

    c_root.x is pelvis/support; c_head.x is the complete walnut+face mass.
    Author c_shoulder_shell.side + c_arm_shell_fk.side, then the existing
    c_forearm_fk.side, c_hand_fk.side and digit controls. Set hand IK switch=1.
    Old shoulder/upperarm controls are driven sockets, preserving ARP ancestry.
    This deliberately changes shell behavior; previous actions require reposing.
    """
    if rig.type!='ARMATURE' or mesh.type!='MESH':
        raise TypeError('Expected armature rig and skinned mesh')
    if mesh.data.users!=1 or rig.data.users!=1:
        raise ValueError('Working rig and mesh datablocks must be single-user copies')
    if rig.get('MC_PerformanceRigV7'):
        raise ValueError('V7 setup was already applied to this rig; refusing duplicate constraints')
    before=_rest_signature(rig)
    deforms={name for name,b in before.items() if b['deform']}
    if len(deforms)!=49:
        raise ValueError(f'Expected 49 native deform bones, got {len(deforms)}')
    ancestors={b.name:_nearest_deform_ancestor(b) for b in rig.data.bones if b.use_deform}
    body_ids,island_sizes=_body_vertex_ids(mesh)
    report=dict(body_vertex_indices=body_ids,body_island_sizes=island_sizes,
                weights_before=_weight_summary(mesh,deforms),
                body_weights_before=_weight_summary(mesh,deforms,body_ids),
                control_parents_before={n:before[n]['parent'] for n in
                    ['c_shoulder.l','c_shoulder.r','c_arm_fk.l','c_arm_fk.r']})

    body_set=set(body_ids)
    for name in deforms:
        group=mesh.vertex_groups.get(name)
        if group:
            group.remove(body_ids)
    mesh.vertex_groups['head.x'].add(body_ids,1.0,'REPLACE')
    group_objects={g.index:g for g in mesh.vertex_groups}
    replacements=[]
    for vertex in mesh.data.vertices:
        if vertex.index in body_set:
            continue
        values=[(g.group,g.weight) for g in vertex.groups if group_objects[g.group].name in deforms]
        total=sum(weight for _,weight in values)
        if total<=0:
            raise ValueError(f'Unweighted limb vertex {vertex.index}')
        replacements.extend((group_index,vertex.index,weight/total) for group_index,weight in values)
    for group_index,vertex_index,weight in replacements:
        group_objects[group_index].add([vertex_index],weight,'REPLACE')
    mesh.data.update();mesh.update_tag()

    previous_active=bpy.context.view_layer.objects.active
    previous_selected=list(bpy.context.selected_objects)
    previous_mode=previous_active.mode if previous_active else 'OBJECT'
    if bpy.context.object and bpy.context.object.mode!='OBJECT':
        bpy.ops.object.mode_set(mode='OBJECT')
    bpy.ops.object.select_all(action='DESELECT');rig.select_set(True)
    bpy.context.view_layer.objects.active=rig
    bpy.ops.object.mode_set(mode='EDIT')
    new_names=[]
    for side in ('l','r'):
        for source_name,new_name in ((f'c_shoulder.{side}',f'c_shoulder_shell.{side}'),
                                     (f'c_arm_fk.{side}',f'c_arm_shell_fk.{side}')):
            original=rig.data.edit_bones[source_name]
            original_matrix=original.matrix.copy();original_length=original.length
            original_head=original.head.copy();original_tail=original.tail.copy();original_roll=original.roll
            bone=rig.data.edit_bones.new(new_name)
            bone.parent=rig.data.edit_bones['head.x'];bone.use_connect=False
            bone.head=original_head;bone.tail=original_tail;bone.roll=original_roll
            bone.matrix=original_matrix;bone.length=original_length
            bone.use_deform=False;bone.inherit_scale='NONE'
            new_names.append(new_name)
    bpy.ops.object.mode_set(mode='OBJECT')

    for side in ('l','r'):
        shoulder=rig.pose.bones[f'c_shoulder_shell.{side}']
        arm=rig.pose.bones[f'c_arm_shell_fk.{side}']
        for target,source in ((shoulder,rig.pose.bones[f'c_shoulder.{side}']),
                              (arm,rig.pose.bones[f'c_arm_fk.{side}'])):
            target.rotation_mode=source.rotation_mode
            target.custom_shape=source.custom_shape
            target.custom_shape_scale_xyz=tuple(source.custom_shape_scale_xyz)
            target.custom_shape_translation=tuple(source.custom_shape_translation)
            target.custom_shape_rotation_euler=tuple(source.custom_shape_rotation_euler)
            target.use_custom_shape_bone_size=source.use_custom_shape_bone_size
            target.lock_scale=(True,True,True)
            target.bone.hide=False;target.bone.hide_select=False
        arm.lock_location=(True,True,True)
        attachment=arm.constraints.new('COPY_LOCATION');attachment.name='MC_V7_ShoulderTail'
        attachment.target=rig;attachment.subtarget=f'c_shoulder.{side}';attachment.head_tail=1.0
        attachment.owner_space='WORLD';attachment.target_space='WORLD'
        for old_name,new_name in ((f'c_shoulder.{side}',shoulder.name),
                                   (f'c_arm_fk.{side}',arm.name)):
            constraint=rig.pose.bones[old_name].constraints.new('COPY_TRANSFORMS')
            constraint.name='MC_V7_WalnutSpace';constraint.target=rig;constraint.subtarget=new_name
            constraint.owner_space='WORLD';constraint.target_space='WORLD';constraint.mix_mode='REPLACE'

    # A single rigid mass deliberately receives scaling; limb sockets inherit
    # position/rotation but not scale. Native deform bones are left untouched.
    rig.pose.bones['c_head.x']['MC_role']='Walnut kernel, shell halves, eyes and brows'
    rig.pose.bones['c_root.x']['MC_role']='Pelvis and leg support'
    rig.pose.bones['c_head.x'].lock_scale=(False,False,False)

    visible={
        'MC V7 - Body':['c_pos','c_traj','c_root_master.x','c_root.x','c_neck.x','c_head.x'],
        'MC V7 - Arms FK':[f'{prefix}.{side}' for side in ('l','r') for prefix in
                           ('c_shoulder_shell','c_arm_shell_fk','c_forearm_fk')],
        'MC V7 - Arms IK':[f'{prefix}.{side}' for side in ('l','r') for prefix in
                           ('c_hand_ik','c_arm_ik','c_arms_pole','c_stretch_arm')],
        'MC V7 - Hands':[f'c_hand_fk.{side}' for side in ('l','r')],
        'MC V7 - Fingers':[f'c_{digit}{joint}.{side}' for side in ('l','r')
                           for digit in ('pinky','ring','thumb') for joint in ('1_base','1','2','3')],
        'MC V7 - Feet':[f'{prefix}.{side}' for side in ('l','r') for prefix in
                       ('c_thigh_b','c_thigh_fk','c_leg_fk','c_foot_fk','c_toes_fk',
                        'c_thigh_ik','c_foot_ik','c_toes_ik','c_leg_pole','c_foot_01','c_foot_roll_cursor')]
    }
    for collection in rig.data.collections_all:
        collection.is_visible=False
    for name,names in visible.items():
        collection=rig.data.collections.get(name) or rig.data.collections.new(name)
        collection.is_visible=True
        for bone_name in names:
            bone=rig.data.bones.get(bone_name)
            if not bone:
                continue
            for old_collection in list(bone.collections):
                old_collection.unassign(bone)
            collection.assign(bone);bone.hide=False;bone.hide_select=False
    for side in ('l','r'):
        hand_switch=f'c_hand_ik.{side}'
        foot_switch=f'c_foot_ik.{side}'
        for prefix in ('c_arm_shell_fk','c_forearm_fk','c_hand_fk'):
            _driver_shape_visibility(rig,f'{prefix}.{side}',hand_switch,'switch')
        for prefix in ('c_hand_ik','c_arm_ik','c_arms_pole'):
            _driver_shape_visibility(rig,f'{prefix}.{side}',hand_switch,'1-switch')
        for prefix in ('c_thigh_fk','c_leg_fk','c_foot_fk','c_toes_fk'):
            _driver_shape_visibility(rig,f'{prefix}.{side}',foot_switch,'switch')
        for prefix in ('c_thigh_ik','c_foot_ik','c_toes_ik','c_leg_pole','c_foot_01','c_foot_roll_cursor'):
            _driver_shape_visibility(rig,f'{prefix}.{side}',foot_switch,'1-switch')

    rig.update_tag();bpy.context.view_layer.update()
    after=_rest_signature(rig)
    errors=[]
    for name,original in before.items():
        current=after[name]
        if current['parent']!=original['parent'] or current['deform']!=original['deform']:
            errors.append(f'{name}: parent/deform changed')
        delta=max(abs(a-b) for ar,br in zip(original['matrix'],current['matrix']) for a,b in zip(ar,br))
        if delta>1e-6:
            errors.append(f'{name}: rest matrix changed by {delta}')
    for name,parent in ancestors.items():
        if _nearest_deform_ancestor(rig.data.bones[name])!=parent:
            errors.append(f'{name}: nearest deform ancestor changed')
    if len([b for b in rig.data.bones if b.use_deform])!=49:
        errors.append('Native deform count changed')
    for side in ('l','r'):
        for source,new in ((f'c_shoulder.{side}',f'c_shoulder_shell.{side}'),
                            (f'c_arm_fk.{side}',f'c_arm_shell_fk.{side}')):
            delta=max(abs(a-b) for ar,br in zip(before[source]['matrix'],after[new]['matrix'])
                      for a,b in zip(ar,br))
            if delta>1e-6:
                errors.append(f'{new}: copied control rest differs by {delta}')
    if errors:
        raise RuntimeError('V7 preservation checks failed: '+ '; '.join(errors))
    rig['MC_PerformanceRigV7']=True
    report.update(weights_after=_weight_summary(mesh,deforms),
                  body_weights_after=_weight_summary(mesh,deforms,body_ids),
                  control_parents_after={name:rig.data.bones[name].parent.name for name in
                      ['c_shoulder.l','c_shoulder.r','c_arm_fk.l','c_arm_fk.r']+new_names},
                  new_controls=new_names,production_deform_count=49,
                  all_original_rest_matrices_preserved=True,all_original_parents_preserved=True,
                  native_export_ancestors_preserved=True,
                  head_pivot=list(rig.data.bones['c_head.x'].head_local),
                  head_axes={axis:list(rig.data.bones['c_head.x'].matrix_local.to_3x3().col[i])
                             for i,axis in enumerate('XYZ')},
                  author_controls=dict(pelvis='c_root.x',walnut_mass='c_head.x',
                      shoulders=[f'c_shoulder_shell.{s}' for s in ('l','r')],
                      upperarms=[f'c_arm_shell_fk.{s}' for s in ('l','r')],
                      elbows=[f'c_forearm_fk.{s}' for s in ('l','r')]))
    bpy.ops.object.select_all(action='DESELECT')
    for obj in previous_selected:
        obj.select_set(True)
    bpy.context.view_layer.objects.active=previous_active
    if previous_active and previous_mode!='OBJECT':
        bpy.ops.object.mode_set(mode=previous_mode)
    return report
