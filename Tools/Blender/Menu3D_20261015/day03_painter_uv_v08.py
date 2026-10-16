"""Deterministic, unique per-material UV0 and Painter geometry/anatomy packet.

Called by finish_day03_reference_v08.py only after lead geometry review. Smart
projection supplies enamel charts; gums use known closed-loft parametric charts
with the seam on the lingual side, separate cap disks, and no center hero seam.
"""
import bpy,bmesh,numpy as np,json,math
from pathlib import Path

def finish_uv(o,parts,out):
 me=o.data;out=Path(out);bpy.ops.object.select_all(action='DESELECT');o.select_set(True);bpy.context.view_layer.objects.active=o
 layer=me.uv_layers.get('UV0') or me.uv_layers.new(name='UV0');me.uv_layers.active=layer
 before=np.empty(len(me.loops)*2,np.float32);layer.data.foreach_get('uv',before)
 bpy.ops.object.mode_set(mode='EDIT');bpy.ops.mesh.select_mode(type='FACE');bpy.ops.mesh.select_all(action='DESELECT')
 bm=bmesh.from_edit_mesh(me)
 for f in bm.faces:
  if f.material_index==0:f.select_set(True)
 bmesh.update_edit_mesh(me,loop_triangles=False,destructive=False)
 result=bpy.ops.uv.smart_project(angle_limit=math.radians(66),island_margin=.00390625,area_weight=.25,correct_aspect=True,scale_to_bounds=False)
 assert result=={'FINISHED'},result
 result=bpy.ops.uv.pack_islands(udim_source='CLOSEST_UDIM',rotate=False,scale=True,merge_overlap=False,margin_method='FRACTION',margin=.00390625)
 assert result=={'FINISHED'},result
 bpy.ops.object.mode_set(mode='OBJECT');me=o.data;uv=np.empty(len(me.loops)*2,np.float32);me.uv_layers['UV0'].data.foreach_get('uv',uv);uv=uv.reshape(-1,2)
 loops=np.empty(len(me.loops),np.int32);me.loops.foreach_get('vertex_index',loops)
 starts=np.empty(len(me.polygons),np.int32);counts=np.empty(len(me.polygons),np.int32);slots=np.empty(len(me.polygons),np.int32);me.polygons.foreach_get('loop_start',starts);me.polygons.foreach_get('loop_total',counts);me.polygons.foreach_get('material_index',slots)
 charts=[]
 for part in [p for p in parts if p['material_slot']==1]:
  st=part['vertex_start'];en=st+part['vertex_count'];lower='_Lower_' in part['mesh']
  if lower:rects=[(.69,.02,.98,.28),(.02,.67,.64,.93),(.69,.35,.98,.61)];capcenters=[(.75,.90),(.92,.90)]
  else:rects=[(.02,.35,.31,.61),(.02,.02,.64,.28),(.35,.35,.64,.61)];capcenters=[(.75,.73),(.92,.73)]
  intervals=[(0,60),(60,180),(180,240)]
  for index,(rect,interval) in enumerate(zip(rects,intervals)):charts.append({'part':part['mesh'],'chart':'body_'+str(index),'section_interval':interval,'uv_rect':rect,'lingual_seam_radial_index':20,'root_margin_radial_index':10 if lower else 30})
  for index,center in enumerate(capcenters):charts.append({'part':part['mesh'],'chart':'cap_'+str(index),'uv_rect':[center[0]-.055,center[1]-.055,center[0]+.055,center[1]+.055]})
  for s,c in zip(starts,counts):
   ids=loops[s:s+c]-st
   if not 0<=ids[0]<en-st:continue
   assert np.all((ids>=0)&(ids<en-st))
   if np.any(ids>=241*40):
    cap=int(ids.max()-241*40);center=np.array(capcenters[cap]);values=[]
    for v in ids:
     if v>=241*40:values.append(center)
     else:
      theta=2*math.pi*(int(v)%40)/40;values.append(center+.055*np.array([math.cos(theta),math.sin(theta)]))
    uv[s:s+c]=values
   else:
    section=ids//40;radial=(ids%40-20)%40
    if radial.max()-radial.min()>20:radial=np.where(radial==0,40,radial)
    i=int(section.min());chart=0 if i<60 else 1 if i<180 else 2;start,end=intervals[chart];x0,y0,x1,y1=rects[chart]
    uv[s:s+c]=np.column_stack([x0+(x1-x0)*(section-start)/(end-start),y0+(y1-y0)*radial/40])
 me.uv_layers['UV0'].data.foreach_set('uv',uv.astype(np.float32).ravel());me.update();assert np.isfinite(uv).all();assert uv.min()>=-1e-6 and uv.max()<=1+1e-6
 co=np.empty(len(me.vertices)*3,np.float32);me.shape_keys.key_blocks['Basis'].data.foreach_get('co',co);co=co.reshape(-1,3)
 me.calc_loop_triangles();tri=np.array([t.loops[:]for t in me.loop_triangles],np.int32);tri_mat=np.array([t.material_index for t in me.loop_triangles],np.int32)
 crown_root=np.zeros(len(co),np.float32);crown_tip=np.zeros(len(co),np.float32);gum_margin=np.zeros(len(co),np.float32);anatomy=[]
 def smooth(t):t=np.clip(t,0,1);return t*t*(3-2*t)
 for p in parts:
  st=p['vertex_start'];en=st+p['vertex_count'];c=co[st:en];lower='_Lower_' in p['mesh']
  if p['material_slot']==0:
   root=float(c[:,2].min() if lower else c[:,2].max());tip=float(c[:,2].max() if lower else c[:,2].min());t=np.clip((c[:,2]-root)/(tip-root),0,1);crown_root[st:en]=1-smooth(t/.50);crown_tip[st:en]=smooth((t-.70)/.30)
   anatomy.append({'part':p['mesh'],'root_z':root,'tip_z':tip,'root_to_tip_axis':'+Z' if lower else '-Z','root_weight':'1 atroot, fades0 byhalfheight','tip_weight':'0 until70%height,1 atincisal','uv_bounds':[uv[np.isin(loops,np.arange(st,en))].min(0).tolist(),uv[np.isin(loops,np.arange(st,en))].max(0).tolist()]})
  else:
   j=np.arange(p['vertex_count'])%40;theta=2*np.pi*j/40;target=np.pi/2 if lower else 3*np.pi/2;angle=(theta-target+np.pi)%(2*np.pi)-np.pi;values=np.exp(-(angle/.68)**2);values[241*40:]=0;gum_margin[st:en]=values
 np.savez_compressed(out/'texture_mesh_packet.npz',vertices=co,loop_vertex_indices=loops,loop_uv=uv,triangle_loop_indices=tri,triangle_material_indices=tri_mat,polygon_material_indices=slots,crown_root_weight=crown_root,crown_tip_weight=crown_tip,gum_margin_weight=gum_margin)
 report={'policy':'Unique0–1 UV0 per material; NO intentional mirror/repeatedprofile overlaps','texture_sets':['MC_Enamel','MC_Gum'],'resolution':4096,'padding_target_px':16,'enamel_pack_margin_fraction':.00390625,'enamel_padding_design':'32px pair gap at4K; independent validation required','enamel_chart_method':'SmartProject66degrees,areaweight.25,uniquecharts,noUVstacking,packrotateFalse','gum_chart_method':'3surface strips/arch, lingual seamradial20, separate2capdisks/arch','gum_charts':charts,'anatomy':anatomy,'packet_keys':['vertices','loop_vertex_indices','loop_uv','triangle_loop_indices','triangle_material_indices','polygon_material_indices','crown_root_weight','crown_tip_weight','gum_margin_weight'],'front_back_side_caps':'All faces carry nonzero-areacharts; independent overlap/distortion gate follows','uv_finite':True,'uv_in_0_1':True,'uv_changed':bool(not np.array_equal(before.reshape(-1,2),uv))}
 (out/'painter_uv_handoff.json').write_text(json.dumps(report,indent=2));return report

def export_static_obj(o,parts,out):
 out=Path(out);me=o.data;coords=np.empty(len(me.vertices)*3,np.float32)
 if me.shape_keys:me.shape_keys.key_blocks['Basis'].data.foreach_get('co',coords)
 else:me.vertices.foreach_get('co',coords)
 coords=coords.reshape(-1,3);uv=np.empty(len(me.loops)*2,np.float32);me.uv_layers['UV0'].data.foreach_get('uv',uv);uv=uv.reshape(-1,2)
 normals=np.empty(len(me.corner_normals)*3,np.float32);me.corner_normals.foreach_get('vector',normals);normals=normals.reshape(-1,3);assert len(normals)==len(me.loops)
 starts=np.empty(len(me.polygons),np.int32);counts=np.empty(len(me.polygons),np.int32);slots=np.empty(len(me.polygons),np.int32);me.polygons.foreach_get('loop_start',starts);me.polygons.foreach_get('loop_total',counts);me.polygons.foreach_get('material_index',slots);loops=np.empty(len(me.loops),np.int32);me.loops.foreach_get('vertex_index',loops);bounds=np.array([p['vertex_start']for p in parts])
 with (out/'MC_DAY03_Painter_Open.obj').open('w',newline='\n')as f:
  f.write('# v08 staticOpen texturemesh, same UV0/materials as nativeFBX\nmtllib MC_DAY03_Painter_Open.mtl\n')
  for c in coords:f.write('v %.8f %.8f %.8f\n'%tuple(c))
  for c in uv:f.write('vt %.8f %.8f\n'%tuple(c))
  for c in normals:f.write('vn %.8f %.8f %.8f\n'%tuple(c))
  f.write('s 1\n')
  last=None
  for s,c,slot in zip(starts,counts,slots):
   part=int(np.searchsorted(bounds,loops[s],side='right')-1)
   if part!=last:f.write('g '+parts[part]['mesh']+'\nusemtl '+('MC_Enamel' if slot==0 else 'MC_Gum')+'\n');last=part
   f.write('f '+' '.join(str(int(loops[i])+1)+'/'+str(int(i)+1)+'/'+str(int(i)+1)for i in range(s,s+c))+'\n')
 (out/'MC_DAY03_Painter_Open.mtl').write_text('newmtl MC_Enamel\nKd 0.8 0.8 0.8\nnewmtl MC_Gum\nKd 0.8 0.8 0.8\n')
 return str(out/'MC_DAY03_Painter_Open.obj')
