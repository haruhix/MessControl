"""r02 organic cavity/tongue geometry proof from the open reference direction.

The rejected V03 bathtub/flat-front forms are not carried forward. Dimensions
are an inferred 3D solution to the single image. Native pivots/units and the
existing character sole contact are retained. No final FBX/Painter release.
"""
import bpy,bmesh,numpy as np,json,math,hashlib,sys,argparse
from pathlib import Path
from mathutils import Vector,Matrix
from mathutils.bvhtree import BVHTree
assert bpy.app.background
ap=argparse.ArgumentParser();ap.add_argument('--candidate',default='Organic_r05');ap.add_argument('--mask-half',type=float,default=3.440874);ap.add_argument('--interior-mask-half',type=float,default=2.580656);ap.add_argument('--mask-center',type=float,default=0.);opt=ap.parse_args(sys.argv[sys.argv.index('--')+1:]if'--'in sys.argv else[])
BASE=Path('E:/DEVGAME/MessControl/ArtSource/UI/MainMenu3D/20261015_refB_v1');OUT=BASE/'Assembly/UnrealHandoff/v08_ReferenceFinish/Interior'/opt.candidate;OUT.mkdir(parents=True,exist_ok=True);OUTER=BASE/'Assembly/UnrealHandoff/v08b_SeamUV/Candidates/Final96_GumMinStretch_LocalFangRepair/MC_DAY03_UnrealMorphSource.blend'
bpy.ops.wm.read_factory_settings(use_empty=True)
def material(name,color):
 m=bpy.data.materials.new(name);m.use_nodes=True;m.use_backface_culling=True;bs=m.node_tree.nodes.get('Principled BSDF');bs.inputs['Base Color'].default_value=(*color,1);bs.inputs['Roughness'].default_value=.48;return m
mi=material('MC_Interior',(.22,.27,.29));mt=material('MC_Tongue',(.44,.47,.48))
def mesh_obj(name,co,faces,uvfaces,mat):
 me=bpy.data.meshes.new(name+'_Surface');me.from_pydata(co,[],faces);me.materials.append(mat);me.update();uv=me.uv_layers.new(name='UV0')
 for f,values in zip(me.polygons,uvfaces):
  for li,val in zip(f.loop_indices,values):uv.data[li].uv=val
 bm=bmesh.new();bm.from_mesh(me);bmesh.ops.recalc_face_normals(bm,faces=bm.faces);bm.to_mesh(me);bm.free();me.update()
 for f in me.polygons:f.use_smooth=True
 ob=bpy.data.objects.new(name,me);bpy.context.scene.collection.objects.link(ob);return ob

# Elliptical bowl, not a rounded-rectangle extrusion. The concave inner wall
# faces native +Y and thickens into a closed back return. Cheek volume and
# palate folds are integrated into this one continuous surface.
N=128;R=64;verts=[];faces=[];uvs=[]
def cavity(r,t,back=False):
 x=9.35*r*math.cos(t);z=.02+6.3*r*math.sin(t)
 y=-3.2-7.8*max(0,1-r*r)**.62
 y-=2.8*math.exp(-(x/2.7)**2-((z+.35)/2.35)**2)
 y+=1.8*math.exp(-((abs(x)-7.25)/1.05)**2-((z+.1)/2.2)**2)*(1-r**6)
 # Fleshy lateral columns curve inward below the palate, with continuous
 # rounded cross-sections and deep recess behind them.
 arch_x=5.35+.115*(z+.3)**2
 y+=2.35*math.exp(-((abs(x)-arch_x)/1.12)**2-((z+.1)/2.7)**4)*(1-r**8)
 # The ceiling overhangs the deeper center. These are genuine depth changes;
 # diagnostic albedo remains constant throughout the cavity.
 y+=1.75*math.exp(-(x/4.7)**4-((z-2.75)/.75)**2)
 for zz,amp in [(1.85,.065),(2.25,.08),(2.65,.075),(3.05,.055)]:y+=amp*math.exp(-((z-(zz-.024*x*x))/.145)**2-(x/4.6)**6)
 # Lower floor slopes away behind the independent tongue.
 y+=.45*math.exp(-(x/5.0)**4-((z+2.65)/.95)**2)
 if back:x*=1.007;z=.02+(z-.02)*1.007;y-=.25
 return(x,y,z)
for back in (False,True):
 verts.append(cavity(0,0,back))
 for ir in range(1,R+1):
  for j in range(N):verts.append(cavity(ir/R,2*math.pi*j/N,back))
layer=1+N*R
for back in (False,True):
 off=layer*int(back);center=(.5,.49 if not back else .925);radius=.465 if not back else .054
 def uv_for(idx):
  if idx==off:return center
  rr=(idx-off-1)//N+1;j=(idx-off-1)%N;return(center[0]+radius*rr/R*math.cos(2*math.pi*j/N),center[1]+radius*rr/R*math.sin(2*math.pi*j/N))
 for j in range(N):
  ids=(off,off+1+(j+1)%N,off+1+j)
  if back:ids=tuple(reversed(ids))
  faces.append(ids);uvs.append([uv_for(v)for v in ids])
 for ir in range(R-1):
  for j in range(N):
   a=off+1+ir*N+j;b=off+1+ir*N+(j+1)%N;ids=(a,b,b+N,a+N)
   if back:ids=tuple(reversed(ids))
   faces.append(ids);uvs.append([uv_for(v)for v in ids])
for j in range(N):
 a=1+(R-1)*N+j;b=1+(R-1)*N+(j+1)%N;faces.append((a,a+layer,b+layer,b));uvs.append([(.035+.93*j/N,.982),(.035+.93*j/N,.992),(.035+.93*(j+1)/N,.992),(.035+.93*(j+1)/N,.982)])
interior=mesh_obj('SM_DAY03_Interior',verts,faces,uvs,mi)

# True closed spheroidal tongue. The front is a recessed oval roll behind the
# lower crowns; no vertical plane connects the dorsal and ventral surfaces.
# Broad bilateral lobes and the saddle are geometry, not painted shading.
N=128;L=72;verts=[];faces=[];uvs=[]
def tongue(phi,t):
 cp=math.cos(phi);sp=math.sin(phi);x=8.05*cp*math.cos(t);y=-2.85-2.20*cp*math.sin(t);z=-3.43+(1.23 if sp>=0 else 1.32)*sp
 if sp>0:
  # A broad raised peripheral dorsum defines one continuous tongue body. This
  # smooth plateau has much lower peak curvature than two isolated Gaussians.
  lobes=(1-math.exp(-(x/2.25)**2))*math.exp(-(x/7.4)**8)
  rear=.65+.35/(1+math.exp((y+2.0)/.6));z+=1.95*lobes*sp**.85*rear;z-=.13*math.exp(-(x/.66)**2)*sp
 return(x,y,z)
verts.append(tongue(-math.pi/2,0))
for il in range(1,L):
 for j in range(N):verts.append(tongue(-math.pi/2+math.pi*il/L,2*math.pi*j/N))
top=len(verts);verts.append(tongue(math.pi/2,0))
def tuv(idx,upper):
 cx=.5;cy=.67 if upper else.18;radius=.30 if upper else.135
 if idx in(0,top):return(cx,cy)
 il=(idx-1)//N+1;j=(idx-1)%N;rr=math.cos(-math.pi/2+math.pi*il/L);t=2*math.pi*j/N
 return(cx+radius*rr*math.cos(t),cy+radius*rr*math.sin(t))
for j in range(N):
 ids=(0,1+j,1+(j+1)%N);faces.append(ids);uvs.append([tuv(v,False)for v in ids])
for il in range(L-2):
 upper=il+1>=L//2
 for j in range(N):
  a=1+il*N+j;b=1+il*N+(j+1)%N;ids=(a,a+N,b+N,b);faces.append(ids);uvs.append([tuv(v,upper)for v in ids])
for j in range(N):
 ids=(top,1+(L-2)*N+(j+1)%N,1+(L-2)*N+j);faces.append(ids);uvs.append([tuv(v,True)for v in ids])
coords=np.array(verts,np.float64);anchor=np.array([-5.757009,-2.887758,-2.076843]);patch=np.exp(-((abs(coords[:,0])-abs(anchor[0]))/.85)**2-((coords[:,1]-anchor[1])/.75)**2);uppermask=coords[:,2]>-3.43
contact=[]
for i in range(3):
 tree=BVHTree.FromPolygons(coords.tolist(),faces,all_triangles=False);hit,normal,index,distance=tree.ray_cast(Vector((anchor[0],anchor[1],5)),Vector((0,0,-1)))
 assert hit is not None;delta=anchor[2]-hit.z;coords[uppermask,2]+=delta*patch[uppermask];contact.append({'iteration':i,'ray_hit_before_z':hit.z,'delta':float(delta)})
tongueob=mesh_obj('SM_DAY03_Tongue',coords.tolist(),faces,uvs,mt)
objects=[interior,tongueob]
audits=[]
for ob in objects:
 bm=bmesh.new();bm.from_mesh(ob.data);assert all(e.is_manifold for e in bm.edges);volume=bm.calc_volume(signed=True);assert volume>0;bm.free();co=np.array([v.co[:]for v in ob.data.vertices]);audits.append({'mesh':ob.name,'vertices':len(co),'triangles':sum(len(p.vertices)-2 for p in ob.data.polygons),'manifold_closed':True,'positive_signed_volume':volume,'bounds_cm':[(co.min(0)*100).tolist(),(co.max(0)*100).tolist()]})
bpy.context.scene.unit_settings.system='NONE';bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'MC_DAY03_InteriorSource.blend'))
report={'status':opt.candidate+' organic shape proof only; noFBX/texture release','source':str(OUT/'MC_DAY03_InteriorSource.blend'),'source_sha256':hashlib.sha256((OUT/'MC_DAY03_InteriorSource.blend').read_bytes()).hexdigest(),'reference_policy':'OpenDAY03 reference direction + leadexplicitorganicrepair; geometrydepthinferred. RejectedV03tubnotpreserved.','origin':'Identitynative+Yfacing; sourceunitsmeters, FBXwillcmmetadata noextra100','shape':'Deep concavecentralfunnel, integratedroundedbuccalarches/overhangingpalate/softfolds. Closedspheroidalbilateraltongue withraisedrearlobes/centralvalley/recessedanteriorarc. Noportal/uvula.','parts':audits,'character_contact_anchor':anchor.tolist(),'contact_iterations':contact,'UV_status':'Logicalcontinuoussurface charts provisional, measuredminimumstretchsolve aftershapeapproval'};(OUT/'interior_handoff.json').write_text(json.dumps(report,indent=2))
with bpy.data.libraries.load(str(OUTER),link=False)as(src,dst):dst.objects=['SK_DAY03']
outer=dst.objects[0];outer.parent=None;outer.matrix_world=Matrix.Identity(4);bpy.context.scene.collection.objects.link(outer)
for mod in list(outer.modifiers):outer.modifiers.remove(mod)
for k in outer.data.shape_keys.key_blocks:k.value=0.
for ob in objects:ob.scale.x=.90
for m in outer.data.materials:
 bs=m.node_tree.nodes.get('Principled BSDF')
 for link in list(bs.inputs['Base Color'].links):m.node_tree.links.remove(link)
 bs.inputs['Base Color'].default_value=(.68,.72,.73,1)if m.name.startswith('MC_Enamel')else(.28,.36,.38,1);bs.inputs['Roughness'].default_value=.42;m.use_backface_culling=True
scene=bpy.context.scene;w=bpy.data.worlds.new('GeometryReview');scene.world=w;w.use_nodes=True;w.node_tree.nodes['Background'].inputs[0].default_value=(.02,.025,.03,1);w.node_tree.nodes['Background'].inputs[1].default_value=.45
for name,loc,power,size in [('Front',(0,12,8),1600,10),('Left',(-9,8,3),1000,8),('Right',(9,8,3),1000,8),('Rim',(0,-7,8),1500,8),('Cavity',(0,4,0),120,5)]:
 d=bpy.data.lights.new('GEO-Review'+name,'AREA');l=bpy.data.objects.new('GEO-Review'+name,d);scene.collection.objects.link(l);l.location=loc;l.rotation_euler=(Vector((0,-3,0))-l.location).to_track_quat('-Z','Y').to_euler();d.energy=power;d.shape='DISK';d.size=size
cd=bpy.data.cameras.new('GEO-ReviewCamera');cam=bpy.data.objects.new('GEO-ReviewCamera',cd);scene.collection.objects.link(cam);scene.camera=cam;scene.render.engine='BLENDER_EEVEE';scene.render.resolution_x=1600;scene.render.resolution_y=900;scene.render.resolution_percentage=100;scene.render.image_settings.file_format='PNG';scene.view_settings.view_transform='AgX'
for name,loc in [('composite_open_front',(0,35,0)),('composite_open_oblique',(12,30,7)),('interior_only_front',(0,35,0)),('interior_only_oblique',(12,30,7))]:
 outer.hide_render='interior_only'in name;cam.location=loc;cam.rotation_euler=(Vector((0,-1,0))-cam.location).to_track_quat('-Z','Y').to_euler();cd.type='ORTHO'if'front'in name else'PERSP';cd.ortho_scale=19.5;cd.lens=49.5;scene.render.filepath=str(OUT/(name+'.png'));bpy.ops.render.render(write_still=True)
# Additional native-height crop under simple warm physical lighting. The same
# constant basecolor is used at the rim, arches and deepest recess; no depth
# image or painted albedo creates the shadow.
for ob in list(bpy.data.objects):
 if ob.type=='LIGHT':bpy.data.objects.remove(ob,do_unlink=True)
w.node_tree.nodes['Background'].inputs[1].default_value=.07
for name,loc,power,size,color in [('WarmKey',(-4,8,7),1300,5,(1,.76,.59)),('CoolFill',(7,5,1),220,4,(.68,.78,1))]:
 d=bpy.data.lights.new(name,'AREA');l=bpy.data.objects.new(name,d);scene.collection.objects.link(l);l.location=loc;l.rotation_euler=(Vector((0,-7,0))-l.location).to_track_quat('-Z','Y').to_euler();d.energy=power;d.size=size;d.color=color
mi.node_tree.nodes.get('Principled BSDF').inputs['Base Color'].default_value=(.21,.063,.047,1);mt.node_tree.nodes.get('Principled BSDF').inputs['Base Color'].default_value=(.39,.125,.094,1)
for ob in [outer]+objects:
 half=opt.mask_half if ob==outer else opt.interior_mask_half
 for m in ob.data.materials:
  nodes=m.node_tree.nodes;links=m.node_tree.links;out=nodes.get('Material Output');bs=nodes.get('Principled BSDF');geo=nodes.new('ShaderNodeNewGeometry');sep=nodes.new('ShaderNodeSeparateXYZ');links.new(geo.outputs['Position'],sep.inputs[0]);sub=nodes.new('ShaderNodeMath');sub.operation='SUBTRACT';sub.inputs[1].default_value=opt.mask_center;links.new(sep.outputs['Z'],sub.inputs[0]);ab=nodes.new('ShaderNodeMath');ab.operation='ABSOLUTE';links.new(sub.outputs[0],ab.inputs[0]);greater=nodes.new('ShaderNodeMath');greater.operation='GREATER_THAN';greater.inputs[1].default_value=half;links.new(ab.outputs[0],greater.inputs[0]);tr=nodes.new('ShaderNodeBsdfTransparent');mix=nodes.new('ShaderNodeMixShader');links.new(greater.outputs[0],mix.inputs[0]);links.new(bs.outputs[0],mix.inputs[1]);links.new(tr.outputs[0],mix.inputs[2]);links.new(mix.outputs[0],out.inputs['Surface'])
outer.hide_render=False;cam.location=(0,35,0);cam.rotation_euler=(Vector((0,-1,0))-cam.location).to_track_quat('-Z','Y').to_euler();cd.type='ORTHO';cd.ortho_scale=19.5;scene.render.filepath=str(OUT/'native_height_warm_front.png');bpy.ops.render.render(write_still=True)
(OUT/'native_diagnostic_crop.json').write_text(json.dumps({'outer_card_half_source':opt.mask_half,'interior_tongue_half_source':opt.interior_mask_half,'center_source_z':opt.mask_center,'actor_uniform_scale_assumption':.537654,'outer_half_world_cm':opt.mask_half*53.7654,'interior_half_world_cm':opt.interior_mask_half*53.7654,'camera':'Diagnosticorthographic; heightmask matched, rootownsexactUEprojection','depth_shading':'Uniformalbedo, twoarealights, lowconstantworld; no projection/rasterdepthpaint'},indent=2));print('ORGANIC_INTERIOR_PROOF',json.dumps(report))
