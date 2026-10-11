"""Shared Blender 5 action utilities for the asymmetric caries boss previews.

Keep authored bind poses and custom properties when swapping actions. This
module has no dependency on the separate wizard animation authoring tools.
"""
import hashlib
from pathlib import Path
from types import SimpleNamespace

import bpy


def sha(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024*1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def curves(action):
    return [curve for layer in action.layers for strip in layer.strips
            for bag in strip.channelbags for curve in bag.fcurves]


def smooth(value):
    value = max(0., min(1., value))
    return value*value*(3-2*value)


def remap(frame, points):
    """Monotone cubic timing; plateaus remain holds without overshoot."""
    if frame <= points[0][0]:
        return float(points[0][1])
    if frame >= points[-1][0]:
        return float(points[-1][1])
    widths = [b[0]-a[0] for a,b in zip(points, points[1:])]
    slopes = [(b[1]-a[1])/width for a,b,width in zip(points, points[1:], widths)]
    tangents = [slopes[0]]
    for index in range(1, len(points)-1):
        a,b = slopes[index-1],slopes[index]
        if a*b <= 0:
            tangents.append(0.)
        else:
            w1,w2 = 2*widths[index]+widths[index-1],widths[index]+2*widths[index-1]
            tangents.append((w1+w2)/(w1/a+w2/b))
    tangents.append(slopes[-1])
    for index,(a,b) in enumerate(zip(points, points[1:])):
        if frame <= b[0]:
            width = widths[index]
            t = (frame-a[0])/width
            return ((2*t**3-3*t**2+1)*a[1]+(t**3-2*t**2+t)*width*tangents[index]
                    +(-2*t**3+3*t**2)*b[1]+(t**3-t**2)*width*tangents[index+1])


def defaults(rig):
    return {bone.name: dict(basis=bone.matrix_basis.copy(),
            properties={key:bone[key] for key in bone.keys()
                        if isinstance(bone[key], (bool,int,float))})
            for bone in rig.pose.bones}


def bind(rig, action, stored_defaults):
    animation = rig.animation_data_create()
    animation.action = None
    for bone in rig.pose.bones:
        bone.matrix_basis = stored_defaults[bone.name]['basis'].copy()
        for key,value in stored_defaults[bone.name]['properties'].items():
            bone[key] = value
    animation.action = action
    if action and action.slots:
        animation.action_slot = action.slots[0]
    animation.use_nla = False
    for track in animation.nla_tracks:
        track.mute = True
    rig.update_tag()
    bpy.context.view_layer.update()


def signature(action):
    return [(fc.data_path,fc.array_index,
             tuple((tuple(key.co),key.interpolation) for key in fc.keyframe_points))
            for fc in curves(action)]


def bake(rig, action, stored_defaults, records):
    bind(rig,action,stored_defaults)
    previous_euler,previous_quat = {},{}
    for frame,(pose,props) in enumerate(records,1):
        bpy.context.scene.frame_set(frame)
        for name,(loc,rotation,scale) in pose.items():
            bone = rig.pose.bones[name]
            bone.location,bone.scale = loc,scale
            if bone.rotation_mode == 'QUATERNION':
                if name in previous_quat and previous_quat[name].dot(rotation)<0:
                    rotation = -rotation
                bone.rotation_quaternion = rotation
                previous_quat[name] = rotation.copy()
                prop = 'rotation_quaternion'
            else:
                if bone.rotation_mode == 'AXIS_ANGLE':
                    raise ValueError('Unsupported axis-angle control: '+name)
                bone.rotation_euler = (rotation.to_euler(bone.rotation_mode,previous_euler[name])
                    if name in previous_euler else rotation.to_euler(bone.rotation_mode))
                previous_euler[name] = bone.rotation_euler.copy()
                prop = 'rotation_euler'
            for path in ('location',prop,'scale'):
                bone.keyframe_insert(data_path=path,frame=frame,group=name)
        for (name,prop),value in props.items():
            bone = rig.pose.bones[name]
            bone[prop] = value
            bone.keyframe_insert(data_path='["'+prop+'"]',frame=frame,group=name)
    for curve in curves(action):
        for key in curve.keyframe_points:
            key.interpolation = 'LINEAR'
        curve.update()


author = SimpleNamespace(sha=sha,curves=curves,smooth=smooth,remap=remap,bind=bind)
