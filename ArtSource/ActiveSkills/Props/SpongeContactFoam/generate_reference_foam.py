"""Generate Sponge contact banks and broken wake islands as two-slot OBJ meshes."""
from collections import Counter
from itertools import product
from math import ceil, cos, pi, sin, sqrt
from pathlib import Path
import json
import random

OUT = Path(__file__).resolve().parent
TETS = ((0,1,3,7),(0,1,5,7),(0,2,3,7),(0,2,6,7),(0,4,5,7),(0,4,6,7))

def sub(a,b):
    return tuple(x-y for x,y in zip(a,b))

def cross(a,b):
    return (a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0])

def dot(a,b):
    return sum(x*y for x,y in zip(a,b))

class Mesh:
    def __init__(self):
        self.points=[]
        self.faces=[]

    def vertex(self,p):
        self.points.append(p)
        return len(self.points)-1

    def face(self,a,b,c,outward,material=0):
        normal=cross(sub(self.points[b],self.points[a]),sub(self.points[c],self.points[a]))
        if dot(normal,outward)<0:
            b,c=c,b
        self.faces.append((a,b,c,material))

    def sphere(self,center,axes,material,segments=12,rings=8):
        x,y,z=center
        rx,ry,rz=axes
        top=self.vertex((x,y,z+rz))
        for row in range(1,rings):
            theta=pi*row/rings
            for col in range(segments):
                phi=2*pi*col/segments
                self.vertex((x+rx*sin(theta)*cos(phi),y+ry*sin(theta)*sin(phi),z+rz*cos(theta)))
        bottom=self.vertex((x,y,z-rz))
        for i in range(segments):
            a=top+1+i
            b=top+1+(i+1)%segments
            self.face(top,a,b,(0,0,1),material)
            for _ in range(rings-2):
                c,d=a+segments,b+segments
                direction=sub(self.points[a],center)
                self.face(a,c,b,direction,material)
                self.face(b,c,d,direction,material)
                a,b=c,d
            self.face(a,b,bottom,(0,0,-1),material)

    def density_body(self, lobes, low, high, step, floor, iso):
        nx,ny,nz=size=tuple(ceil((b-a)/step)+1 for a,b in zip(low,high))
        field=[0.0]*(nx*ny*nz)
        def index(i,j,k):
            return (k*ny+j)*nx+i
        def xyz(v):
            k,rem=divmod(v,nx*ny)
            j,i=divmod(rem,nx)
            return tuple(a+step*b for a,b in zip(low,(i,j,k)))
        for center,radii,weight in lobes:
            spans=[range(max(0,int((c-r-origin)//step)),min(n,ceil((c+r-origin)/step)+1)) for origin,c,r,n in zip(low,center,radii,size)]
            for i,j,k in product(*spans):
                d=sum(((origin+step*u-c)/r)**2 for origin,u,c,r in zip(low,(i,j,k),center,radii))
                if d<1:
                    field[index(i,j,k)]+=weight*(1-d)**2
        for k,j,i in product(range(nz),range(ny),range(nx)):
            v=index(i,j,k)
            field[v]=min(field[v]-iso,(low[2]+step*k-floor)*.65)
        cuts={}
        def intersection(a,b):
            pair=tuple(sorted((a,b)))
            if pair not in cuts:
                p,q=xyz(a),xyz(b)
                t=field[a]/(field[a]-field[b])
                cuts[pair]=self.vertex(tuple(x+t*(y-x) for x,y in zip(p,q)))
            return cuts[pair]
        for k,j,i in product(range(nz-1),range(ny-1),range(nx-1)):
            v=[index(i+(c&1),j+((c>>1)&1),k+(c>>2)) for c in range(8)]
            inside=[field[p]>0 for p in v]
            if all(inside) or not any(inside):
                continue
            for tet in TETS:
                solid=[v[n] for n in tet if inside[n]]
                air=[v[n] for n in tet if not inside[n]]
                if not solid or not air:
                    continue
                out=sub(xyz(air[0]),xyz(solid[0]))
                if len(solid)==1:
                    self.face(*(intersection(solid[0],n) for n in air),out)
                elif len(air)==1:
                    self.face(*(intersection(n,air[0]) for n in solid),out)
                else:
                    a,b,c,d=(intersection(n,m) for n in solid for m in air)
                    self.face(a,b,d,out)
                    self.face(a,d,c,out)

    def bubble_candidates(self, test):
        points=[]
        for a,b,c,_ in self.faces:
            p,q,r=[self.points[ix] for ix in (a,b,c)]
            avg=tuple((x+y+z)/3 for x,y,z in zip(p,q,r))
            n=cross(sub(q,p),sub(r,p))
            if n[2]>.46*sqrt(dot(n,n)) and test(*avg):
                points.append(avg)
        return points

    def export(self, name, height):
        bounds=[[fn(p[d] for p in self.points) for d in range(3)] for fn in (min,max)]
        edges=Counter(tuple(sorted((f[i],f[(i+1)%3]))) for f in self.faces for i in range(3))
        assert set(edges.values())=={2},Counter(edges.values())
        normals=[[0,0,0] for _ in self.points]
        for a,b,c,_ in self.faces:
            n=cross(sub(self.points[b],self.points[a]),sub(self.points[c],self.points[a]))
            for ix in (a,b,c):
                for d in range(3):
                    normals[ix][d]+=n[d]
        assert min(dot(n,n) for n in normals)>1e-16
        with (OUT/(name+'.obj')).open('w',encoding='ascii') as f:
            f.write(f'# Original MessControl sponge suds, cm, +X travel, +Z up\nmtllib {name}.mtl\no {name}\ns 1\n')
            for x,y,z in self.points:
                f.write(f'v {x:.6f} {y:.6f} {z:.6f}\n')
            for x,y,z in self.points:
                f.write(f'vt {0.5+x/220+y*.0012:.6f} {0.04+0.92*max(0,min(1,(z-bounds[0][2])/height)):.6f}\n')
            for n in normals:
                length=sqrt(dot(n,n))
                f.write('vn '+' '.join(f'{v/length:.7f}' for v in n)+'\n')
            for mi,mat in enumerate(('FoamCream','BubbleFilm')):
                f.write(f'usemtl {mat}\n')
                for face in self.faces:
                    if face[3]==mi:
                        f.write('f '+' '.join(f'{ix+1}/{ix+1}/{ix+1}' for ix in face[:3])+'\n')
        (OUT/(name+'.mtl')).write_text('newmtl FoamCream\nKd .94 .93 .87\nKs .54 .54 .54\nNs 90\nd 1\nillum 2\nnewmtl BubbleFilm\nKd .79 .93 .91\nKs .9 .9 .9\nNs 120\nd .29\nillum 2\n',encoding='ascii')
        return dict(name=name,bounds_cm=bounds,triangles=len(self.faces),vertices=len(self.points),material_triangle_counts=dict(Counter(('FoamCream','BubbleFilm')[f[3]] for f in self.faces)),watertight=True)

def add_bubbles(mesh,candidates,counts,rng,floor,max_height):
    # Large clear bubbles are selected first, followed by small shared crests.
    rng.shuffle(candidates)
    centers=[]
    for count,r_lo,r_hi,material in counts:
        created=0
        for x,y,z in candidates:
            if created>=count:
                break
            r=rng.uniform(r_lo,r_hi)
            if z+r*.69>max_height or z-r*1.4<floor+.2:
                continue
            if any((x-a)**2+(y-b)**2<(r+q)**2*.70 for a,b,q in centers):
                continue
            centers.append((x,y,r))
            mesh.sphere((x,y,z-r*.32),(r,r*rng.uniform(.87,1.08),r*rng.uniform(.8,1.02)),material,12 if r>3.5 else 8,8 if r>3.5 else 6)
            created+=1
    return len(centers)

def contact():
    rng=random.Random(40924)
    lobes=[]
    # Thin low bridges remain mostly hidden under the actual board's edge.
    for sign in (-1,1):
        for x in range(-75,77,13):
            lobes.append(((x,sign*(38+rng.uniform(-1.3,1.5)),.55),(13,8.7,5.9),.63))
        for y in range(-36,37,12):
            lobes.append(((sign*78,y,.25),(9.4,13.1,5.0),.62))
    # Individual masses gathered into distinct pockets along the sides. Large
    # outer banks occupy alternating corners, with quiet portions in between.
    for sign, pockets in (
        (-1,((-71,15),(-54,12),(-37,9),(-14,8),(12,8),(35,11),(53,17),(71,20))),
        (1,((-73,20),(-56,15),(-39,12),(-19,10),(1,8),(19,11),(38,13),(59,13),(74,10))),
    ):
        for x,h in pockets:
            y=sign*(43.5+rng.uniform(-1.5,4.2))
            lobes.append(((x,y,2.1+h*.21),(rng.uniform(13,17),rng.uniform(12,17),h),rng.uniform(.78,1.20)))
            if h>=11:
                lobes.append(((x+rng.uniform(-6,7),y+sign*rng.uniform(5,8),2.0),(rng.uniform(8,14),rng.uniform(8,12),rng.uniform(6,9)),rng.uniform(.75,1.03)))
    for sign in (-1,1):
        for y,h in ((-35,13),(-17,9),(4,10),(23,13),(37,14)):
            x=sign*(80+rng.uniform(-1.5,4))
            lobes.append(((x,y+rng.uniform(-2,2),2.4),(rng.uniform(10,15),rng.uniform(13,18),h),rng.uniform(.78,1.10)))
    mesh=Mesh()
    mesh.density_body(lobes,(-112,-76,-6),(112,76,33),3.1,-2,.51)
    c=mesh.bubble_candidates(lambda x,y,z: z>4.5 and (abs(x)>78.5 or abs(y)>43.5))
    count=add_bubbles(mesh,c,((11,4.2,7.2,1),(23,1.7,4.1,1),(22,1.4,3.4,0)),rng,-2,29)
    qa=mesh.export('SM_SpongeContactFoam',25)
    qa['embedded_bubbles']=count
    assert all(abs(x)>65 or abs(y)>27 for x,y,z in mesh.points)
    return qa

def wake():
    mesh=Mesh()
    rng=random.Random(503)
    # A crooked tapered island, with three bulbous opaque crowns and a lacy tail.
    lobes=(
        ((-25,-1.5,.8),(10,5.9,3.8),.82),((-16,1.8,1.3),(16,10,8.6),1.0),
        ((-3,-1.8,1.35),(16,12,10.5),1.01),((9,2.8,1.1),(15,9.1,8.0),.91),
        ((21,1.5,.95),(12,6.9,5.8),.90),((28,3.0,.55),(7.9,6.3,4.2),.73),
        ((-5,-7.3,.65),(6.2,6.9,5.1),.84),((12,7.8,.55),(7.6,5.7,4.3),.78),
    )
    mesh.density_body(lobes,(-41,-21,-3.2),(41,21,16),3.7,0,.46)
    c=mesh.bubble_candidates(lambda x,y,z: z>2.3)
    count=add_bubbles(mesh,c,((4,1.75,3.4,1),(1,1.1,2.15,0)),rng,0,12)
    mesh.points=[(x,y,z*1.12) for x,y,z in mesh.points]
    qa=mesh.export('SM_SpongeWakeFoam',10)
    qa['embedded_bubbles']=count
    return qa

if __name__=='__main__':
    result=[contact(),wake()]
    (OUT/'FoamReferenceMeshQA.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf8')
    print(json.dumps(result,indent=2))
