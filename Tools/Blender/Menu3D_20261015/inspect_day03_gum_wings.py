"""Read-only headless diagnosis of source gingiva and exported pose ranges."""
import bpy, json, numpy as np
from pathlib import Path
assert bpy.app.background
base=Path('E:/DEVGAME/MessControl/ArtSource/UI/MainMenu3D/20261015_refB_v1')
out=base/'Assembly/UnrealHandoff/v05_GumWidthRepair'
out.mkdir(parents=True,exist_ok=True)
source=base/'Gums/SymmetricLower_DAY03_v01/DCC_v02/MC_DAY03_SymmetricLowerCompound.blend'
bpy.ops.wm.open_mainfile(filepath=str(source))
report={'version':bpy.app.version_string,'source':str(source),'objects':[]}
root=bpy.data.objects['MC_JawsRoot']
for ob in sorted((o for o in bpy.data.objects if o.type=='MESH' and 'DAY03' in o.name),key=lambda o:o.name):
    data={'name':ob.name,'vertices':len(ob.data.vertices),'modifiers':[(m.name,m.type) for m in ob.modifiers],
          'keys':[k.name for k in ob.data.shape_keys.key_blocks] if ob.data.shape_keys else [],'poses':{}}
    for amount in (0.,.25,.5,.75,1.):
        root.animation_data_clear();root['OpenAmount']=amount;root.update_tag();bpy.context.scene.frame_set(42);bpy.context.view_layer.update()
        dg=bpy.context.evaluated_depsgraph_get();ev=ob.evaluated_get(dg);me=ev.to_mesh()
        a=np.empty(len(me.vertices)*3);me.vertices.foreach_get('co',a);a=a.reshape(-1,3)
        m=np.asarray(ev.matrix_world);a=a@m[:3,:3].T+m[:3,3]
        p={'min':a.min(0).tolist(),'max':a.max(0).tolist()}
        if 'Gum' in ob.name:
            p['x_bins']=[]
            for lo,hi in [(0,5),(5,6),(6,7),(7,8),(8,9),(9,10)]:
                b=a[(abs(a[:,0])>=lo)&(abs(a[:,0])<hi)]
                if len(b):p['x_bins'].append({'range':[lo,hi],'n':len(b),'min':b.min(0).tolist(),'max':b.max(0).tolist()})
        data['poses'][str(amount)]=p;ev.to_mesh_clear()
    report['objects'].append(data)
(out/'source_inspection.json').write_text(json.dumps(report,indent=2))
print(json.dumps(report))
