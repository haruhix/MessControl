"""Seeded impact cracks, projected only onto surviving enamel.

Replaces straight sparse ribbons with irregular trunks, smaller forks and
hairline tips. The primary cluster is identical at both damage stages; deeper
damage removes intersected strokes and adds a secondary cluster.
"""
import bpy,math,random
from mathutils import Vector

def add_branch_cracks(obj,lo,hi,stage,seed,crack_material):
    size=hi-lo;vertices=[];faces=[];stats={'paths':0,'forks':0,'projected_quads':0}
    def project(p,axis,sign,sideaxis):
        start=(lo+hi)*.5
        start[axis]=hi[axis]+size[axis] if sign>0 else lo[axis]-size[axis]
        start[sideaxis]=p.x;start.z=p.y
        direction=Vector((0,0,0));direction[axis]=-sign
        hit,point,normal,index=obj.ray_cast(start,direction)
        if not hit or index<0 or obj.data.polygons[index].material_index!=0 or normal[axis]*sign<.28:return None
        return point+normal*(size.z*.0007)

    def ribbon(points,width,axis,sign,sideaxis,rng):
        stats['paths']+=1
        # A long chord can sink inside the curved crown. Subdivide before ray
        # projection, keeping thin strokes continuous across the enamel.
        dense=[]
        for a,b in zip(points,points[1:]):
            steps=max(1,math.ceil((b-a).length/(size.z*.009)))
            dense.extend(a.lerp(b,j/steps) for j in range(steps))
        points=dense+[points[-1]]
        pairs=[]
        for i,p in enumerate(points):
            direction=points[min(i+1,len(points)-1)]-points[max(i-1,0)]
            if direction.length<1e-8:pairs.append(None);continue
            direction.normalize();perp=Vector((-direction.y,direction.x))
            t=i/(len(points)-1)
            w=width*(.025+.975*(1-t)**.75)*rng.uniform(.82,1.16)
            a=project(p-perp*w,axis,sign,sideaxis);b=project(p+perp*w,axis,sign,sideaxis)
            pairs.append((a,b) if a is not None and b is not None and (a-b).length<w*8 else None)
        for a,b in zip(pairs,pairs[1:]):
            if a is None or b is None:continue
            quad=[a[0],a[1],b[1],b[0]]
            if max((quad[(i+1)%4]-quad[i]).length for i in range(4))>size.z*.065:continue
            index=len(vertices);vertices.extend(tuple(v) for v in quad);faces.append((index,index+1,index+2,index+3));stats['projected_quads']+=1

    def path(start,end,rng):
        points=[start.copy(),end.copy()]
        for level in range(4):
            split=[]
            for a,b in zip(points,points[1:]):
                delta=b-a;normal=Vector((-delta.y,delta.x)).normalized()
                mid=a.lerp(b,rng.uniform(.40,.60))+normal*delta.length*rng.uniform(-.16,.16)
                split.extend((a,mid))
            points=split+[points[-1]]
        return points

    def grow(start,angle,length,width,depth,axis,sign,sideaxis,rng):
        end=start+Vector((math.cos(angle),math.sin(angle)))*length
        # Keep the principal network on the readable crown, not the hidden root.
        end.x=max(lo[sideaxis]+size[sideaxis]*.045,min(hi[sideaxis]-size[sideaxis]*.045,end.x))
        end.y=max(lo.z+size.z*.13,min(hi.z-size.z*.055,end.y))
        if (end-start).length<size.z*.018:return
        points=path(start,end,rng);ribbon(points,width,axis,sign,sideaxis,rng)
        if depth==0:return
        for index in ([5,10] if depth==2 else [8]):
            if depth==1 and rng.random()<.42:continue
            tangent=points[index+1]-points[index-1];heading=math.atan2(tangent.y,tangent.x)
            sign_branch=rng.choice((-1,1));branch_angle=heading+sign_branch*rng.uniform(.55,1.18)
            stats['forks']+=1
            grow(points[index],branch_angle,length*rng.uniform(.23,.43),width*rng.uniform(.34,.55),depth-1,axis,sign,sideaxis,rng)

    for axis in (0,1):
        sideaxis=1-axis
        for sign in (-1,1):
            for cluster in range(1 if stage==1 else 2):
                rng=random.Random(seed+axis*71471+(sign+1)*13829+cluster*104729)
                u=rng.uniform(.38,.62) if cluster==0 else rng.uniform(.28,.72)
                v=rng.uniform(.56,.69) if cluster==0 else rng.uniform(.29,.42)
                center=Vector((lo[sideaxis]+u*size[sideaxis],lo.z+v*size.z))
                radius=min(size[sideaxis],size.z)*(.36 if cluster==0 else .21)
                arms=rng.randint(5,7) if cluster==0 else rng.randint(4,5)
                rotation=rng.uniform(0,math.tau)
                for arm in range(arms):
                    angle=rotation+math.tau*arm/arms+rng.uniform(-.21,.21)
                    grow(center,angle,radius*rng.uniform(.75,1.2),size.z*(.00145 if cluster==0 else .00085)*rng.uniform(.8,1.1),2,axis,sign,sideaxis,rng)
    data=bpy.data.meshes.new('MC_OwnedBranchCracks');data.from_pydata(vertices,[],faces);data.materials.append(crack_material);data.update()
    ribbons=bpy.data.objects.new(data.name,data);bpy.context.scene.collection.objects.link(ribbons)
    bpy.ops.object.select_all(action='DESELECT');obj.select_set(True);ribbons.select_set(True);bpy.context.view_layer.objects.active=obj
    bpy.ops.object.join()
    return stats
