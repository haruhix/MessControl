"""Ephemeral offline normalization experiment; never saves any blend."""
import bpy,json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
mesh=bpy.data.objects['SM_NutWizard'];rig=bpy.data.objects['rig'];scene=bpy.context.scene
rig.animation_data.use_nla=False
action=bpy.data.actions['MC_Wizard_Cast']
rig.animation_data.action=action
if action.slots: rig.animation_data.action_slot=action.slots[0]
deforms={b.name for b in rig.data.bones if b.use_deform}
groups={g.index:g for g in mesh.vertex_groups}
samples=[1,24,48,56,64,90,109]
def evaluated_world(frame):
    scene.frame_set(frame);bpy.context.view_layer.update()
    obj=mesh.evaluated_get(bpy.context.evaluated_depsgraph_get())
    result=[obj.matrix_world@v.co for v in obj.data.vertices]
    return result
original={frame:evaluated_world(frame) for frame in samples}
replacements=[]
for vertex in mesh.data.vertices:
    values=[(g.group,g.weight) for g in vertex.groups if groups[g.group].name in deforms]
    total=sum(w for _,w in values)
    if total>0:
        replacements.extend((gi,vertex.index,w/total) for gi,w in values)
for gi,vi,w in replacements:
    groups[gi].add([vi],w,'REPLACE')
mesh.data.update();mesh.update_tag();rig.update_tag()
result=dict(source=bpy.data.filepath,scope='ephemeral in-memory standalone process; blend NOT saved',
    normalization='all deform-bone weights normalized per vertex, ratios retained',frames=[])
for frame in samples:
    changed=evaluated_world(frame)
    differences=[(a-b).length for a,b in zip(original[frame],changed)]
    result['frames'].append(dict(frame=frame,max_world_delta_m=max(differences),
        mean_world_delta_m=sum(differences)/len(differences)))
output=ROOT/'Saved/Codex/WizardWeightNormalizationProbe.json'
output.write_text(json.dumps(result,indent=2),encoding='utf8')
print(json.dumps(result),flush=True)
