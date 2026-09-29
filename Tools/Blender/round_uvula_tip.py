"""Refine the authored uvula's bulb without rebuilding the throat or its animation."""
import bpy, math
from pathlib import Path
root=Path('E:/DEVGAME/MessControl/ArtSource/LivingThroat')
obj=bpy.data.objects['SM_Uvula']; assert len(obj.data.vertices)==193*97
for j in range(193):
    t=-1.6+2.6*j/192
    radius=.38+.11*(1+math.cos(max(0,t)*math.pi))+.05*math.sin(max(0,t)*math.pi)**2*math.sin(t*4.2+.6)
    if t<0:
        flare=min(1,-t/1.5); radius=.60+3.6*flare*flare*(3-2*flare)
    if t>.60:
        bulb=.46*math.sqrt(max(0,1-((t-.78)/.22)**2))
        blend=min(1,max(0,(t-.60)/.16)); blend=blend*blend*(3-2*blend)
        radius=radius*(1-blend)+bulb*blend
    radius=max(.0015,radius)
    for i in range(97):
        a=2*math.pi*i/96
        folded=radius*(1+.065*math.sin(a*5+t*10)+.035*math.cos(a*9-t*7))
        grain=.0017*math.sin(a*29+t*89)*math.sin(a*17-t*77)
        front=.30+.56*math.exp(-(t/.45)**2) if t<0 else .86
        obj.data.vertices[j*97+i].co=(.21*math.sin(t*math.pi)+math.cos(a)*(folded+grain)*front,.065*math.sin(t*math.pi*2)+math.sin(a)*(folded+grain),-t)
obj.data.update()
for o in bpy.context.selected_objects:o.select_set(False)
obj.hide_set(False);obj.select_set(True);bpy.context.view_layer.objects.active=obj
matrix=obj.matrix_world.copy();obj.location=(0,0,0);obj.scale=(1,1,1)
bpy.ops.export_scene.fbx(filepath=str(root/'SM_Uvula.fbx'),use_selection=True,object_types={'MESH'},apply_unit_scale=True,axis_forward='-Y',axis_up='Z',mesh_smooth_type='FACE',bake_anim=False)
obj.matrix_world=matrix
bpy.ops.wm.save_as_mainfile(filepath=str(root/'LivingThroat.blend'))
result={'mesh':obj.name,'vertices':len(obj.data.vertices),'export':str(root/'SM_Uvula.fbx')}
